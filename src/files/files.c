#include "files.h"
#include "../config/config.h"
#include "../logging/logging.h"
#include "../telegram/telegram.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sys/stat.h>
typedef struct _stat64 c2t_stat_t;
#else
#include <sys/stat.h>
typedef struct stat c2t_stat_t;
#endif

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

static int mime_is(const char *mime_type, const char *expected)
{
    size_t length = strlen(expected);
    return strlen(mime_type) >= length &&
           ascii_equal_nocase(mime_type, expected, length) &&
           (mime_type[length] == '\0' || mime_type[length] == ';');
}

static int hexadecimal_value(unsigned char character)
{
    if (character >= '0' && character <= '9')
        return character - '0';
    character = (unsigned char)tolower(character);
    return character >= 'a' && character <= 'f' ? character - 'a' + 10 : -1;
}

static int decode_path(char *path, int uri)
{
    char *input = path;
    char *output = path;
    while (*input) {
        if (uri && *input == '%') {
            int high = hexadecimal_value((unsigned char)input[1]);
            int low = input[1] ? hexadecimal_value((unsigned char)input[2]) : -1;
            if (high < 0 || low < 0)
                return 0;
            unsigned char decoded = (unsigned char)((high << 4) | low);
            if (decoded == 0)
                return 0;
            *output++ = (char)decoded;
            input += 3;
        } else {
            *output++ = *input++;
        }
    }
    *output = '\0';
    return 1;
}

static char *clipboard_path(const void *data, size_t length,
                            const char *mime_type, int *explicit_uri)
{
    const char *text = data;
    while (length > 0 && isspace((unsigned char)*text)) {
        ++text;
        --length;
    }
    while (length > 0 && isspace((unsigned char)text[length - 1]))
        --length;
    if (length == 0 || memchr(text, '\0', length))
        return NULL;

    int uri_list = mime_is(mime_type, "text/uri-list");
    if (memchr(text, '\r', length) || memchr(text, '\n', length))
        return NULL;

    if (length >= 2 && ((text[0] == '"' && text[length - 1] == '"') ||
                        (text[0] == '\'' && text[length - 1] == '\''))) {
        ++text;
        length -= 2;
    }

    char *path = malloc(length + 1);
    if (!path)
        return NULL;
    memcpy(path, text, length);
    path[length] = '\0';

    int uri = length >= 7 && ascii_equal_nocase(path, "file://", 7);
    *explicit_uri = uri || uri_list;
    if (uri) {
        char *uri_path = path + 7;
        size_t uri_path_length = strlen(uri_path);
        if (uri_path_length >= 10 &&
            ascii_equal_nocase(uri_path, "localhost/", 10))
            uri_path += 9;
        else if (*uri_path != '/') {
            free(path);
            return NULL;
        }
        memmove(path, uri_path, strlen(uri_path) + 1);
#ifdef _WIN32
        if (strlen(path) >= 3 && path[0] == '/' &&
            isalpha((unsigned char)path[1]) &&
            path[2] == ':')
            memmove(path, path + 1, strlen(path));
#endif
    }
    if (!decode_path(path, uri)) {
        free(path);
        return NULL;
    }
    return path;
}

static const char *filename_from_path(const char *path)
{
    const char *filename = path;
    for (const char *cursor = path; *cursor; ++cursor) {
        if (*cursor == '/' || *cursor == '\\')
            filename = cursor + 1;
    }
    return *filename ? filename : "clipboard.bin";
}

static const char *mime_from_filename(const char *filename)
{
    const char *extension = strrchr(filename, '.');
    if (!extension)
        return "application/octet-stream";
    ++extension;
    struct mime_entry {
        const char *extension;
        const char *mime_type;
    };
    static const struct mime_entry types[] = {
        {"txt", "text/plain"}, {"csv", "text/csv"},
        {"json", "application/json"}, {"xml", "application/xml"},
        {"pdf", "application/pdf"}, {"zip", "application/zip"},
        {"png", "image/png"}, {"jpg", "image/jpeg"},
        {"jpeg", "image/jpeg"}, {"gif", "image/gif"},
        {"webp", "image/webp"}, {"bmp", "image/bmp"},
        {"mp3", "audio/mpeg"}, {"m4a", "audio/mp4"},
        {"mp4", "video/mp4"}, {"mov", "video/quicktime"}
    };
    for (size_t index = 0; index < sizeof(types) / sizeof(types[0]); ++index) {
        size_t expected_length = strlen(types[index].extension);
        if (strlen(extension) == expected_length &&
            ascii_equal_nocase(extension, types[index].extension,
                               expected_length))
            return types[index].mime_type;
    }
    return "application/octet-stream";
}

