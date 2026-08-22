/*
 * Copyright (C) 2026 Antonio Ricciardi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "telegram.h"
#include "telegram_platform.h"
#include "../config/config.h"
#include "../logging/logging.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <pthread.h>
#include <unistd.h>
static pthread_mutex_t telegram_mutex = PTHREAD_MUTEX_INITIALIZER;
static void telegram_lock(void) { (void)pthread_mutex_lock(&telegram_mutex); }
static void telegram_unlock(void) { (void)pthread_mutex_unlock(&telegram_mutex); }
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static CRITICAL_SECTION telegram_mutex;
static int telegram_mutex_initialized;
static void telegram_lock(void)
{
    if (!telegram_mutex_initialized) {
        InitializeCriticalSection(&telegram_mutex);
        telegram_mutex_initialized = 1;
    }
    EnterCriticalSection(&telegram_mutex);
}
static void telegram_unlock(void) { LeaveCriticalSection(&telegram_mutex); }
#endif

#define TELEGRAM_MAX_CHARACTERS 4096U
#define TELEGRAM_MAX_CAPTION_BYTES 1023U
#define TELEGRAM_DEDUP_TABLE_SIZE 2048U
#define TELEGRAM_DEDUP_TABLE_MASK (TELEGRAM_DEDUP_TABLE_SIZE - 1U)
#define TELEGRAM_DEDUPLICATION_CAPACITY 1024U

static const char *bot_token;
static const char *chat_id;
static int initialized;
static int deduplicate;

typedef struct sent_content {
    uint64_t hash[2];
    size_t length;
    int valid;
} sent_content_t;

static sent_content_t sent_contents_table[TELEGRAM_DEDUP_TABLE_SIZE];
static uint32_t sent_order[TELEGRAM_DEDUPLICATION_CAPACITY];
static size_t sent_order_head;
static size_t sent_order_count;

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

    size_t words = length / 8;
    for (size_t w = 0; w < words; ++w) {
        uint64_t word_val;
        memcpy(&word_val, bytes + w * 8, sizeof(word_val));
        hash[0] ^= word_val;
        hash[0] *= UINT64_C(1099511628211);
        hash[1] ^= word_val;
        hash[1] *= UINT64_C(14029467366897019727);
    }
    for (size_t rem = words * 8; rem < length; ++rem) {
        hash[0] ^= bytes[rem];
        hash[0] *= UINT64_C(1099511628211);
        hash[1] ^= bytes[rem];
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

/* Returns 1 for a duplicate and 0 when the send may proceed (O(1) table lookup). */
static int prepare_send(const void *data, size_t length,
                        const c2t_clipboard_source_t *source,
                        sent_content_t *pending)
{
    pending->valid = 0;
    if (!deduplicate)
        return 0;

    uint64_t hash[2];
    content_hash(data, length, source, hash);

    telegram_lock();
    size_t slot = (size_t)(hash[0] & TELEGRAM_DEDUP_TABLE_MASK);
    for (size_t probe = 0; probe < 32; ++probe) {
        size_t idx = (slot + probe) & TELEGRAM_DEDUP_TABLE_MASK;
        if (!sent_contents_table[idx].valid)
            break;
        if (sent_contents_table[idx].length == length &&
            sent_contents_table[idx].hash[0] == hash[0] &&
            sent_contents_table[idx].hash[1] == hash[1]) {
            telegram_unlock();
            return 1;
        }
    }
    telegram_unlock();

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

    telegram_lock();
    /* Evict oldest entry if at capacity */
    if (sent_order_count >= TELEGRAM_DEDUPLICATION_CAPACITY) {
        uint32_t old_slot = sent_order[sent_order_head];
        sent_contents_table[old_slot].valid = 0;
        sent_order_head = (sent_order_head + 1) % TELEGRAM_DEDUPLICATION_CAPACITY;
        --sent_order_count;
    }

    size_t slot = (size_t)(pending->hash[0] & TELEGRAM_DEDUP_TABLE_MASK);
    size_t target_idx = slot;
    for (size_t probe = 0; probe < 32; ++probe) {
        size_t idx = (slot + probe) & TELEGRAM_DEDUP_TABLE_MASK;
        if (!sent_contents_table[idx].valid) {
            target_idx = idx;
            break;
        }
    }

    sent_contents_table[target_idx] = *pending;
    sent_contents_table[target_idx].valid = 1;
    size_t next_order_slot = (sent_order_head + sent_order_count) % TELEGRAM_DEDUPLICATION_CAPACITY;
    sent_order[next_order_slot] = (uint32_t)target_idx;
    ++sent_order_count;
    telegram_unlock();

    c2t_log_debug("telegram", "Content hash stored in O(1) table after successful delivery");
    return 1;
}

