#include "telegram.h"
#include "telegram_platform.h"
#include "../config/config.h"
#include "../logging/logging.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TELEGRAM_MAX_CHARACTERS 4096
#define TELEGRAM_MAX_CAPTION_BYTES 1023
#define TELEGRAM_DEDUPLICATION_CAPACITY 1024

static const char *bot_token;
static const char *chat_id;
static int initialized;
static int deduplicate;

typedef struct sent_content {
    uint64_t hash[2];
    size_t length;
    int valid;
} sent_content_t;

static sent_content_t sent_contents[TELEGRAM_DEDUPLICATION_CAPACITY];
static size_t sent_content_count;
static size_t next_sent_content;

static size_t bounded_length(const char *text, size_t capacity)
{
    size_t length = 0;
    while (length < capacity && text[length])
        ++length;
    return length;
}

static size_t append_source_value(char *output, size_t offset, size_t capacity,
                                  const char *value, size_t value_capacity)
{
    size_t length = bounded_length(value, value_capacity);
    int previous_space = offset > 0 && output[offset - 1] == ' ';
    for (size_t index = 0; index < length && offset + 1 < capacity; ++index) {
        unsigned char character = (unsigned char)value[index];
        if (character < 32 || character == 127) {
            if (!previous_space && offset > 0)
                output[offset++] = ' ';
            previous_space = 1;
        } else {
            size_t width = 1;
            if ((character & 0xe0) == 0xc0)
                width = 2;
            else if ((character & 0xf0) == 0xe0)
                width = 3;
            else if ((character & 0xf8) == 0xf0)
                width = 4;
            if (width > length - index || width > capacity - offset - 1)
                break;
            int valid = 1;
            for (size_t continuation = 1; continuation < width;
                 ++continuation) {
                if (((unsigned char)value[index + continuation] & 0xc0) !=
                    0x80) {
                    valid = 0;
                    break;
                }
            }
            if (!valid)
                width = 1;
            memcpy(output + offset, value + index, width);
            offset += width;
            index += width - 1;
            previous_space = character == ' ';
        }
    }
    while (offset > 0 && output[offset - 1] == ' ')
        --offset;
    output[offset] = '\0';
    return offset;
}

static size_t format_source(const c2t_clipboard_source_t *source,
                            char output[TELEGRAM_MAX_CAPTION_BYTES + 1])
{
    if (!source || (!source->application[0] && !source->title[0] &&
                    !source->process_id)) {
        output[0] = '\0';
        return 0;
    }

    static const char prefix[] = "Source: ";
    memcpy(output, prefix, sizeof(prefix) - 1);
    size_t offset = sizeof(prefix) - 1;
    int has_details = 0;
    if (source->application[0]) {
        size_t value_start = offset;
        offset = append_source_value(output, offset,
                                     TELEGRAM_MAX_CAPTION_BYTES + 1,
                                     source->application,
                                     sizeof(source->application));
        has_details = offset > value_start;
        if (!has_details) {
            memcpy(output, prefix, sizeof(prefix) - 1);
            offset = sizeof(prefix) - 1;
        }
    }
    if (source->title[0] && offset + 3 < TELEGRAM_MAX_CAPTION_BYTES) {
        size_t previous_offset = offset;
        if (has_details) {
            memcpy(output + offset, " | ", 3);
            offset += 3;
        }
        size_t value_start = offset;
        offset = append_source_value(output, offset,
                                     TELEGRAM_MAX_CAPTION_BYTES + 1,
                                     source->title, sizeof(source->title));
        if (offset > value_start)
            has_details = 1;
        else
            offset = previous_offset;
    }
    if (source->process_id && offset + 20 < TELEGRAM_MAX_CAPTION_BYTES) {
        int written = snprintf(output + offset,
                               TELEGRAM_MAX_CAPTION_BYTES + 1 - offset,
                               "%sPID %lu", has_details ? " | " : "",
                               (unsigned long)source->process_id);
        if (written > 0) {
            offset += (size_t)written;
            has_details = 1;
        }
    }
    if (!has_details) {
        output[0] = '\0';
        return 0;
    }
    output[offset] = '\0';
    return offset;
}

