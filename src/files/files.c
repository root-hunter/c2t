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

[[nodiscard]] static int ascii_equal_nocase(const char *left, const char *right,
                                              size_t length)
{
    for (size_t index = 0; index < length; ++index) {
        if (tolower((unsigned char)left[index]) !=
            tolower((unsigned char)right[index]))
            return 0;
    }
    return 1;
}

[[nodiscard]] static int mime_is(const char *mime_type, const char *expected)
{
    size_t length = strlen(expected);
    return strlen(mime_type) >= length &&
           ascii_equal_nocase(mime_type, expected, length) &&
           (mime_type[length] == '\0' || mime_type[length] == ';');
}

[[nodiscard]] static int hexadecimal_value(unsigned char character)
{
    if (character >= '0' && character <= '9')
        return character - '0';
    character = (unsigned char)tolower(character);
    return character >= 'a' && character <= 'f' ? character - 'a' + 10 : -1;
}

[[nodiscard]] static int decode_path(char *path, int uri)
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

[[nodiscard]] static char *clipboard_path(const void *data, size_t length,
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
        return nullptr;

    int uri_list = mime_is(mime_type, "text/uri-list");
    if (memchr(text, '\r', length) || memchr(text, '\n', length))
        return nullptr;

    if (length >= 2 && ((text[0] == '"' && text[length - 1] == '"') ||
                        (text[0] == '\'' && text[length - 1] == '\''))) {
        ++text;
        length -= 2;
    }

    char *path = malloc(length + 1);
    if (!path)
        return nullptr;
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
            return nullptr;
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
        return nullptr;
    }
    return path;
}

[[nodiscard]] static const char *filename_from_path(const char *path)
{
    const char *filename = path;
    for (const char *cursor = path; *cursor; ++cursor) {
        if (*cursor == '/' || *cursor == '\\')
            filename = cursor + 1;
    }
    return *filename ? filename : "clipboard.bin";
}

[[nodiscard]] static const char *mime_from_filename(const char *filename)
{
    const char *extension = strrchr(filename, '.');
    if (!extension)
        return "application/octet-stream";
    ++extension;
    char ext[8] = {};
    for (size_t i = 0; i < 7 && extension[i]; ++i)
        ext[i] = (char)tolower((unsigned char)extension[i]);

    /* O(1) branch-predictable matching on normalized extension */
    if (ext[0] == 't' && ext[1] == 'x' && ext[2] == 't' && ext[3] == '\0') return "text/plain";
    if (ext[0] == 'p' && ext[1] == 'n' && ext[2] == 'g' && ext[3] == '\0') return "image/png";
    if (ext[0] == 'j' && ext[1] == 'p' && ext[2] == 'g' && ext[3] == '\0') return "image/jpeg";
    if (ext[0] == 'j' && ext[1] == 'p' && ext[2] == 'e' && ext[3] == 'g' && ext[4] == '\0') return "image/jpeg";
    if (ext[0] == 'p' && ext[1] == 'd' && ext[2] == 'f' && ext[3] == '\0') return "application/pdf";
    if (ext[0] == 'z' && ext[1] == 'i' && ext[2] == 'p' && ext[3] == '\0') return "application/zip";
    if (ext[0] == 'j' && ext[1] == 's' && ext[2] == 'o' && ext[3] == 'n' && ext[4] == '\0') return "application/json";
    if (ext[0] == 'c' && ext[1] == 's' && ext[2] == 'v' && ext[3] == '\0') return "text/csv";
    if (ext[0] == 'w' && ext[1] == 'e' && ext[2] == 'b' && ext[3] == 'p' && ext[4] == '\0') return "image/webp";
    if (ext[0] == 'g' && ext[1] == 'i' && ext[2] == 'f' && ext[3] == '\0') return "image/gif";
    if (ext[0] == 'b' && ext[1] == 'm' && ext[2] == 'p' && ext[3] == '\0') return "image/bmp";
    if (ext[0] == 'm' && ext[1] == 'p' && ext[2] == '4' && ext[3] == '\0') return "video/mp4";
    if (ext[0] == 'm' && ext[1] == 'p' && ext[2] == '3' && ext[3] == '\0') return "audio/mpeg";
    if (ext[0] == 'm' && ext[1] == '4' && ext[2] == 'a' && ext[3] == '\0') return "audio/mp4";
    if (ext[0] == 'm' && ext[1] == 'o' && ext[2] == 'v' && ext[3] == '\0') return "video/quicktime";
    if (ext[0] == 'x' && ext[1] == 'm' && ext[2] == 'l' && ext[3] == '\0') return "application/xml";

    return "application/octet-stream";
}

#include <errno.h>