static void clear_sent_contents_locked(void)
{
    memset(sent_contents_table, 0, sizeof(sent_contents_table));
    sent_order_head = 0;
    sent_order_count = 0;
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
    if (field_count > 4 || !chat_id)
        return 0;

    size_t chat_len = strlen(chat_id);
    size_t max_body_length = 8 + chat_len * 3 + 1;
    for (size_t index = 0; index < field_count; ++index) {
        max_body_length += 1 + strlen(fields[index].name) + 1 + fields[index].length * 3 + 1;
    }

    char stack_body[4096];
    char *body = max_body_length <= sizeof(stack_body) ? stack_body : malloc(max_body_length);
    if (!body) {
        c2t_log_error("telegram", "Not enough memory for message body");
        return 0;
    }

    static const char hexadecimal[] = "0123456789ABCDEF";
    size_t offset = 0;

    memcpy(body + offset, "chat_id=", 8);
    offset += 8;
    for (size_t i = 0; i < chat_len; ++i) {
        unsigned char c = (unsigned char)chat_id[i];
        if (is_unreserved(c)) {
            body[offset++] = (char)c;
        } else {
            body[offset++] = '%';
            body[offset++] = hexadecimal[c >> 4];
            body[offset++] = hexadecimal[c & 0x0f];
        }
    }

    for (size_t index = 0; index < field_count; ++index) {
        body[offset++] = '&';
        size_t nlen = strlen(fields[index].name);
        memcpy(body + offset, fields[index].name, nlen);
        offset += nlen;
        body[offset++] = '=';
        const char *v = fields[index].value;
        size_t vlen = fields[index].length;
        for (size_t i = 0; i < vlen; ++i) {
            unsigned char c = (unsigned char)v[i];
            if (is_unreserved(c)) {
                body[offset++] = (char)c;
            } else {
                body[offset++] = '%';
                body[offset++] = hexadecimal[c >> 4];
                body[offset++] = hexadecimal[c & 0x0f];
            }
        }
    }
    body[offset] = '\0';

    int result = telegram_http_post(
        bot_token, method, "application/x-www-form-urlencoded",
        body, offset);

    if (body != stack_body)
        free(body);
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
                                trimmed_length <= 2048 ? trimmed : nullptr,
                                trimmed_length);
        }
    }

    char phone[17] = {};
    size_t phone_length;
    if (parse_phone(trimmed, trimmed_length, phone, &phone_length)) {
        static const char contact_name[] = "Clipboard";
        *recognized = 1;
        c2t_log_info("telegram", "Recognized phone number as contact");
        return send_contact(phone, phone_length, contact_name,
                            sizeof(contact_name) - 1, nullptr, 0);
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
    if (needle_length > length || needle_length == 0)
        return 0;

    const unsigned char *p = data;
    size_t remaining = length;
    unsigned char first = (unsigned char)needle[0];
    while (remaining >= needle_length) {
        const unsigned char *match = memchr(p, first, remaining - needle_length + 1);
        if (!match)
            return 0;
        if (memcmp(match, needle, needle_length) == 0)
            return 1;
        remaining -= (size_t)(match - p + 1);
        p = match + 1;
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
    char prefix_buf[2048];
    int prefix_length = source_length
        ? snprintf(prefix_buf, sizeof(prefix_buf), prefix_format, boundary, chat_id, boundary,
                   source_text, boundary, field, safe_filename, mime_type)
        : snprintf(prefix_buf, sizeof(prefix_buf), prefix_format, boundary, chat_id, boundary, field,
                   safe_filename, mime_type);

    char suffix_buf[64];
    int suffix_length = snprintf(suffix_buf, sizeof(suffix_buf), "\r\n--%s--\r\n", boundary);
    if (prefix_length < 0 || (size_t)prefix_length >= sizeof(prefix_buf) ||
        suffix_length < 0 || (size_t)suffix_length >= sizeof(suffix_buf))
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
    memcpy(body, prefix_buf, (size_t)prefix_length);
    if (length)
        memcpy(body + prefix_length, data, length);
    memcpy(body + prefix_length + length, suffix_buf, (size_t)suffix_length);
    body[body_length] = '\0';

    char content_type[96];
    snprintf(content_type, sizeof(content_type),
             "multipart/form-data; boundary=%s", boundary);
    int result = telegram_http_post(bot_token, method, content_type,
                                    body, body_length);
    free(body);
    return result;
}

static int send_encrypted_file(const void *encrypted_data, size_t length,
                               const unsigned char nonce[C2T_CRYPTO_NONCE_SIZE],
                               const char *mime_type,
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
        else if (mime_has_prefix(mime_type, "text/"))
            filename = "clipboard.txt";
        else
            filename = "clipboard.bin";
    }

    char safe_filename[256];
    sanitize_filename(filename, safe_filename);

    char boundary[48];
    char source_text[TELEGRAM_MAX_CAPTION_BYTES + 1];
    size_t source_length = format_source(source, source_text);
    unsigned int suffix_val = 0;
    do {
        snprintf(boundary, sizeof(boundary), "c2tBoundary%u", suffix_val++);
    } while (contains_bytes((const unsigned char *)source_text, source_length, boundary));

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
    char prefix_buf[2048];
    int prefix_length = source_length
        ? snprintf(prefix_buf, sizeof(prefix_buf), prefix_format, boundary, chat_id, boundary,
                   source_text, boundary, field, safe_filename, mime_type)
        : snprintf(prefix_buf, sizeof(prefix_buf), prefix_format, boundary, chat_id, boundary, field,
                   safe_filename, mime_type);

    char suffix_buf[64];
    int suffix_length = snprintf(suffix_buf, sizeof(suffix_buf), "\r\n--%s--\r\n", boundary);
    if (prefix_length < 0 || (size_t)prefix_length >= sizeof(prefix_buf) ||
        suffix_length < 0 || (size_t)suffix_length >= sizeof(suffix_buf))
        return 0;

    c2t_encrypted_stream_t enc_stream;
    c2t_encrypted_stream_init(&enc_stream, prefix_buf, (size_t)prefix_length,
                             encrypted_data, length, nonce,
                             suffix_buf, (size_t)suffix_length);

    c2t_stream_t stream;
    stream.read = c2t_encrypted_stream_read;
    stream.total_size = (size_t)prefix_length + length + (size_t)suffix_length;
    stream.user_data = &enc_stream;

    char content_type[96];
    snprintf(content_type, sizeof(content_type),
             "multipart/form-data; boundary=%s", boundary);

    return telegram_http_post_stream(bot_token, method, content_type, &stream);
}