static void content_hash(const void *data, size_t length,
                         const c2t_clipboard_source_t *source,
                         uint64_t hash[2])
{
    const unsigned char *bytes = data;
    hash[0] = UINT64_C(14695981039346656037);
    hash[1] = UINT64_C(7809847782465536322);
    for (size_t index = 0; index < length; ++index) {
        hash[0] ^= bytes[index];
        hash[0] *= UINT64_C(1099511628211);
        hash[1] ^= bytes[index];
        hash[1] *= UINT64_C(14029467366897019727);
    }
    char source_text[TELEGRAM_MAX_CAPTION_BYTES + 1];
    size_t source_length = format_source(source, source_text);
    for (size_t index = 0; index < source_length; ++index) {
        hash[0] ^= (unsigned char)source_text[index];
        hash[0] *= UINT64_C(1099511628211);
        hash[1] ^= (unsigned char)source_text[index];
        hash[1] *= UINT64_C(14029467366897019727);
    }
}

/* Returns 1 for a duplicate and 0 when the send may proceed. */
static int prepare_send(const void *data, size_t length,
                        const c2t_clipboard_source_t *source,
                        sent_content_t *pending)
{
    pending->valid = 0;
    if (!deduplicate)
        return 0;

    uint64_t hash[2];
    content_hash(data, length, source, hash);
    for (size_t index = 0; index < sent_content_count; ++index) {
        const sent_content_t *entry = &sent_contents[index];
        if (entry->length == length && entry->hash[0] == hash[0] &&
            entry->hash[1] == hash[1])
            return 1;
    }

    pending->hash[0] = hash[0];
    pending->hash[1] = hash[1];
    pending->length = length;
    pending->valid = 1;
    return 0;
}

static int finish_send(sent_content_t *pending, int result)
{
    if (!pending->valid)
        return result;
    if (!result)
        return 0;
    sent_contents[next_sent_content] = *pending;
    next_sent_content =
        (next_sent_content + 1) % TELEGRAM_DEDUPLICATION_CAPACITY;
    if (sent_content_count < TELEGRAM_DEDUPLICATION_CAPACITY)
        ++sent_content_count;
    c2t_log_debug("telegram", "Content hash stored after successful delivery");
    return 1;
}

static void clear_sent_contents(void)
{
    sent_content_count = 0;
    next_sent_content = 0;
}

static int token_is_valid(const char *token)
{
    size_t length = strlen(token);
    if (length == 0 || length > 256)
        return 0;

    for (size_t index = 0; index < length; ++index) {
        unsigned char character = (unsigned char)token[index];
        if (!isalnum(character) && character != ':' && character != '_' &&
            character != '-')
            return 0;
    }
    return 1;
}

static int chat_is_valid(const char *chat)
{
    size_t length = strlen(chat);
    if (length == 0 || length > 256)
        return 0;
    for (size_t index = 0; index < length; ++index) {
        unsigned char character = (unsigned char)chat[index];
        if (!isalnum(character) && character != '-' && character != '_' &&
            character != '@')
            return 0;
    }
    return 1;
}

static int is_unreserved(unsigned char character)
{
    return isalnum(character) || character == '-' || character == '_' ||
           character == '.' || character == '~';
}

typedef struct {
    const char *name;
    const char *value;
    size_t length;
} form_field_t;

static char *form_encode(const char *value, size_t length)
{
    if (length > (SIZE_MAX - 1) / 3)
        return NULL;

    static const char hexadecimal[] = "0123456789ABCDEF";
    char *encoded = malloc(length * 3 + 1);
    if (!encoded)
        return NULL;

    char *output = encoded;
    for (size_t index = 0; index < length; ++index) {
        unsigned char character = (unsigned char)value[index];
        if (is_unreserved(character)) {
            *output++ = (char)character;
        } else {
            *output++ = '%';
            *output++ = hexadecimal[character >> 4];
            *output++ = hexadecimal[character & 0x0f];
        }
    }
    *output = '\0';
    return encoded;
}