static void append_escaped_html(char *output, size_t *offset, size_t capacity,
                                const char *input)
{
    if (!input || !output || !offset)
        return;
    while (*input && *offset + 6 < capacity) {
        if (*input == '&') {
            memcpy(output + *offset, "&amp;", 5);
            *offset += 5;
        } else if (*input == '<') {
            memcpy(output + *offset, "&lt;", 4);
            *offset += 4;
        } else if (*input == '>') {
            memcpy(output + *offset, "&gt;", 4);
            *offset += 4;
        } else {
            output[(*offset)++] = *input;
        }
        input++;
    }
    output[*offset] = '\0';
}

static int send_file_error_telegram(const char *path, const char *error_message,
                                     const c2t_clipboard_source_t *source)
{
    char html[2048];
    size_t offset = 0;

    static const char header[] = "⚠️ <b>File Delivery Failed</b>\n<b>Path:</b> <code>";
    memcpy(html, header, sizeof(header) - 1);
    offset = sizeof(header) - 1;

    append_escaped_html(html, &offset, sizeof(html), path);

    static const char mid[] = "</code>\n<b>Error:</b> ";
    if (offset + sizeof(mid) - 1 < sizeof(html)) {
        memcpy(html + offset, mid, sizeof(mid) - 1);
        offset += sizeof(mid) - 1;
    }

    append_escaped_html(html, &offset, sizeof(html), error_message);

    const c2t_config_t *config = c2t_config_get();
    if (config->telegram_send_window_info && source &&
        (source->application[0] || source->title[0] || source->process_id)) {
        static const char src_hdr[] = "\n<b>Source:</b> <i>";
        if (offset + sizeof(src_hdr) - 1 < sizeof(html)) {
            memcpy(html + offset, src_hdr, sizeof(src_hdr) - 1);
            offset += sizeof(src_hdr) - 1;
        }

        char source_desc[512] = {};
        size_t s_off = 0;
        if (source->application[0]) {
            s_off += snprintf(source_desc + s_off, sizeof(source_desc) - s_off,
                              "%s", source->application);
        }
        if (source->title[0] && s_off + 3 < sizeof(source_desc)) {
            s_off += snprintf(source_desc + s_off, sizeof(source_desc) - s_off,
                              "%s%s", s_off > 0 ? " | " : "", source->title);
        }
        if (source->process_id && s_off + 16 < sizeof(source_desc)) {
            snprintf(source_desc + s_off, sizeof(source_desc) - s_off,
                     "%sPID %lu", s_off > 0 ? " | " : "",
                     (unsigned long)source->process_id);
        }

        append_escaped_html(html, &offset, sizeof(html), source_desc);

        static const char src_ftr[] = "</i>";
        if (offset + sizeof(src_ftr) - 1 < sizeof(html)) {
            memcpy(html + offset, src_ftr, sizeof(src_ftr) - 1);
            offset += sizeof(src_ftr) - 1;
        }
    }

    html[offset] = '\0';
    return telegram_send_html(html);
}

enum {
    READ_FILE_OK = 0,
    READ_FILE_NOT_FOUND = 1,
    READ_FILE_NOT_REGULAR = 2,
    READ_FILE_ERROR = -1
};

#ifdef _WIN32
[[nodiscard]] static wchar_t *utf8_path(const char *path)
{
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
                                     nullptr, 0);
    wchar_t *wide = length > 0 ? malloc((size_t)length * sizeof(*wide)) : nullptr;
    if (!wide || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
                                      wide, length)) {
        free(wide);
        return nullptr;
    }
    return wide;
}
#endif