int telegram_send_encrypted_data(const void *encrypted_data, size_t length,
                                 const unsigned char nonce[C2T_CRYPTO_NONCE_SIZE],
                                 const char *mime_type,
                                 const c2t_clipboard_source_t *source)
{
    if (!initialized || length == 0)
        return 1;
    if (!encrypted_data || !nonce || !mime_type)
        return 0;

    if (mime_has_prefix(mime_type, "text/") && length < TELEGRAM_MAX_CHARACTERS) {
        char text_buf[TELEGRAM_MAX_CHARACTERS + 1];
        c2t_secure_lock(text_buf, sizeof(text_buf));
        if (!c2t_crypto_decrypt(encrypted_data, length, nonce, text_buf)) {
            c2t_secure_unlock(text_buf, sizeof(text_buf));
            return 0;
        }
        text_buf[length] = '\0';
        int res = telegram_send(text_buf, length, source);
        c2t_secure_zero(text_buf, sizeof(text_buf));
        c2t_secure_unlock(text_buf, sizeof(text_buf));
        return res;
    }

    c2t_log_info("telegram", "Streaming encrypted content (%s, %llu bytes)",
                 mime_type, (unsigned long long)length);
    int result = send_encrypted_file(encrypted_data, length, nonce, mime_type, nullptr, 1, source);
    c2t_log_info("telegram", "Encrypted delivery %s", result ? "completed" : "failed");
    return result;
}

int telegram_send_encrypted_file(const void *encrypted_data, size_t length,
                                 const unsigned char nonce[C2T_CRYPTO_NONCE_SIZE],
                                 const char *mime_type,
                                 const char *filename,
                                 const c2t_clipboard_source_t *source)
{
    if (!initialized)
        return 1;
    if ((!encrypted_data && length != 0) || !nonce || !mime_type || !filename || !*filename)
        return 0;

    c2t_log_info("telegram", "Streaming encrypted file: name=%s, type=%s, size=%llu bytes",
                 filename, mime_type, (unsigned long long)length);
    int result = send_encrypted_file(encrypted_data, length, nonce, mime_type, filename, 0, source);
    c2t_log_info("telegram", "Encrypted file delivery %s", result ? "completed" : "failed");
    return result;
}