static size_t utf8_chunk_length(const char *text, size_t length,
                                size_t maximum_characters)
{
    size_t offset = 0;
    size_t characters = 0;
    while (offset < length && characters < maximum_characters) {
        unsigned char first = (unsigned char)text[offset];
        size_t width = 1;
        if ((first & 0xe0) == 0xc0)
            width = 2;
        else if ((first & 0xf0) == 0xe0)
            width = 3;
        else if ((first & 0xf8) == 0xf0)
            width = 4;

        if (width > length - offset)
            width = 1;
        offset += width;
        ++characters;
    }
    return offset;
}

static int send_fields(const char *method, const form_field_t *fields,
                       size_t field_count)
{
    if (field_count > 4)
        return 0;

    char *encoded_chat = form_encode(chat_id, strlen(chat_id));
    char *encoded_values[4] = {0};
    if (!encoded_chat) {
        c2t_log_error("telegram", "Not enough memory for message encoding");
        return 0;
    }

    size_t body_length = strlen("chat_id=") + strlen(encoded_chat);
    for (size_t index = 0; index < field_count; ++index) {
        encoded_values[index] = form_encode(fields[index].value,
                                             fields[index].length);
        if (!encoded_values[index] ||
            strlen(fields[index].name) > SIZE_MAX - body_length - 2 ||
            strlen(encoded_values[index]) >
                SIZE_MAX - body_length - strlen(fields[index].name) - 2) {
            for (size_t free_index = 0; free_index <= index; ++free_index)
                free(encoded_values[free_index]);
            free(encoded_chat);
            c2t_log_error("telegram", "Not enough memory for message encoding");
            return 0;
        }
        body_length += 2 + strlen(fields[index].name) +
                       strlen(encoded_values[index]);
    }

    char *body = malloc(body_length + 1);
    if (!body) {
        for (size_t index = 0; index < field_count; ++index)
            free(encoded_values[index]);
        free(encoded_chat);
        c2t_log_error("telegram", "Not enough memory for message body");
        return 0;
    }

    size_t offset = (size_t)snprintf(body, body_length + 1, "chat_id=%s",
                                     encoded_chat);
    for (size_t index = 0; index < field_count; ++index) {
        offset += (size_t)snprintf(body + offset, body_length + 1 - offset,
                                  "&%s=%s", fields[index].name,
                                  encoded_values[index]);
    }

    int result = telegram_http_post(
        bot_token, method, "application/x-www-form-urlencoded",
        body, body_length);
    free(body);
    for (size_t index = 0; index < field_count; ++index)
        free(encoded_values[index]);
    free(encoded_chat);
    return result;
}

static int send_form(const char *text, size_t length)
{
    form_field_t field = {"text", text, length};
    return send_fields("sendMessage", &field, 1);
}

static int send_contact(const char *phone, size_t phone_length,
                        const char *name, size_t name_length,
                        const char *vcard, size_t vcard_length)
{
    form_field_t fields[3] = {
        {"phone_number", phone, phone_length},
        {"first_name", name, name_length},
        {"vcard", vcard, vcard_length}
    };
    return send_fields("sendContact", fields, vcard ? 3 : 2);
}

static int send_location(const char *latitude, const char *longitude)
{
    form_field_t fields[2] = {
        {"latitude", latitude, strlen(latitude)},
        {"longitude", longitude, strlen(longitude)}
    };
    return send_fields("sendLocation", fields, 2);
}

static int ascii_equal_nocase(const char *left, const char *right,
                              size_t length)
{
    for (size_t index = 0; index < length; ++index) {
        if (tolower((unsigned char)left[index]) !=
            tolower((unsigned char)right[index]))
            return 0;
    }
    return 1;
}

static void trim_text(const char **text, size_t *length)
{
    while (*length > 0 && isspace((unsigned char)**text)) {
        ++*text;
        --*length;
    }
    while (*length > 0 && isspace((unsigned char)(*text)[*length - 1]))
        --*length;
}