[[nodiscard]] static int read_file(const char *path, const c2t_config_t *config,
                                  unsigned char **data, size_t *length,
                                  char *error_out, size_t error_capacity)
{
    c2t_stat_t status;
    FILE *file;
#ifdef _WIN32
    wchar_t *wide = utf8_path(path);
    if (!wide) {
        if (error_out && error_capacity > 0)
            snprintf(error_out, error_capacity, "Invalid UTF-8 file path");
        return READ_FILE_NOT_FOUND;
    }
    int stat_result = _wstat64(wide, &status);
    int saved_errno = errno;
    file = stat_result == 0 ? _wfopen(wide, L"rb") : nullptr;
    int open_errno = errno;
    free(wide);
    if (stat_result != 0) {
        if (error_out && error_capacity > 0)
            snprintf(error_out, error_capacity,
                     "File does not exist or cannot be accessed: %s",
                     strerror(saved_errno));
        return READ_FILE_NOT_FOUND;
    }
    if ((status.st_mode & _S_IFMT) != _S_IFREG) {
        if (file)
            fclose(file);
        if (error_out && error_capacity > 0) {
            if ((status.st_mode & _S_IFMT) == _S_IFDIR)
                snprintf(error_out, error_capacity,
                         "Path is a directory, not a regular file");
            else
                snprintf(error_out, error_capacity, "Path is not a regular file");
        }
        return READ_FILE_NOT_REGULAR;
    }
#else
    int stat_result = stat(path, &status);
    int saved_errno = errno;
    file = stat_result == 0 ? fopen(path, "rb") : nullptr;
    int open_errno = errno;
    if (stat_result != 0) {
        if (error_out && error_capacity > 0)
            snprintf(error_out, error_capacity,
                     "File does not exist or cannot be accessed: %s",
                     strerror(saved_errno));
        return READ_FILE_NOT_FOUND;
    }
    if (!S_ISREG(status.st_mode)) {
        if (file)
            fclose(file);
        if (error_out && error_capacity > 0) {
            if (S_ISDIR(status.st_mode))
                snprintf(error_out, error_capacity,
                         "Path is a directory, not a regular file");
            else
                snprintf(error_out, error_capacity, "Path is not a regular file");
        }
        return READ_FILE_NOT_REGULAR;
    }
#endif
    if (status.st_size < 0 || (uintmax_t)status.st_size >
        (uintmax_t)config->telegram_max_file_bytes) {
        if (file)
            fclose(file);
        c2t_log_error("files", "File '%s' exceeds configured limit of %llu bytes (size: %llu bytes)",
                      path,
                      (unsigned long long)config->telegram_max_file_bytes,
                      (unsigned long long)status.st_size);
        if (error_out && error_capacity > 0) {
            snprintf(error_out, error_capacity,
                     "File size (%.2f MB / %llu bytes) exceeds configured limit (%.2f MB / %llu bytes)",
                     (double)status.st_size / (1024.0 * 1024.0),
                     (unsigned long long)status.st_size,
                     (double)config->telegram_max_file_bytes / (1024.0 * 1024.0),
                     (unsigned long long)config->telegram_max_file_bytes);
        }
        return READ_FILE_ERROR;
    }
    if (!file) {
        c2t_log_error("files", "File '%s' cannot be opened for reading: %s",
                       path, strerror(open_errno));
        if (error_out && error_capacity > 0)
            snprintf(error_out, error_capacity,
                     "Cannot open file for reading: %s", strerror(open_errno));
        return READ_FILE_ERROR;
    }

    *length = (size_t)status.st_size;
    *data = malloc(*length ? *length : 1);
    if (!*data) {
        fclose(file);
        c2t_log_error("files", "Not enough memory to read clipboard file '%s'", path);
        if (error_out && error_capacity > 0)
            snprintf(error_out, error_capacity,
                     "Memory allocation failed while reading file");
        return READ_FILE_ERROR;
    }
    size_t bytes_read = fread(*data, 1, *length, file);
    int read_errno = errno;
    int close_result = fclose(file);
    if (bytes_read != *length || close_result != 0) {
        free(*data);
        *data = nullptr;
        c2t_log_error("files", "Unable to read the complete clipboard file '%s': %s",
                      path, strerror(read_errno));
        if (error_out && error_capacity > 0)
            snprintf(error_out, error_capacity,
                     "Failed to read complete file content: %s",
                     strerror(read_errno));
        return READ_FILE_ERROR;
    }
    return READ_FILE_OK;
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

    char error_msg[512] = {};
    unsigned char *contents = nullptr;
    size_t file_length = 0;
    int read_result = read_file(path, config, &contents, &file_length,
                                error_msg, sizeof(error_msg));

    if (read_result == READ_FILE_NOT_FOUND) {
        if (!explicit_uri) {
            free(path);
            return C2T_FILE_NOT_HANDLED;
        }
        int sent = send_file_error_telegram(path, error_msg, source);
        free(path);
        return sent ? C2T_FILE_SENT : C2T_FILE_ERROR;
    }

    if (read_result == READ_FILE_NOT_REGULAR) {
        if (!explicit_uri) {
            free(path);
            return C2T_FILE_NOT_HANDLED;
        }
        int sent = send_file_error_telegram(path, error_msg, source);
        free(path);
        return sent ? C2T_FILE_SENT : C2T_FILE_ERROR;
    }

    if (read_result == READ_FILE_ERROR) {
        int sent = send_file_error_telegram(path, error_msg, source);
        free(path);
        return sent ? C2T_FILE_SENT : C2T_FILE_ERROR;
    }

    const char *filename = filename_from_path(path);
    const char *mime = mime_from_filename(filename);
    c2t_log_info("files", "Recognized clipboard file: name=%s, type=%s, "
                 "size=%llu bytes", filename, mime,
                 (unsigned long long)file_length);
    int result = telegram_send_file(contents, file_length, mime, filename,
                                    source);
    if (!result) {
        send_file_error_telegram(path, "Failed to upload file to Telegram",
                                 source);
    }
    free(contents);
    free(path);
    return result ? C2T_FILE_SENT : C2T_FILE_ERROR;
}