int telegram_init(void)
{
    telegram_lock();
    const c2t_config_t *config = c2t_config_get();
    deduplicate = config->telegram_deduplicate;
    if (!config->telegram_enabled) {
        telegram_unlock();
        c2t_log_info("telegram", "Telegram integration is disabled");
        return 1;
    }

    c2t_log_info("telegram", "Telegram integration enabled; deduplication=%s",
                 deduplicate ? "enabled" : "disabled");

    bot_token = config->telegram_bot_token;
    chat_id = config->telegram_chat_id;
    if (!bot_token || !token_is_valid(bot_token)) {
        telegram_unlock();
        c2t_log_error("telegram", "TELEGRAM_BOT_TOKEN is missing or invalid");
        return 0;
    }
    if (!chat_id || !chat_is_valid(chat_id)) {
        telegram_unlock();
        c2t_log_info("telegram", "TELEGRAM_CHAT_ID is missing; entering auto-pairing mode...");
        char paired_chat_id[128] = {};
        if (telegram_pair(bot_token, nullptr, paired_chat_id, sizeof(paired_chat_id), 60)) {
            telegram_lock();
            chat_id = config->telegram_chat_id;
        } else {
            c2t_log_error("telegram", "TELEGRAM_CHAT_ID is missing and auto-pairing failed");
            return 0;
        }
    }
    c2t_log_debug("telegram", "Initializing HTTPS transport");
    if (!initialized)
        initialized = telegram_http_init();
    int is_init = initialized;
    telegram_unlock();
    if (is_init)
        c2t_log_info("telegram", "Telegram transport initialized");
    return is_init;
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
            &pending, send_file(data, length, mime_type, nullptr, 1, source));
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

int telegram_send_html(const char *html_text)
{
    if (!initialized || !html_text || !*html_text || !chat_id || !bot_token)
        return 0;

    form_field_t fields[2] = {
        {"text", html_text, strlen(html_text)},
        {"parse_mode", "HTML", 4}
    };
    return send_fields("sendMessage", fields, 2);
}

static int parse_json_field_in_range(const char *start, const char *end, const char *key, char *output, size_t capacity)
{
    if (!start || !end || start >= end || !key || !output || capacity == 0) return 0;
    output[0] = '\0';

    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    size_t pat_len = strlen(pattern);

    const char *pos = start;
    while (pos + pat_len <= end) {
        if (memcmp(pos, pattern, pat_len) == 0) {
            pos += pat_len;
            while (pos < end && (*pos == ' ' || *pos == ':')) pos++;
            if (pos < end && *pos == '"') {
                pos++;
                size_t len = 0;
                while (pos < end && *pos != '"' && len + 1 < capacity) {
                    if (*pos == '\\' && pos + 1 < end) {
                        pos++;
                    }
                    output[len++] = *pos++;
                }
                output[len] = '\0';
                return len > 0;
            }
            break;
        }
        pos++;
    }
    return 0;
}

static int parse_json_chat_id_in_range(const char *start, const char *end, char *output, size_t capacity)
{
    if (!start || !end || start >= end || !output || capacity == 0) return 0;
    output[0] = '\0';

    const char *chat_pos = nullptr;
    char chat_pattern[] = "\"chat\"";
    for (const char *p = start; p + 6 <= end; p++) {
        if (memcmp(p, chat_pattern, 6) == 0) {
            chat_pos = p;
            break;
        }
    }
    if (!chat_pos) {
        char from_pattern[] = "\"from\"";
        for (const char *p = start; p + 6 <= end; p++) {
            if (memcmp(p, from_pattern, 6) == 0) {
                chat_pos = p;
                break;
            }
        }
    }
    if (!chat_pos) chat_pos = start;

    const char *id_pos = nullptr;
    char id_pattern[] = "\"id\"";
    for (const char *p = chat_pos; p + 4 <= end; p++) {
        if (memcmp(p, id_pattern, 4) == 0) {
            id_pos = p + 4;
            break;
        }
    }
    if (!id_pos) return 0;

    while (id_pos < end && (*id_pos == ' ' || *id_pos == ':')) id_pos++;
    if (id_pos < end && *id_pos == '"') id_pos++;

    size_t len = 0;
    while (id_pos < end && ((*id_pos >= '0' && *id_pos <= '9') || *id_pos == '-') && len + 1 < capacity) {
        output[len++] = *id_pos++;
    }
    output[len] = '\0';
    return len > 0;
}