static int parse_phone(const char *text, size_t length, char phone[17],
                       size_t *phone_length)
{
    trim_text(&text, &length);
    if (length == 0 || length > 64)
        return 0;

    size_t input_offset;
    size_t output = 1;
    phone[0] = '+';
    if (text[0] == '+') {
        input_offset = 1;
    } else if (length >= 2 && text[0] == '0' && text[1] == '0') {
        input_offset = 2;
    } else {
        return 0;
    }

    for (size_t index = input_offset; index < length; ++index) {
        unsigned char character = (unsigned char)text[index];
        if (isdigit(character)) {
            if (output >= 16)
                return 0;
            phone[output++] = (char)character;
        } else if (character != ' ' && character != '-' &&
                   character != '(' && character != ')' && character != '.') {
            return 0;
        }
    }

    size_t digits = output - 1;
    if (digits < 7 || digits > 15)
        return 0;
    phone[output] = '\0';
    *phone_length = output;
    return 1;
}

static int find_vcard_value(const char *text, size_t length, const char *key,
                            const char **value, size_t *value_length)
{
    size_t key_length = strlen(key);
    size_t offset = 0;
    while (offset < length) {
        size_t end = offset;
        while (end < length && text[end] != '\r' && text[end] != '\n')
            ++end;
        if (end - offset > key_length &&
            ascii_equal_nocase(text + offset, key, key_length) &&
            (text[offset + key_length] == ':' ||
             text[offset + key_length] == ';')) {
            size_t colon = offset + key_length;
            while (colon < end && text[colon] != ':')
                ++colon;
            if (colon < end) {
                *value = text + colon + 1;
                *value_length = end - colon - 1;
                trim_text(value, value_length);
                return *value_length > 0;
            }
        }
        offset = end;
        while (offset < length && (text[offset] == '\r' || text[offset] == '\n'))
            ++offset;
    }
    return 0;
}

static int parse_location(const char *text, size_t length,
                          char latitude[32], char longitude[32])
{
    trim_text(&text, &length);
    int has_geo_prefix = length >= 4 &&
                         ascii_equal_nocase(text, "geo:", 4);
    if (has_geo_prefix) {
        text += 4;
        length -= 4;
    }
    if (length == 0 || length >= 128)
        return 0;

    char input[128];
    memcpy(input, text, length);
    input[length] = '\0';
    char *separator = strchr(input, ',');
    if (!separator || strchr(separator + 1, ','))
        return 0;
    if (!has_geo_prefix &&
        (!strchr(input, '.') || !strchr(separator + 1, '.')))
        return 0;

    char *latitude_end;
    char *longitude_end;
    double latitude_value = strtod(input, &latitude_end);
    while (isspace((unsigned char)*latitude_end))
        ++latitude_end;
    if (latitude_end != separator)
        return 0;
    double longitude_value = strtod(separator + 1, &longitude_end);
    while (isspace((unsigned char)*longitude_end))
        ++longitude_end;
    if (*longitude_end || latitude_value < -90.0 || latitude_value > 90.0 ||
        longitude_value < -180.0 || longitude_value > 180.0)
        return 0;

    snprintf(latitude, 32, "%.8f", latitude_value);
    snprintf(longitude, 32, "%.8f", longitude_value);
    return 1;
}

static int text_is_url(const char *text, size_t length)
{
    trim_text(&text, &length);
    size_t prefix = length >= 8 && ascii_equal_nocase(text, "https://", 8)
                        ? 8
                        : (length >= 7 && ascii_equal_nocase(text, "http://", 7)
                               ? 7 : 0);
    if (!prefix || prefix == length)
        return 0;
    for (size_t index = prefix; index < length; ++index) {
        if (isspace((unsigned char)text[index]))
            return 0;
    }
    return 1;
}