#ifdef _WIN32
static wchar_t *utf8_path(const char *path)
{
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
                                     NULL, 0);
    wchar_t *wide = length > 0 ? malloc((size_t)length * sizeof(*wide)) : NULL;
    if (!wide || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
                                      wide, length)) {
        free(wide);
        return NULL;
    }
    return wide;
}
#endif

static int read_file(const char *path, const c2t_config_t *config,
                     unsigned char **data, size_t *length)
{
    c2t_stat_t status;
    FILE *file;
#ifdef _WIN32
    wchar_t *wide = utf8_path(path);
    if (!wide)
        return C2T_FILE_NOT_HANDLED;
    int stat_result = _wstat64(wide, &status);
    file = stat_result == 0 ? _wfopen(wide, L"rb") : NULL;
    free(wide);
    if (stat_result != 0)
        return C2T_FILE_NOT_HANDLED;
    if ((status.st_mode & _S_IFMT) != _S_IFREG) {
#else
    int stat_result = stat(path, &status);
    file = stat_result == 0 ? fopen(path, "rb") : NULL;
    if (stat_result != 0)
        return C2T_FILE_NOT_HANDLED;
    if (!S_ISREG(status.st_mode)) {
#endif
        if (file)
            fclose(file);
        return C2T_FILE_NOT_HANDLED;
    }
    if (status.st_size < 0 || (uintmax_t)status.st_size >
        (uintmax_t)config->telegram_max_file_bytes) {
        if (file)
            fclose(file);
        c2t_log_error("files", "File exceeds configured limit of %llu bytes",
                      (unsigned long long)config->telegram_max_file_bytes);
        return C2T_FILE_ERROR;
    }
    if (!file) {
        c2t_log_error("files", "Recognized file cannot be opened for reading");
        return C2T_FILE_ERROR;
    }

    *length = (size_t)status.st_size;
    *data = malloc(*length ? *length : 1);
    if (!*data) {
        fclose(file);
        c2t_log_error("files", "Not enough memory to read clipboard file");
        return C2T_FILE_ERROR;
    }
    size_t bytes_read = fread(*data, 1, *length, file);
    int close_result = fclose(file);
    if (bytes_read != *length || close_result != 0) {
        free(*data);
        *data = NULL;
        c2t_log_error("files", "Unable to read the complete clipboard file");
        return C2T_FILE_ERROR;
    }
    return C2T_FILE_SENT;
}

int c2t_file_try_clipboard_path(const void *data, size_t length,
                                const char *mime_type,
                                const c2t_clipboard_source_t *source)
{
    const c2t_config_t *config = c2t_config_get();
    if (!config->telegram_send_files || !data || !mime_type ||
        strncmp(mime_type, "text/", 5) != 0)
        return C2T_FILE_NOT_HANDLED;

    int explicit_uri = 0;
    char *path = clipboard_path(data, length, mime_type, &explicit_uri);
    if (!path)
        return C2T_FILE_NOT_HANDLED;

    unsigned char *contents = NULL;
    size_t file_length = 0;
    int read_result = read_file(path, config, &contents, &file_length);
    if (read_result == C2T_FILE_NOT_HANDLED) {
        free(path);
        return explicit_uri ? C2T_FILE_ERROR : C2T_FILE_NOT_HANDLED;
    }
    if (read_result == C2T_FILE_ERROR) {
        free(path);
        return C2T_FILE_ERROR;
    }

    const char *filename = filename_from_path(path);
    const char *mime = mime_from_filename(filename);
    c2t_log_info("files", "Recognized clipboard file: name=%s, type=%s, "
                 "size=%llu bytes", filename, mime,
                 (unsigned long long)file_length);
    int result = telegram_send_file(contents, file_length, mime, filename,
                                    source);
    free(contents);
    free(path);
    return result ? C2T_FILE_SENT : C2T_FILE_ERROR;
}