int telegram_get_bot_username(const char *token, char *username_out, size_t capacity)
{
    if (!token || !username_out || capacity == 0) return 0;
    username_out[0] = '\0';

    int temp_http = 0;
    if (!initialized) {
        if (!telegram_http_init()) return 0;
        temp_http = 1;
    }

    char response[2048] = {};
    int res = telegram_http_get(token, "getMe", response, sizeof(response));
    if (temp_http && !initialized) {
        telegram_http_cleanup();
    }

    if (!res) return 0;
    return parse_json_field_in_range(response, response + strlen(response), "username", username_out, capacity);
}

int telegram_poll_updates_callback(const char *token, int64_t *offset, int timeout_seconds,
                                   telegram_update_callback_t callback, void *user_data)
{
    if (!token) return 0;

    int temp_http = 0;
    if (!initialized) {
        if (!telegram_http_init()) return 0;
        temp_http = 1;
    }

    char query[128];
    if (offset && *offset > 0) {
        snprintf(query, sizeof(query), "getUpdates?offset=%lld&timeout=%d", (long long)*offset, timeout_seconds);
    } else {
        snprintf(query, sizeof(query), "getUpdates?timeout=%d", timeout_seconds);
    }

    char response[32768] = {};
    int res = telegram_http_get(token, query, response, sizeof(response));
    if (temp_http && !initialized) {
        telegram_http_cleanup();
    }

    if (!res || !strstr(response, "\"ok\":true")) {
        return 0;
    }

    const char *result_pos = strstr(response, "\"result\"");
    if (!result_pos) return 0;

    int updates_found = 0;
    int64_t max_update_id = -1;
    const char *curr = strstr(result_pos, "\"update_id\"");

    while (curr) {
        const char *id_ptr = curr + 11;
        while (*id_ptr == ' ' || *id_ptr == ':') id_ptr++;
        int64_t uid = strtoll(id_ptr, nullptr, 10);
        if (uid > max_update_id) max_update_id = uid;

        const char *next = strstr(curr + 11, "\"update_id\"");
        const char *block_end = next ? next : (response + strlen(response));

        char item_chat_id[128] = {};
        char item_username[128] = {};
        char item_text[512] = {};

        parse_json_chat_id_in_range(curr, block_end, item_chat_id, sizeof(item_chat_id));
        parse_json_field_in_range(curr, block_end, "username", item_username, sizeof(item_username));
        parse_json_field_in_range(curr, block_end, "text", item_text, sizeof(item_text));

        if (callback) {
            callback(uid, item_chat_id, item_username, item_text, user_data);
        }
        updates_found++;

        curr = next;
    }

    if (max_update_id >= 0 && offset) {
        *offset = max_update_id + 1;
    }

    return updates_found;
}

typedef struct {
    char *chat_id_out;
    size_t chat_id_capacity;
    char *username_out;
    size_t username_capacity;
    char *text_out;
    size_t text_capacity;
    int found;
} single_poll_ctx_t;

static void single_poll_callback([[maybe_unused]] int64_t update_id, const char *cb_chat_id,
                                 const char *username, const char *text,
                                 void *user_data)
{
    single_poll_ctx_t *ctx = (single_poll_ctx_t *)user_data;
    if (ctx->found) return;

    if (ctx->chat_id_out && cb_chat_id && *cb_chat_id) {
        snprintf(ctx->chat_id_out, ctx->chat_id_capacity, "%s", cb_chat_id);
    }
    if (ctx->username_out && username && *username) {
        snprintf(ctx->username_out, ctx->username_capacity, "%s", username);
    }
    if (ctx->text_out && text && *text) {
        snprintf(ctx->text_out, ctx->text_capacity, "%s", text);
    }
    ctx->found = 1;
}

int telegram_poll_updates_timeout(const char *token, int64_t *offset, int timeout_seconds,
                                 char *chat_id_out, size_t chat_id_capacity,
                                 char *username_out, size_t username_capacity,
                                 char *text_out, size_t text_capacity)
{
    single_poll_ctx_t ctx = {
        chat_id_out, chat_id_capacity,
        username_out, username_capacity,
        text_out, text_capacity,
        0
    };
    telegram_poll_updates_callback(token, offset, timeout_seconds, single_poll_callback, &ctx);
    return ctx.found;
}