static int send_rich_text(const char *text, size_t length, int *recognized)
{
    const char *trimmed = text;
    size_t trimmed_length = length;
    trim_text(&trimmed, &trimmed_length);
    *recognized = 0;

    if (trimmed_length >= 11 &&
        ascii_equal_nocase(trimmed, "BEGIN:VCARD", 11)) {
        const char *phone_value;
        const char *name = "Clipboard";
        size_t phone_value_length;
        size_t name_length = strlen(name);
        char phone[17];
        size_t phone_length;
        if (find_vcard_value(trimmed, trimmed_length, "TEL", &phone_value,
                             &phone_value_length) &&
            parse_phone(phone_value, phone_value_length, phone, &phone_length)) {
            find_vcard_value(trimmed, trimmed_length, "FN", &name, &name_length);
            *recognized = 1;
            c2t_log_info("telegram", "Recognized vCard contact");
            return send_contact(phone, phone_length, name, name_length,
                                trimmed_length <= 2048 ? trimmed : NULL,
                                trimmed_length);
        }
    }

    char phone[17];
    size_t phone_length;
    if (parse_phone(trimmed, trimmed_length, phone, &phone_length)) {
        static const char contact_name[] = "Clipboard";
        *recognized = 1;
        c2t_log_info("telegram", "Recognized phone number as contact");
        return send_contact(phone, phone_length, contact_name,
                            sizeof(contact_name) - 1, NULL, 0);
    }

    char latitude[32];
    char longitude[32];
    if (parse_location(trimmed, trimmed_length, latitude, longitude)) {
        *recognized = 1;
        c2t_log_info("telegram", "Recognized geographic coordinates");
        return send_location(latitude, longitude);
    }

    if (text_is_url(trimmed, trimmed_length))
        c2t_log_info("telegram", "Recognized URL; using Telegram link preview");
    return 1;
}

static int mime_is(const char *mime_type, const char *expected)
{
    size_t length = strlen(expected);
    return strlen(mime_type) >= length &&
           ascii_equal_nocase(mime_type, expected, length) &&
           (mime_type[length] == '\0' || mime_type[length] == ';' ||
            isspace((unsigned char)mime_type[length]));
}

static int mime_has_prefix(const char *mime_type, const char *prefix)
{
    size_t length = strlen(prefix);
    return strlen(mime_type) >= length &&
           ascii_equal_nocase(mime_type, prefix, length);
}

static int contains_bytes(const unsigned char *data, size_t length,
                          const char *needle)
{
    size_t needle_length = strlen(needle);
    if (needle_length > length)
        return 0;
    for (size_t index = 0; index <= length - needle_length; ++index) {
        if (memcmp(data + index, needle, needle_length) == 0)
            return 1;
    }
    return 0;
}

static int add_size(size_t *total, size_t value)
{
    if (value > SIZE_MAX - *total)
        return 0;
    *total += value;
    return 1;
}

static void sanitize_filename(const char *filename, char output[256])
{
    size_t index = 0;
    if (filename) {
        while (*filename && index < 255) {
            unsigned char character = (unsigned char)*filename++;
            output[index++] = character < 32 || character == '"' ||
                              character == '\\' ? '_' : (char)character;
        }
    }
    if (index == 0) {
        memcpy(output, "clipboard.bin", sizeof("clipboard.bin"));
        return;
    }
    output[index] = '\0';
}

static int send_file(const void *data, size_t length, const char *mime_type,
                     const char *requested_filename, int allow_photo,
                     const c2t_clipboard_source_t *source)
{
    const char *method;
    const char *field;
    const char *filename;
    if (requested_filename) {
        method = "sendDocument";
        field = "document";
        filename = requested_filename;
    } else if (allow_photo && mime_is(mime_type, "image/png")) {
        method = "sendPhoto";
        field = "photo";
        filename = "clipboard.png";
    } else if (allow_photo && mime_is(mime_type, "image/jpeg")) {
        method = "sendPhoto";
        field = "photo";
        filename = "clipboard.jpg";
    } else {
        method = "sendDocument";
        field = "document";
        if (mime_is(mime_type, "image/bmp"))
            filename = "clipboard.bmp";
        else if (mime_is(mime_type, "image/webp"))
            filename = "clipboard.webp";
        else if (mime_is(mime_type, "image/gif"))
            filename = "clipboard.gif";
        else
            filename = "clipboard.bin";
    }

    char safe_filename[256];
    sanitize_filename(filename, safe_filename);

    char boundary[48];
    char source_text[TELEGRAM_MAX_CAPTION_BYTES + 1];
    size_t source_length = format_source(source, source_text);
    unsigned int suffix = 0;
    do {
        snprintf(boundary, sizeof(boundary), "c2tBoundary%u", suffix++);
    } while (contains_bytes(data, length, boundary) ||
             contains_bytes((const unsigned char *)source_text, source_length,
                            boundary));

    static const char first_format[] =
        "--%s\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n"
        "%s\r\n--%s\r\nContent-Disposition: form-data; name=\"%s\"; "
        "filename=\"%s\"\r\nContent-Type: %s\r\n\r\n";
    static const char caption_format[] =
        "--%s\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n"
        "%s\r\n--%s\r\nContent-Disposition: form-data; name=\"caption\""
        "\r\n\r\n%s\r\n--%s\r\nContent-Disposition: form-data; "
        "name=\"%s\"; filename=\"%s\"\r\nContent-Type: %s\r\n\r\n";
    const char *prefix_format = source_length ? caption_format : first_format;
    int prefix_length = source_length
        ? snprintf(NULL, 0, prefix_format, boundary, chat_id, boundary,
                   source_text, boundary, field, safe_filename, mime_type)
        : snprintf(NULL, 0, prefix_format, boundary, chat_id, boundary, field,
                   safe_filename, mime_type);
    int suffix_length = snprintf(NULL, 0, "\r\n--%s--\r\n", boundary);
    if (prefix_length < 0 || suffix_length < 0)
        return 0;

    size_t body_length = (size_t)prefix_length;
    if (!add_size(&body_length, length) ||
        !add_size(&body_length, (size_t)suffix_length)) {
        c2t_log_error("telegram", "Clipboard file is too large");
        return 0;
    }

    if (body_length == SIZE_MAX)
        return 0;
    unsigned char *body = malloc(body_length + 1);
    if (!body) {
        c2t_log_error("telegram", "Not enough memory for upload body");
        return 0;
    }
    if (source_length)
        snprintf((char *)body, (size_t)prefix_length + 1, prefix_format,
                 boundary, chat_id, boundary, source_text, boundary, field,
                 safe_filename, mime_type);
    else
        snprintf((char *)body, (size_t)prefix_length + 1, prefix_format,
                 boundary, chat_id, boundary, field, safe_filename, mime_type);
    memcpy(body + prefix_length, data, length);
    snprintf((char *)body + prefix_length + length, (size_t)suffix_length + 1,
             "\r\n--%s--\r\n", boundary);

    char content_type[96];
    snprintf(content_type, sizeof(content_type),
             "multipart/form-data; boundary=%s", boundary);
    int result = telegram_http_post(bot_token, method, content_type,
                                    body, body_length);
    free(body);
    return result;
}

int telegram_init(void)
{
    const c2t_config_t *config = c2t_config_get();
    deduplicate = config->telegram_deduplicate;
    if (!config->telegram_enabled) {
        c2t_log_info("telegram", "Telegram integration is disabled");
        return 1;
    }

    c2t_log_info("telegram", "Telegram integration enabled; deduplication=%s",
                 deduplicate ? "enabled" : "disabled");

    bot_token = config->telegram_bot_token;
    chat_id = config->telegram_chat_id;
    if (!bot_token || !token_is_valid(bot_token)) {
        c2t_log_error("telegram", "TELEGRAM_BOT_TOKEN is missing or invalid");
        return 0;
    }
    if (!chat_id || !chat_is_valid(chat_id)) {
        c2t_log_error("telegram", "TELEGRAM_CHAT_ID is missing or invalid");
        return 0;
    }
    c2t_log_debug("telegram", "Initializing HTTPS transport");
    initialized = telegram_http_init();
    if (initialized)
        c2t_log_info("telegram", "Telegram transport initialized");
    return initialized;
}