int telegram_poll_updates(const char *token, int64_t *offset,
                         char *chat_id_out, size_t chat_id_capacity,
                         char *username_out, size_t username_capacity,
                         char *text_out, size_t text_capacity)
{
    return telegram_poll_updates_timeout(token, offset, 2,
                                         chat_id_out, chat_id_capacity,
                                         username_out, username_capacity,
                                         text_out, text_capacity);
}

int telegram_pair(const char *token, const char *expected_code,
                  char *chat_id_out, size_t capacity, int timeout_seconds)
{
    if (!token || !token_is_valid(token)) {
        c2t_log_error("pairing", "[PAIRING] Invalid or missing Telegram Bot Token");
        return 0;
    }

    int was_initialized = initialized;
    if (!initialized) {
        if (!telegram_http_init()) {
            c2t_log_error("pairing", "[PAIRING] Failed to initialize HTTPS transport");
            return 0;
        }
        initialized = 1;
    }

    char bot_username[128] = {};
    if (!telegram_get_bot_username(token, bot_username, sizeof(bot_username))) {
        c2t_log_error("pairing", "[PAIRING] Could not fetch Telegram Bot profile via getMe");
        if (!was_initialized) telegram_http_cleanup();
        return 0;
    }

    char code_buf[64] = {};
    if (expected_code && *expected_code) {
        snprintf(code_buf, sizeof(code_buf), "%s", expected_code);
    } else {
        srand((unsigned int)time(nullptr));
        snprintf(code_buf, sizeof(code_buf), "c2t_%04x%04x", rand() % 0xffff, rand() % 0xffff);
    }

    printf("\n======================================================================\n");
    printf("[PAIRING] Telegram Bot: @%s\n", bot_username);
    printf("[PAIRING] Please open Telegram and visit:\n");
    printf("[PAIRING]   https://t.me/%s?start=%s\n", bot_username, code_buf);
    printf("[PAIRING] Or send '/start %s' to @%s\n", code_buf, bot_username);
    printf("[PAIRING] Waiting for pairing message (timeout: %ds)...\n", timeout_seconds);
    printf("======================================================================\n\n");
    fflush(stdout);

    int64_t offset = 0;
    int elapsed = 0;
    char found_chat_id[128] = {};
    char found_username[128] = {};
    char found_text[256] = {};

    while (elapsed < timeout_seconds) {
        if (telegram_poll_updates(token, &offset, found_chat_id, sizeof(found_chat_id),
                                  found_username, sizeof(found_username),
                                  found_text, sizeof(found_text))) {
            if (*found_chat_id) {
                if (chat_id_out && capacity > 0) {
                    snprintf(chat_id_out, capacity, "%s", found_chat_id);
                }

                c2t_config_set_chat_id(found_chat_id);
                bot_token = token;
                chat_id = c2t_config_get()->telegram_chat_id;

                /* Send confirmation message to Telegram chat */
                char confirm_msg[512] = {};
                snprintf(confirm_msg, sizeof(confirm_msg),
                         "✅ c2t paired successfully!\nDevice connected to %s (Chat ID: %s).",
                         found_username[0] ? found_username : "user", found_chat_id);
                send_form(confirm_msg, strlen(confirm_msg));

                printf("[PAIRING] Successfully paired with @%s (Chat ID: %s)\n",
                       found_username[0] ? found_username : "user", found_chat_id);
                c2t_log_info("pairing", "[PAIRING] Paired successfully with chat_id=%s (@%s)",
                             found_chat_id, found_username);
                return 1;
            }
        }
#ifndef _WIN32
        sleep(2);
#else
        Sleep(2000);
#endif
        elapsed += 2;
    }

    printf("[PAIRING] Pairing timed out after %d seconds.\n", timeout_seconds);
    c2t_log_error("pairing", "[PAIRING] Pairing timed out");
    if (!was_initialized) telegram_http_cleanup();
    return 0;
}

void telegram_cleanup(void)
{
    c2t_log_debug("telegram", "Cleaning up Telegram state");
    telegram_lock();
    if (initialized)
        telegram_http_cleanup();
    initialized = 0;
    deduplicate = 0;
    bot_token = nullptr;
    chat_id = nullptr;
    clear_sent_contents_locked();
    telegram_unlock();
}