int telegram_send(const char *text, size_t length,
                  const c2t_clipboard_source_t *source)
{
    if (!initialized)
        return 1;
    if (length == 0)
        return 1;

    sent_content_t pending;
    int preparation = prepare_send(text, length, source, &pending);
    if (preparation > 0) {
        c2t_log_info("telegram", "Skipping duplicate text (%llu bytes)",
                     (unsigned long long)length);
        return 1;
    }
    int rich_type;
    int rich_result = send_rich_text(text, length, &rich_type);
    if (rich_type) {
        char source_text[TELEGRAM_MAX_CAPTION_BYTES + 1];
        size_t source_length = format_source(source, source_text);
        if (rich_result && source_length)
            rich_result = send_form(source_text, source_length);
        rich_result = finish_send(&pending, rich_result);
        c2t_log_info("telegram", "Rich content delivery %s",
                     rich_result ? "completed" : "failed");
        return rich_result;
    }

    c2t_log_info("telegram", "Sending text (%llu bytes)",
                 (unsigned long long)length);

    int result = 1;
    size_t chunk_index = 0;
    char source_text[TELEGRAM_MAX_CAPTION_BYTES + 1];
    size_t source_length = format_source(source, source_text);
    if (source_length) {
        size_t source_characters = 0;
        for (size_t index = 0; index < source_length; ++index) {
            if (((unsigned char)source_text[index] & 0xc0) != 0x80)
                ++source_characters;
        }
        size_t available = TELEGRAM_MAX_CHARACTERS - source_characters - 2;
        size_t chunk_length = utf8_chunk_length(text, length, available);
        size_t message_length = source_length + 2 + chunk_length;
        char *message = malloc(message_length);
        if (!message) {
            return 0;
        }
        memcpy(message, source_text, source_length);
        memcpy(message + source_length, "\n\n", 2);
        memcpy(message + source_length + 2, text, chunk_length);
        result = send_form(message, message_length);
        free(message);
        text += chunk_length;
        length -= chunk_length;
        ++chunk_index;
    }
    while (length > 0) {
        size_t chunk_length = utf8_chunk_length(text, length,
                                                TELEGRAM_MAX_CHARACTERS);
        c2t_log_debug("telegram", "Sending text chunk %llu (%llu bytes)",
                      (unsigned long long)++chunk_index,
                      (unsigned long long)chunk_length);
        if (!send_form(text, chunk_length))
            result = 0;
        text += chunk_length;
        length -= chunk_length;
    }
    result = finish_send(&pending, result);
    c2t_log_info("telegram", "Text delivery %s", result ? "completed" : "failed");
    return result;
}

int telegram_send_data(const void *data, size_t length, const char *mime_type,
                       const c2t_clipboard_source_t *source)
{
    if (!initialized || length == 0)
        return 1;
    if (!data || !mime_type)
        return 0;
    if (mime_has_prefix(mime_type, "text/"))
        return telegram_send(data, length, source);
    if (mime_has_prefix(mime_type, "image/")) {
        sent_content_t pending;
        int preparation = prepare_send(data, length, source, &pending);
        if (preparation > 0) {
            c2t_log_info("telegram", "Skipping duplicate file (%s, %llu bytes)",
                         mime_type, (unsigned long long)length);
            return 1;
        }
        c2t_log_info("telegram", "Uploading file (%s, %llu bytes)", mime_type,
                     (unsigned long long)length);
        int result = finish_send(
            &pending, send_file(data, length, mime_type, NULL, 1, source));
        c2t_log_info("telegram", "File delivery %s",
                     result ? "completed" : "failed");
        return result;
    }

    c2t_log_warning("telegram", "Unsupported clipboard MIME type: %s",
                    mime_type);
    return 0;
}

int telegram_send_file(const void *data, size_t length, const char *mime_type,
                       const char *filename,
                       const c2t_clipboard_source_t *source)
{
    if (!initialized)
        return 1;
    if ((!data && length != 0) || !mime_type || !filename || !*filename)
        return 0;

    sent_content_t pending;
    int preparation = prepare_send(data, length, source, &pending);
    if (preparation > 0) {
        c2t_log_info("telegram", "Skipping duplicate filesystem file (%s)",
                     filename);
        return 1;
    }
    c2t_log_info("telegram", "Uploading filesystem file: name=%s, type=%s, "
                 "size=%llu bytes", filename, mime_type,
                 (unsigned long long)length);
    int result = finish_send(
        &pending, send_file(data, length, mime_type, filename, 0, source));
    c2t_log_info("telegram", "Filesystem file delivery %s",
                 result ? "completed" : "failed");
    return result;
}

void telegram_cleanup(void)
{
    c2t_log_debug("telegram", "Cleaning up Telegram state");
    if (initialized)
        telegram_http_cleanup();
    initialized = 0;
    deduplicate = 0;
    clear_sent_contents();
}
