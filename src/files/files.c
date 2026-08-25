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
#include "../runtime/environment.h"
#include "../telegram/telegram.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <sys/stat.h>
#include "../win32/win32_api.h"

#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#ifndef strncasecmp
#define strncasecmp _strnicmp
#endif

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
static HANDLE c2t_CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess,
                              DWORD dwShareMode,
                              LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                              DWORD dwCreationDisposition,
                              DWORD dwFlagsAndAttributes,
                              HANDLE hTemplateFile) {
  c2t_win32_api_init();
  if (g_c2t_win32.CreateFileW)
    return g_c2t_win32.CreateFileW(
        lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
        dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
  return INVALID_HANDLE_VALUE;
}
static DWORD c2t_GetFileAttributesW(LPCWSTR lpFileName) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetFileAttributesW)
    return g_c2t_win32.GetFileAttributesW(lpFileName);
  return INVALID_FILE_ATTRIBUTES;
}
static DWORD c2t_GetFullPathNameW(LPCWSTR lpFileName, DWORD nBufferLength,
                                  LPWSTR lpBuffer, LPWSTR *lpFilePart) {
  c2t_win32_api_init();
  return g_c2t_win32.GetFullPathNameW
             ? g_c2t_win32.GetFullPathNameW(lpFileName, nBufferLength,
                                            lpBuffer, lpFilePart)
             : 0;
}
static DWORD c2t_GetLogicalDrives(VOID) {
  c2t_win32_api_init();
  return g_c2t_win32.GetLogicalDrives ? g_c2t_win32.GetLogicalDrives() : 0;
}
static UINT c2t_GetDriveTypeW(LPCWSTR lpRootPathName) {
  c2t_win32_api_init();
  return g_c2t_win32.GetDriveTypeW
             ? g_c2t_win32.GetDriveTypeW(lpRootPathName)
             : DRIVE_UNKNOWN;
}
static BOOL c2t_GetDiskFreeSpaceExW(
    LPCWSTR lpDirectoryName, PULARGE_INTEGER lpFreeBytesAvailableToCaller,
    PULARGE_INTEGER lpTotalNumberOfBytes,
    PULARGE_INTEGER lpTotalNumberOfFreeBytes) {
  c2t_win32_api_init();
  return g_c2t_win32.GetDiskFreeSpaceExW
             ? g_c2t_win32.GetDiskFreeSpaceExW(
                   lpDirectoryName, lpFreeBytesAvailableToCaller,
                   lpTotalNumberOfBytes, lpTotalNumberOfFreeBytes)
             : FALSE;
}
static BOOL c2t_ReadFile(HANDLE hFile, LPVOID lpBuffer,
                          DWORD nNumberOfBytesToRead,
                          LPDWORD lpNumberOfBytesRead,
                          LPOVERLAPPED lpOverlapped) {
  c2t_win32_api_init();
  if (g_c2t_win32.ReadFile)
    return g_c2t_win32.ReadFile(hFile, lpBuffer, nNumberOfBytesToRead,
                                lpNumberOfBytesRead, lpOverlapped);
  return FALSE;
}
static BOOL c2t_GetFileSizeEx(HANDLE hFile, PLARGE_INTEGER lpFileSize) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetFileSizeEx)
    return g_c2t_win32.GetFileSizeEx(hFile, lpFileSize);
  return FALSE;
}
static BOOL c2t_CloseHandle(HANDLE hObject) {
  c2t_win32_api_init();
  if (g_c2t_win32.CloseHandle)
    return g_c2t_win32.CloseHandle(hObject);
  return FALSE;
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
#define CreateFileW c2t_CreateFileW
#define GetFileAttributesW c2t_GetFileAttributesW
#define GetFullPathNameW c2t_GetFullPathNameW
#define GetLogicalDrives c2t_GetLogicalDrives
#define GetDriveTypeW c2t_GetDriveTypeW
#define GetDiskFreeSpaceExW c2t_GetDiskFreeSpaceExW
#define ReadFile c2t_ReadFile
#define GetFileSizeEx c2t_GetFileSizeEx
#define CloseHandle c2t_CloseHandle

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
#ifdef _WIN32
  wchar_t *wide = utf8_path(path);
  if (!wide) {
    if (error_out && error_capacity > 0)
      snprintf(error_out, error_capacity, "Invalid UTF-8 file path");
    return READ_FILE_NOT_FOUND;
  }
  DWORD attr = GetFileAttributesW(wide);
  if (attr == INVALID_FILE_ATTRIBUTES) {
    free(wide);
    if (error_out && error_capacity > 0)
      snprintf(error_out, error_capacity,
               "File does not exist or cannot be accessed");
    return READ_FILE_NOT_FOUND;
  }
  if (attr & FILE_ATTRIBUTE_DIRECTORY) {
    free(wide);
    if (error_out && error_capacity > 0)
      snprintf(error_out, error_capacity,
               "Path is a directory, not a regular file");
    return READ_FILE_NOT_REGULAR;
  }
  HANDLE hFile = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  free(wide);
  if (hFile == INVALID_HANDLE_VALUE) {
    if (error_out && error_capacity > 0)
      snprintf(error_out, error_capacity, "Cannot open file for reading");
    return READ_FILE_ERROR;
  }
  LARGE_INTEGER fsz;
  if (!GetFileSizeEx(hFile, &fsz) || fsz.QuadPart < 0) {
    CloseHandle(hFile);
    if (error_out && error_capacity > 0)
      snprintf(error_out, error_capacity, "Cannot get file size");
    return READ_FILE_ERROR;
  }
  uint64_t file_bytes = (uint64_t)fsz.QuadPart;
  if ((uintmax_t)file_bytes > (uintmax_t)config->telegram_max_file_bytes) {
    CloseHandle(hFile);
    c2t_log_error(
        "files",
        "File '%s' exceeds configured limit of %llu bytes (size: %llu bytes)",
        path, (unsigned long long)config->telegram_max_file_bytes,
        (unsigned long long)file_bytes);
    if (error_out && error_capacity > 0) {
      snprintf(error_out, error_capacity,
               "File size (%.2f MB / %llu bytes) exceeds configured limit "
               "(%.2f MB / %llu bytes)",
               (double)file_bytes / (1024.0 * 1024.0),
               (unsigned long long)file_bytes,
               (double)config->telegram_max_file_bytes / (1024.0 * 1024.0),
               (unsigned long long)config->telegram_max_file_bytes);
    }
    return READ_FILE_ERROR;
  }
  *length = (size_t)file_bytes;
  *data = malloc(*length ? *length : 1);
  if (!*data) {
    CloseHandle(hFile);
    c2t_log_error("files", "Not enough memory to read file '%s'", path);
    if (error_out && error_capacity > 0)
      snprintf(error_out, error_capacity,
               "Memory allocation failed while reading file");
    return READ_FILE_ERROR;
  }
  DWORD bytes_read = 0;
  DWORD to_read = (DWORD)*length;
  if (to_read > 0 && !ReadFile(hFile, *data, to_read, &bytes_read, NULL)) {
    free(*data);
    *data = nullptr;
    CloseHandle(hFile);
    c2t_log_error("files", "Unable to read complete file '%s'", path);
    if (error_out && error_capacity > 0)
      snprintf(error_out, error_capacity,
               "Failed to read complete file content");
    return READ_FILE_ERROR;
  }
  CloseHandle(hFile);
  return READ_FILE_OK;
#else
  c2t_stat_t status;
  FILE *file;
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
#endif
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

  c2t_log_info("files", "Listing directory: '%s' (sanitized: '%s')",
               target, clean_path);

  int res = 0;
#ifdef _WIN32
  res = list_dir_windows(clean_path, output, capacity);
#else
  res = list_dir_posix(clean_path, output, capacity);
#endif
  c2t_log_info("files", "Directory listing for '%s' returned status=%d",
               clean_path, res);
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

#ifdef _WIN32
  wchar_t *wide = utf8_path(clean_path);
  if (!wide) {
    free(clean_path);
    snprintf(output, capacity, "⚠️ <b>Invalid UTF-8 file path</b>");
    return 0;
  }
  DWORD attr = GetFileAttributesW(wide);
  if (attr == INVALID_FILE_ATTRIBUTES) {
    free(wide);
    snprintf(output, capacity,
             "⚠️ <b>Cannot access file:</b> <code>%s</code>", clean_path);
    free(clean_path);
    return 0;
  }
  if (attr & FILE_ATTRIBUTE_DIRECTORY) {
    free(wide);
    snprintf(output, capacity,
             "⚠️ <code>%s</code> <b>is not a regular file.</b>", clean_path);
    free(clean_path);
    return 0;
  }
  HANDLE hFile = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  free(wide);
  if (hFile == INVALID_HANDLE_VALUE) {
    snprintf(output, capacity,
             "⚠️ <b>Cannot access file:</b> <code>%s</code>", clean_path);
    free(clean_path);
    return 0;
  }
  LARGE_INTEGER fsz;
  uint64_t file_size_val = 0;
  if (GetFileSizeEx(hFile, &fsz) && fsz.QuadPart > 0)
    file_size_val = (uint64_t)fsz.QuadPart;

  size_t limit = max_bytes > 0 && max_bytes < 3200 ? max_bytes : 3000;
  unsigned char buf[3500];
  DWORD bytes_read = 0;
  BOOL read_ok = ReadFile(hFile, buf, (DWORD)limit, &bytes_read, NULL);
  CloseHandle(hFile);
  if (!read_ok) {
    snprintf(output, capacity,
             "⚠️ <b>Error reading file:</b> <code>%s</code>", clean_path);
    free(clean_path);
    return 0;
  }
#else
  c2t_stat_t st;
  FILE *f = nullptr;
  int stat_res = stat(clean_path, &st);
  int saved_errno = errno;
  if (stat_res == 0)
    f = fopen(clean_path, "rb");

  if (stat_res != 0 || !f) {
    if (f)
      fclose(f);
    snprintf(output, capacity,
             "⚠️ <b>Cannot access file:</b> <code>%s</code>\n<b>Error:</b> %s",
             clean_path, strerror(saved_errno));
    free(clean_path);
    return 0;
  }

  if (!S_ISREG(st.st_mode)) {
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
  uint64_t file_size_val = (uint64_t)st.st_size;
#endif

  /* Check for binary content */
  for (size_t i = 0; i < bytes_read; ++i) {
    if (buf[i] == 0) {
      snprintf(output, capacity,
               "⚠️ <b>Binary File:</b> <code>%s</code> (size: %llu bytes)\n"
               "<i>File contains binary data and cannot be displayed as text.\n"
               "Use <code>/getfile %s</code> to download it.</i>",
               clean_path, (unsigned long long)file_size_val, clean_path);
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
           (unsigned long long)file_size_val);
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

  if ((uint64_t)bytes_read < file_size_val) {
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

#ifdef _WIN32
  wchar_t *wide = utf8_path(clean_path);
  if (!wide) {
    free(clean_path);
    snprintf(output, capacity, "⚠️ <b>Invalid UTF-8 path</b>");
    return 0;
  }
  DWORD attr = GetFileAttributesW(wide);
  if (attr == INVALID_FILE_ATTRIBUTES) {
    free(wide);
    snprintf(output, capacity,
             "⚠️ <b>Cannot access:</b> <code>%s</code>", clean_path);
    free(clean_path);
    return 0;
  }
  uint64_t file_size = 0;
  HANDLE hFile = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  free(wide);
  if (hFile != INVALID_HANDLE_VALUE) {
    LARGE_INTEGER fsz;
    if (GetFileSizeEx(hFile, &fsz) && fsz.QuadPart > 0) {
      file_size = (uint64_t)fsz.QuadPart;
    }
    CloseHandle(hFile);
  }
  const char *type_str = (attr & FILE_ATTRIBUTE_DIRECTORY) ? "Directory" : "Regular File";
  const char *type_icon = (attr & FILE_ATTRIBUTE_DIRECTORY) ? "📁" : "📄";
  char perm_str[32] = {};
  snprintf(perm_str, sizeof(perm_str), "%s",
           (attr & FILE_ATTRIBUTE_READONLY) ? "Read-Only" : "Read/Write");
  char size_str[32];
  format_size_human(file_size, size_str, sizeof(size_str));
  const char *filename = filename_from_path(clean_path);
  const char *mime = mime_from_filename(filename);

  snprintf(output, capacity,
           "ℹ️ <b>Filesystem Item Info</b>\n\n"
           "• <b>Path:</b> <code>%s</code>\n"
           "• <b>Name:</b> <code>%s</code>\n"
           "• <b>Type:</b> %s %s\n"
           "• <b>Size:</b> %s (<i>%llu bytes</i>)\n"
           "• <b>MIME Type:</b> <code>%s</code>\n"
           "• <b>Permissions:</b> <code>%s</code>",
           clean_path, filename, type_icon, type_str, size_str,
           (unsigned long long)file_size, mime, perm_str);
  free(clean_path);
  return 1;
#else
  c2t_stat_t st;
  int stat_res = stat(clean_path, &st);
  int saved_errno = errno;

  if (stat_res != 0) {
    snprintf(output, capacity,
             "⚠️ <b>Cannot access:</b> <code>%s</code>\n<b>Error:</b> %s",
             clean_path, strerror(saved_errno));
    free(clean_path);
    return 0;
  }

  const char *type_str = "Other / Unknown";
  const char *type_icon = "❓";
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

  char size_str[32];
  format_size_human((uint64_t)st.st_size, size_str, sizeof(size_str));

  const char *filename = filename_from_path(clean_path);
  const char *mime = mime_from_filename(filename);

  char time_str[64] = "Unknown";
  struct tm tm_buf;
  if (gmtime_r(&st.st_mtime, &tm_buf) != nullptr) {
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S UTC", &tm_buf);
  }

  char perm_str[32] = {};
  snprintf(
      perm_str, sizeof(perm_str), "%c%c%c%c%c%c%c%c%c%c (%04o)",
      S_ISDIR(st.st_mode) ? 'd' : (S_ISLNK(st.st_mode) ? 'l' : '-'),
      (st.st_mode & S_IRUSR) ? 'r' : '-', (st.st_mode & S_IWUSR) ? 'w' : '-',
      (st.st_mode & S_IXUSR) ? 'x' : '-', (st.st_mode & S_IRGRP) ? 'r' : '-',
      (st.st_mode & S_IWGRP) ? 'w' : '-', (st.st_mode & S_IXGRP) ? 'x' : '-',
      (st.st_mode & S_IROTH) ? 'r' : '-', (st.st_mode & S_IWOTH) ? 'w' : '-',
      (st.st_mode & S_IXOTH) ? 'x' : '-', (unsigned int)(st.st_mode & 07777));

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
#endif
}

static int is_directory_path(const char *path) {
  if (!path || !*path)
    return 0;
  size_t len = strlen(path);
  if (path[len - 1] == '/' || path[len - 1] == '\\')
    return 1;

#ifdef _WIN32
  wchar_t *wpath = utf8_path(path);
  if (!wpath)
    return 0;
  DWORD attr = GetFileAttributesW(wpath);
  free(wpath);
  if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
    return 1;
#else
  c2t_stat_t st;
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

/* ========================================================================= */
/* Interactive Telegram File Explorer Implementation                          */
/* ========================================================================= */

#define EXPLORER_MAX_ITEMS 512
#define EXPLORER_PAGE_SIZE 6

typedef struct {
  char name[768];
  char detail[64];
  uint64_t size;
  int is_dir;
  time_t mtime;
} c2t_explorer_item_t;

#ifdef _WIN32
static CRITICAL_SECTION s_explorer_mutex;
static atomic_bool s_explorer_mutex_inited;
static atomic_flag s_explorer_init_lock = ATOMIC_FLAG_INIT;
static void explorer_lock(void) {
  if (!atomic_load_explicit(&s_explorer_mutex_inited, memory_order_acquire)) {
    while (atomic_flag_test_and_set_explicit(&s_explorer_init_lock,
                                              memory_order_acquire)) {}
    if (!atomic_load_explicit(&s_explorer_mutex_inited,
                              memory_order_relaxed)) {
      InitializeCriticalSection(&s_explorer_mutex);
      atomic_store_explicit(&s_explorer_mutex_inited, 1,
                            memory_order_release);
    }
    atomic_flag_clear_explicit(&s_explorer_init_lock, memory_order_release);
  }
  EnterCriticalSection(&s_explorer_mutex);
}
static void explorer_unlock(void) {
  LeaveCriticalSection(&s_explorer_mutex);
}
#else
static pthread_mutex_t s_explorer_mutex = PTHREAD_MUTEX_INITIALIZER;
static void explorer_lock(void) { (void)pthread_mutex_lock(&s_explorer_mutex); }
static void explorer_unlock(void) { (void)pthread_mutex_unlock(&s_explorer_mutex); }
#endif

static char s_explorer_dir[1024] = {0};
static int s_explorer_page = 0;
static int64_t s_explorer_msg_id = 0;
static c2t_explorer_item_t s_explorer_items[EXPLORER_MAX_ITEMS];
static size_t s_explorer_item_count = 0;
static size_t s_explorer_dir_count = 0;
static size_t s_explorer_file_count = 0;
static uint64_t s_explorer_total_bytes = 0;
static uint32_t s_explorer_generation = 0;
static int s_explorer_truncated = 0;

static const char *get_file_icon(const char *name, int is_dir) {
  if (is_dir) return "📁";
  const char *dot = strrchr(name, '.');
  if (!dot) return "📄";
  if (strcasecmp(dot, ".c") == 0 || strcasecmp(dot, ".h") == 0 ||
      strcasecmp(dot, ".cpp") == 0 || strcasecmp(dot, ".py") == 0 ||
      strcasecmp(dot, ".js") == 0 || strcasecmp(dot, ".mjs") == 0 ||
      strcasecmp(dot, ".json") == 0 || strcasecmp(dot, ".html") == 0 ||
      strcasecmp(dot, ".css") == 0 || strcasecmp(dot, ".md") == 0 ||
      strcasecmp(dot, ".txt") == 0 || strcasecmp(dot, ".log") == 0 ||
      strcasecmp(dot, ".env") == 0 || strcasecmp(dot, ".yml") == 0 ||
      strcasecmp(dot, ".yaml") == 0 || strcasecmp(dot, ".xml") == 0 ||
      strcasecmp(dot, ".ini") == 0 || strcasecmp(dot, ".conf") == 0) {
    return "📝";
  }
  if (strcasecmp(dot, ".exe") == 0 || strcasecmp(dot, ".bat") == 0 ||
      strcasecmp(dot, ".cmd") == 0 || strcasecmp(dot, ".sh") == 0 ||
      strcasecmp(dot, ".ps1") == 0 || strcasecmp(dot, ".dll") == 0 ||
      strcasecmp(dot, ".so") == 0 || strcasecmp(dot, ".dylib") == 0) {
    return "⚙️";
  }
  if (strcasecmp(dot, ".zip") == 0 || strcasecmp(dot, ".tar") == 0 ||
      strcasecmp(dot, ".gz") == 0 || strcasecmp(dot, ".bz2") == 0 ||
      strcasecmp(dot, ".7z") == 0 || strcasecmp(dot, ".rar") == 0 ||
      strcasecmp(dot, ".xz") == 0) {
    return "📦";
  }
  if (strcasecmp(dot, ".png") == 0 || strcasecmp(dot, ".jpg") == 0 ||
      strcasecmp(dot, ".jpeg") == 0 || strcasecmp(dot, ".gif") == 0 ||
      strcasecmp(dot, ".bmp") == 0 || strcasecmp(dot, ".webp") == 0 ||
      strcasecmp(dot, ".svg") == 0 || strcasecmp(dot, ".ico") == 0) {
    return "🖼️";
  }
  if (strcasecmp(dot, ".mp4") == 0 || strcasecmp(dot, ".mkv") == 0 ||
      strcasecmp(dot, ".avi") == 0 || strcasecmp(dot, ".mov") == 0 ||
      strcasecmp(dot, ".mp3") == 0 || strcasecmp(dot, ".wav") == 0) {
    return "🎬";
  }
  if (strcasecmp(dot, ".pdf") == 0 || strcasecmp(dot, ".doc") == 0 ||
      strcasecmp(dot, ".docx") == 0 || strcasecmp(dot, ".xls") == 0 ||
      strcasecmp(dot, ".xlsx") == 0 || strcasecmp(dot, ".ppt") == 0 ||
      strcasecmp(dot, ".pptx") == 0) {
    return "📑";
  }
  return "📄";
}

static int compare_explorer_items(const void *a, const void *b) {
  const c2t_explorer_item_t *ia = (const c2t_explorer_item_t *)a;
  const c2t_explorer_item_t *ib = (const c2t_explorer_item_t *)b;
  if (ia->is_dir && !ib->is_dir) return -1;
  if (!ia->is_dir && ib->is_dir) return 1;
#ifdef _WIN32
  return _stricmp(ia->name, ib->name);
#else
  return strcasecmp(ia->name, ib->name);
#endif
}

static int scan_explorer_directory_locked(const char *dir_path) {
  if (!dir_path || !*dir_path) dir_path = ".";

#ifdef _WIN32
  if (_stricmp(dir_path, "drives") == 0 || _stricmp(dir_path, "root") == 0) {
    s_explorer_item_count = 0;
    s_explorer_dir_count = 0;
    s_explorer_file_count = 0;
    s_explorer_total_bytes = 0;
    s_explorer_truncated = 0;
    snprintf(s_explorer_dir, sizeof(s_explorer_dir), "Drives");

    DWORD drives = GetLogicalDrives();
    for (char letter = 'A'; letter <= 'Z'; letter++) {
      if (drives & (1 << (letter - 'A'))) {
        if (s_explorer_item_count >= EXPLORER_MAX_ITEMS)
          break;
        char root_path[8];
        snprintf(root_path, sizeof(root_path), "%c:\\", letter);
        wchar_t wroot[8];
        MultiByteToWideChar(CP_UTF8, 0, root_path, -1, wroot, 8);
        UINT type = GetDriveTypeW(wroot);
        const char *desc = "Drive";
        switch (type) {
        case DRIVE_REMOVABLE: desc = "Removable"; break;
        case DRIVE_FIXED:     desc = "Fixed"; break;
        case DRIVE_REMOTE:    desc = "Network"; break;
        case DRIVE_CDROM:     desc = "CD/DVD"; break;
        case DRIVE_RAMDISK:   desc = "RAMDisk"; break;
        default:              desc = "Drive"; break;
        }
        ULARGE_INTEGER free_bytes, total_bytes, total_free;
        uint64_t disk_free = 0;
        if (GetDiskFreeSpaceExW(wroot, &free_bytes, &total_bytes, &total_free)) {
          disk_free = (uint64_t)free_bytes.QuadPart;
        }

        c2t_explorer_item_t *item = &s_explorer_items[s_explorer_item_count++];
        char free_str[32] = {};
        format_size_human(disk_free, free_str, sizeof(free_str));
        snprintf(item->name, sizeof(item->name), "%c:\\", letter);
        snprintf(item->detail, sizeof(item->detail), "%s, %s free", desc,
                 free_str);
        item->is_dir = 1;
        item->size = disk_free;
        item->mtime = 0;
        s_explorer_dir_count++;
      }
    }
    s_explorer_generation++;
    if (s_explorer_generation == 0) s_explorer_generation = 1;
    return 1;
  }
#endif

  char *clean = sanitize_input_path(dir_path);
  if (!clean) clean = strdup(dir_path);
  if (!clean) return 0;

#ifdef _WIN32
  wchar_t *wide_dir = utf8_path(clean);
  if (!wide_dir) {
    free(clean);
    return 0;
  }
  DWORD attributes = GetFileAttributesW(wide_dir);
  if (attributes == INVALID_FILE_ATTRIBUTES ||
      (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
    free(wide_dir);
    free(clean);
    return 0;
  }

  size_t wide_len = wcslen(wide_dir);
  if (wide_len == 0 || wide_len > (SIZE_MAX / sizeof(wchar_t)) - 3) {
    free(wide_dir);
    free(clean);
    return 0;
  }
  wchar_t *search_pattern = calloc(wide_len + 3, sizeof(wchar_t));
  if (!search_pattern) {
    free(wide_dir);
    free(clean);
    return 0;
  }
  memcpy(search_pattern, wide_dir, (wide_len + 1) * sizeof(wchar_t));
  if (search_pattern[wide_len - 1] != L'\\' && search_pattern[wide_len - 1] != L'/') {
    wcscat(search_pattern, L"\\*");
  } else {
    wcscat(search_pattern, L"*");
  }

  char canonical[1024] = {0};
  DWORD canonical_capacity = 32768;
  wchar_t *canonical_wide = calloc(canonical_capacity, sizeof(wchar_t));
  if (canonical_wide) {
    DWORD canonical_length =
        GetFullPathNameW(wide_dir, canonical_capacity, canonical_wide, NULL);
    if (canonical_length > 0 && canonical_length < canonical_capacity)
      (void)WideCharToMultiByte(CP_UTF8, 0, canonical_wide, -1, canonical,
                                (int)sizeof(canonical), NULL, NULL);
  }
  free(canonical_wide);
  free(wide_dir);

  s_explorer_item_count = 0;
  s_explorer_dir_count = 0;
  s_explorer_file_count = 0;
  s_explorer_total_bytes = 0;
  s_explorer_truncated = 0;
  snprintf(s_explorer_dir, sizeof(s_explorer_dir), "%s",
           canonical[0] ? canonical : clean);
  free(clean);

  WIN32_FIND_DATAW fd;
  HANDLE hFind = FindFirstFileW(search_pattern, &fd);
  free(search_pattern);
  if (hFind != INVALID_HANDLE_VALUE) {
    do {
      if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
        continue;
      if (s_explorer_item_count >= EXPLORER_MAX_ITEMS) {
        s_explorer_truncated = 1;
        break;
      }

      char name[768] = {};
      if (WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name,
                              (int)sizeof(name), nullptr, nullptr) == 0)
        continue;
      int is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
      uint64_t sz = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;

      c2t_explorer_item_t *item = &s_explorer_items[s_explorer_item_count++];
      snprintf(item->name, sizeof(item->name), "%s", name);
      item->detail[0] = '\0';
      item->is_dir = is_dir;
      item->size = sz;
      item->mtime = 0;

      if (is_dir) {
        s_explorer_dir_count++;
      } else {
        s_explorer_file_count++;
        s_explorer_total_bytes += sz;
      }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
  }
#else
  DIR *d = opendir(clean);
  if (!d) {
    free(clean);
    return 0;
  }
  char *canonical = realpath(clean, nullptr);

  s_explorer_item_count = 0;
  s_explorer_dir_count = 0;
  s_explorer_file_count = 0;
  s_explorer_total_bytes = 0;
  s_explorer_truncated = 0;
  snprintf(s_explorer_dir, sizeof(s_explorer_dir), "%s", canonical ? canonical : clean);
  free(canonical);

  {
    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
      if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
        continue;
      if (s_explorer_item_count >= EXPLORER_MAX_ITEMS) {
        s_explorer_truncated = 1;
        break;
      }

      char full[1024];
      snprintf(full, sizeof(full), "%s/%s", clean, ent->d_name);
      struct stat st;
      int is_dir = 0;
      uint64_t sz = 0;
      time_t mtime = 0;
      if (stat(full, &st) == 0) {
        is_dir = S_ISDIR(st.st_mode);
        if (!is_dir) {
          sz = (uint64_t)st.st_size;
          s_explorer_total_bytes += sz;
        }
        mtime = st.st_mtime;
      }
#ifdef _DIRENT_HAVE_D_TYPE
      else if (ent->d_type == DT_DIR) {
        is_dir = 1;
      }
#endif
      c2t_explorer_item_t *item = &s_explorer_items[s_explorer_item_count++];
      snprintf(item->name, sizeof(item->name), "%s", ent->d_name);
      item->detail[0] = '\0';
      item->is_dir = is_dir;
      item->size = sz;
      item->mtime = mtime;

      if (is_dir) {
        s_explorer_dir_count++;
      } else {
        s_explorer_file_count++;
      }
    }
    (void)closedir(d);
  }
  free(clean);
#endif

  if (s_explorer_item_count > 1) {
    qsort(s_explorer_items, s_explorer_item_count, sizeof(c2t_explorer_item_t), compare_explorer_items);
  }

  s_explorer_generation++;
  if (s_explorer_generation == 0) s_explorer_generation = 1;

  c2t_log_info("files", "Scanned explorer directory '%s': %llu items (%llu dirs, %llu files, %llu bytes)",
               s_explorer_dir, (unsigned long long)s_explorer_item_count,
               (unsigned long long)s_explorer_dir_count,
               (unsigned long long)s_explorer_file_count,
               (unsigned long long)s_explorer_total_bytes);
  return 1;
}

static void append_json_escaped(char *dst, size_t *offset, size_t cap, const char *src) {
  if (!dst || !offset || !src) return;
  while (*src && *offset + 6 < cap) {
    if (*src == '"') {
      memcpy(dst + *offset, "\\\"", 2);
      *offset += 2;
    } else if (*src == '\\') {
      memcpy(dst + *offset, "\\\\", 2);
      *offset += 2;
    } else if ((unsigned char)*src >= 32) {
      dst[(*offset)++] = *src;
    }
    src++;
  }
  dst[*offset] = '\0';
}

static int append_json_literal(char *dst, size_t *offset, size_t cap,
                               const char *literal) {
  size_t length = strlen(literal);
  if (*offset + length >= cap) return 0;
  memcpy(dst + *offset, literal, length);
  *offset += length;
  dst[*offset] = '\0';
  return 1;
}

static int append_explorer_button(char *dst, size_t *offset, size_t cap,
                                  const char *label,
                                  const char *callback_data) {
  if (!append_json_literal(dst, offset, cap, "{\"text\":\"")) return 0;
  append_json_escaped(dst, offset, cap, label);
  if (!append_json_literal(dst, offset, cap, "\",\"callback_data\":\""))
    return 0;
  append_json_escaped(dst, offset, cap, callback_data);
  return append_json_literal(dst, offset, cap, "\"}");
}

static void explorer_label_name(char *output, size_t capacity,
                                const char *name, size_t maximum_bytes) {
  if (!output || capacity == 0) return;
  size_t input_offset = 0;
  size_t output_offset = 0;
  while (name[input_offset] && output_offset < maximum_bytes &&
         output_offset + 1 < capacity) {
    unsigned char first = (unsigned char)name[input_offset];
    size_t sequence_length = 1;
    if ((first & 0xE0U) == 0xC0U) sequence_length = 2;
    else if ((first & 0xF0U) == 0xE0U) sequence_length = 3;
    else if ((first & 0xF8U) == 0xF0U) sequence_length = 4;
    if (output_offset + sequence_length > maximum_bytes ||
        output_offset + sequence_length >= capacity)
      break;
    int valid = sequence_length == 1;
    if (sequence_length > 1) {
      valid = 1;
      for (size_t index = 1; index < sequence_length; ++index) {
        unsigned char continuation =
            (unsigned char)name[input_offset + index];
        if (!continuation || (continuation & 0xC0U) != 0x80U) {
          valid = 0;
          break;
        }
      }
    }
    if (!valid) sequence_length = 1;
    memcpy(output + output_offset, name + input_offset, sequence_length);
    output_offset += sequence_length;
    input_offset += sequence_length;
  }
  output[output_offset] = '\0';
}

static void build_explorer_keyboard_json(char *out_json, size_t cap, int page, int total_pages) {
  if (!out_json || cap < 64) return;
  size_t offset = 0;
  uint32_t generation = s_explorer_generation;
  if (!append_json_literal(out_json, &offset, cap, "{\"inline_keyboard\":["))
    return;

  size_t start_idx = (size_t)page * EXPLORER_PAGE_SIZE;
  size_t end_idx = start_idx + EXPLORER_PAGE_SIZE;
  if (end_idx > s_explorer_item_count) end_idx = s_explorer_item_count;

  /* Button rows for directory items on current page */
  for (size_t i = start_idx; i < end_idx; i++) {
    const c2t_explorer_item_t *item = &s_explorer_items[i];
    char btn_label[128];
    char cb_data[48];

    if (item->is_dir) {
      char trunc_name[48];
      explorer_label_name(trunc_name, sizeof(trunc_name), item->name, 30);
      if (item->detail[0])
        snprintf(btn_label, sizeof(btn_label), "📁 %s (%s)", trunc_name,
                 item->detail);
      else
        snprintf(btn_label, sizeof(btn_label), "📁 %s/", trunc_name);
      snprintf(cb_data, sizeof(cb_data), "fl_cd:%u:%llu", generation,
               (unsigned long long)i);
    } else {
      char sz_str[32] = {};
      format_size_human(item->size, sz_str, sizeof(sz_str));
      const char *icon = get_file_icon(item->name, 0);
      char trunc_name[48];
      explorer_label_name(trunc_name, sizeof(trunc_name), item->name, 24);
      snprintf(btn_label, sizeof(btn_label), "%s %s (%s)", icon, trunc_name, sz_str);
      snprintf(cb_data, sizeof(cb_data), "fl_sel:%u:%llu", generation,
               (unsigned long long)i);
    }

    if (i > start_idx && !append_json_literal(out_json, &offset, cap, ","))
      break;
    if (!append_json_literal(out_json, &offset, cap, "[") ||
        !append_explorer_button(out_json, &offset, cap, btn_label, cb_data) ||
        !append_json_literal(out_json, &offset, cap, "]"))
      break;
  }

  /* Navigation row */
  {
    char callback[48];
    if (end_idx > start_idx)
      (void)append_json_literal(out_json, &offset, cap, ",");
    (void)append_json_literal(out_json, &offset, cap, "[");

    snprintf(callback, sizeof(callback), "fl_up:%u", generation);
    (void)append_explorer_button(out_json, &offset, cap, "⬆️ Up", callback);

    /* Prev page */
    if (page > 0) {
      snprintf(callback, sizeof(callback), "fl_p:%u:%d", generation, page - 1);
      (void)append_json_literal(out_json, &offset, cap, ",");
      (void)append_explorer_button(out_json, &offset, cap, "◀️ Prev", callback);
    }

    /* Next page */
    if (page + 1 < total_pages) {
      snprintf(callback, sizeof(callback), "fl_p:%u:%d", generation, page + 1);
      (void)append_json_literal(out_json, &offset, cap, ",");
      (void)append_explorer_button(out_json, &offset, cap, "▶️ Next", callback);
    }

    snprintf(callback, sizeof(callback), "fl_rf:%u", generation);
    (void)append_json_literal(out_json, &offset, cap, ",");
    (void)append_explorer_button(out_json, &offset, cap, "🔄 Refresh", callback);
    (void)append_json_literal(out_json, &offset, cap, "]");
  }

  /* Bottom quick action bar: Home, Root/Drives, Close */
  {
    char callback[48];
    (void)append_json_literal(out_json, &offset, cap, ",[ ");
    snprintf(callback, sizeof(callback), "fl_home:%u", generation);
    (void)append_explorer_button(out_json, &offset, cap, "🏠 Home", callback);
    (void)append_json_literal(out_json, &offset, cap, ",");
    snprintf(callback, sizeof(callback), "fl_root:%u", generation);
#ifdef _WIN32
    (void)append_explorer_button(out_json, &offset, cap, "💾 Drives", callback);
#else
    (void)append_explorer_button(out_json, &offset, cap, "🌐 Root /", callback);
#endif
    (void)append_json_literal(out_json, &offset, cap, ",");
    snprintf(callback, sizeof(callback), "fl_close:%u", generation);
    (void)append_explorer_button(out_json, &offset, cap, "❌ Close", callback);
    (void)append_json_literal(out_json, &offset, cap, "]");
  }

  (void)append_json_literal(out_json, &offset, cap, "]}");
}

static void get_parent_directory(const char *current_dir, char *parent_dir, size_t cap) {
  if (!current_dir || !*current_dir || strcmp(current_dir, ".") == 0) {
    snprintf(parent_dir, cap, "..");
    return;
  }
#ifdef _WIN32
  if (_stricmp(current_dir, "Drives") == 0) {
    snprintf(parent_dir, cap, "Drives");
    return;
  }
  if (isalpha((unsigned char)current_dir[0]) && current_dir[1] == ':' &&
      (current_dir[2] == '\0' || (current_dir[2] == '\\' && current_dir[3] == '\0') ||
       (current_dir[2] == '/' && current_dir[3] == '\0'))) {
    snprintf(parent_dir, cap, "Drives");
    return;
  }
#else
  if (strcmp(current_dir, "/") == 0) {
    snprintf(parent_dir, cap, "/");
    return;
  }
#endif
  char buf[1024];
  snprintf(buf, sizeof(buf), "%s", current_dir);
  size_t len = strlen(buf);
  while (len > 1 && (buf[len - 1] == '/' || buf[len - 1] == '\\')) {
    buf[--len] = '\0';
  }
  char *last_slash = strrchr(buf, '/');
  char *last_bslash = strrchr(buf, '\\');
  char *sep = (last_slash > last_bslash) ? last_slash : last_bslash;
  if (sep) {
    if (sep == buf) {
      snprintf(parent_dir, cap, "/");
    } else {
      *sep = '\0';
      snprintf(parent_dir, cap, "%s", buf);
    }
  } else {
    snprintf(parent_dir, cap, "..");
  }
}

static int join_explorer_path(const char *directory, const char *name,
                              char *output, size_t capacity) {
  if (!directory || !name || !output || capacity == 0) return 0;
#ifdef _WIN32
  if (_stricmp(directory, "Drives") == 0) {
    int written = snprintf(output, capacity, "%s", name);
    return written >= 0 && (size_t)written < capacity;
  }
  const char separator = '\\';
#else
  const char separator = '/';
#endif
  size_t length = strlen(directory);
  int written = snprintf(output, capacity, "%s%s%s", directory,
                         length > 0 && (directory[length - 1] == '/' ||
                                        directory[length - 1] == '\\')
                             ? ""
                             : (separator == '/' ? "/" : "\\"),
                         name);
  return written >= 0 && (size_t)written < capacity;
}

static int parse_callback_generation(const char *data, const char *prefix,
                                     uint32_t *generation) {
  size_t prefix_length = strlen(prefix);
  if (strncmp(data, prefix, prefix_length) != 0) return 0;
  const char *number = data + prefix_length;
  char *end = NULL;
  errno = 0;
  unsigned long value = strtoul(number, &end, 10);
  if (errno || end == number || *end != '\0' || value == 0 ||
      value > UINT32_MAX)
    return 0;
  *generation = (uint32_t)value;
  return 1;
}

static int parse_callback_item(const char *data, const char *prefix,
                               uint32_t *generation, size_t *item_index) {
  size_t prefix_length = strlen(prefix);
  if (strncmp(data, prefix, prefix_length) != 0) return 0;
  const char *generation_text = data + prefix_length;
  char *separator = NULL;
  errno = 0;
  unsigned long generation_value = strtoul(generation_text, &separator, 10);
  if (errno || separator == generation_text || *separator != ':' ||
      generation_value == 0 || generation_value > UINT32_MAX)
    return 0;
  const char *index_text = separator + 1;
  char *end = NULL;
  errno = 0;
  unsigned long long index_value = strtoull(index_text, &end, 10);
  if (errno || end == index_text || *end != '\0' || index_value > SIZE_MAX)
    return 0;
  *generation = (uint32_t)generation_value;
  *item_index = (size_t)index_value;
  return 1;
}

static int show_file_action_dialog(size_t item_idx, uint32_t generation,
                                   int64_t edit_msg_id) {
  explorer_lock();
  if (generation != s_explorer_generation ||
      item_idx >= s_explorer_item_count || s_explorer_items[item_idx].is_dir) {
    explorer_unlock();
    return 0;
  }
  c2t_explorer_item_t item = s_explorer_items[item_idx];
  char cur_dir[1024];
  snprintf(cur_dir, sizeof(cur_dir), "%s", s_explorer_dir);
  explorer_unlock();

  char full_path[1024];
  if (!join_explorer_path(cur_dir, item.name, full_path, sizeof(full_path)))
    return 0;

  const char *mime = mime_from_filename(item.name);
  char sz_str[32] = {};
  format_size_human(item.size, sz_str, sizeof(sz_str));
  const char *icon = get_file_icon(item.name, 0);

  char esc_name[512] = {0};
  char esc_path[1024] = {0};
  size_t o1 = 0, o2 = 0;
  append_escaped_html(esc_name, &o1, sizeof(esc_name), item.name);
  append_escaped_html(esc_path, &o2, sizeof(esc_path), full_path);

  char html[1600];
  snprintf(html, sizeof(html),
           "%s <b>File Details:</b> <code>%s</code>\n\n"
           "• <b>Full Path:</b> <code>%s</code>\n"
           "• <b>Size:</b> %s (<i>%llu bytes</i>)\n"
           "• <b>MIME Type:</b> <code>%s</code>\n\n"
           "💡 <i>Select an action below to retrieve or inspect this file:</i>",
           icon, esc_name, esc_path, sz_str, (unsigned long long)item.size, mime);

  char keyboard[512];
  snprintf(keyboard, sizeof(keyboard),
           "{\"inline_keyboard\":[["
           "{\"text\":\"📥 Download File\",\"callback_data\":\"fl_get:%u:%llu\"},"
           "{\"text\":\"👁️ Preview (Cat)\",\"callback_data\":\"fl_cat:%u:%llu\"}"
           "],["
           "{\"text\":\"ℹ️ Detailed Info\",\"callback_data\":\"fl_info:%u:%llu\"},"
           "{\"text\":\"⬅️ Back to Explorer\",\"callback_data\":\"fl_back:%u\"}"
           "]]}",
           generation, (unsigned long long)item_idx, generation,
           (unsigned long long)item_idx, generation,
           (unsigned long long)item_idx, generation);

  if (edit_msg_id > 0) {
    return telegram_edit_message_html(edit_msg_id, html, keyboard);
  }
  return telegram_send_html_keyboard_get_id(html, keyboard, nullptr);
}

int c2t_file_explorer_show(const char *path, int page, int64_t edit_msg_id) {
  explorer_lock();
  int scan_ok = 0;
  if (path && *path) {
    scan_ok = scan_explorer_directory_locked(path);
  } else if (s_explorer_dir[0] != '\0') {
    scan_ok = scan_explorer_directory_locked(s_explorer_dir);
  } else {
    scan_ok = scan_explorer_directory_locked(".");
  }
  if (!scan_ok) {
    char requested[1024] = {0};
    size_t requested_offset = 0;
    append_escaped_html(requested, &requested_offset, sizeof(requested),
                        path && *path ? path : ".");
    explorer_unlock();
    char error_html[1280];
    snprintf(error_html, sizeof(error_html),
             "⚠️ <b>Cannot open directory:</b> <code>%s</code>\n"
             "<i>The path does not exist, is not a directory, or access was denied.</i>",
             requested);
    return telegram_send_html(error_html);
  }

  int total_pages = (int)((s_explorer_item_count + EXPLORER_PAGE_SIZE - 1) / EXPLORER_PAGE_SIZE);
  if (total_pages <= 0) total_pages = 1;
  if (page < 0) page = 0;
  if (page >= total_pages) page = total_pages - 1;
  s_explorer_page = page;

  char total_sz_str[32] = {};
  format_size_human(s_explorer_total_bytes, total_sz_str, sizeof(total_sz_str));

  char esc_dir[1024] = {0};
  size_t d_off = 0;
  append_escaped_html(esc_dir, &d_off, sizeof(esc_dir), s_explorer_dir[0] ? s_explorer_dir : ".");

  char html[3600];
  size_t offset = 0;
  snprintf(html, sizeof(html),
           "📁 <b>File Explorer:</b> <code>%s</code>\n"
           "📊 <i>%llu folders, %llu files (%s) • Page %d/%d</i>\n\n",
           esc_dir,
           (unsigned long long)s_explorer_dir_count,
           (unsigned long long)s_explorer_file_count,
           total_sz_str,
           page + 1, total_pages);
  offset = strlen(html);

  size_t start_idx = (size_t)page * EXPLORER_PAGE_SIZE;
  size_t end_idx = start_idx + EXPLORER_PAGE_SIZE;
  if (end_idx > s_explorer_item_count) end_idx = s_explorer_item_count;

  if (s_explorer_item_count == 0) {
    static const char empty_msg[] = "<i>(Directory is empty)</i>\n";
    if (offset + sizeof(empty_msg) < sizeof(html)) {
      memcpy(html + offset, empty_msg, sizeof(empty_msg) - 1);
      offset += sizeof(empty_msg) - 1;
    }
  } else {
    for (size_t i = start_idx; i < end_idx; i++) {
      const c2t_explorer_item_t *item = &s_explorer_items[i];
      char esc_item_name[512] = {0};
      size_t name_off = 0;
      append_escaped_html(esc_item_name, &name_off, sizeof(esc_item_name), item->name);

      char line[320];
      if (item->is_dir) {
        if (item->detail[0])
          snprintf(line, sizeof(line), "📁 <code>%s</code> <i>(%s)</i>\n",
                   esc_item_name, item->detail);
        else
          snprintf(line, sizeof(line), "📁 <code>%s/</code>\n",
                   esc_item_name);
      } else {
        char sz_str[32] = {};
        format_size_human(item->size, sz_str, sizeof(sz_str));
        const char *icon = get_file_icon(item->name, 0);
        snprintf(line, sizeof(line), "%s <code>%s</code> <i>(%s)</i>\n", icon, esc_item_name, sz_str);
      }
      size_t l_len = strlen(line);
      if (offset + l_len < sizeof(html) - 128) {
        memcpy(html + offset, line, l_len);
        offset += l_len;
      }
    }
  }

  if (s_explorer_truncated) {
    static const char truncated_msg[] =
        "\n⚠️ <i>Listing limited to the first 512 entries.</i>\n";
    size_t truncated_length = sizeof(truncated_msg) - 1;
    if (offset + truncated_length < sizeof(html)) {
      memcpy(html + offset, truncated_msg, truncated_length);
      offset += truncated_length;
    }
  }

  static const char footer_msg[] = "\n💡 <i>Tap items below to open, navigate, or download.</i>";
  size_t ft_len = sizeof(footer_msg) - 1;
  if (offset + ft_len < sizeof(html)) {
    memcpy(html + offset, footer_msg, ft_len);
    offset += ft_len;
  }
  html[offset] = '\0';

  char keyboard_json[2048];
  build_explorer_keyboard_json(keyboard_json, sizeof(keyboard_json), page, total_pages);

  c2t_log_info("files", "Rendering File Explorer for '%s' (page %d/%d, edit_id=%lld)",
               s_explorer_dir, page + 1, total_pages, (long long)edit_msg_id);

  explorer_unlock();

  if (edit_msg_id > 0) {
    if (telegram_edit_message_html(edit_msg_id, html, keyboard_json)) {
      explorer_lock();
      s_explorer_msg_id = edit_msg_id;
      explorer_unlock();
      return 1;
    }
  }

  int64_t new_id = 0;
  int ok = telegram_send_html_keyboard_get_id(html, keyboard_json, &new_id);
  if (ok && new_id > 0) {
    explorer_lock();
    s_explorer_msg_id = new_id;
    explorer_unlock();
  }
  return ok;
}

static int get_explorer_file(uint32_t generation, size_t item_index,
                             char *name, size_t name_capacity,
                             char *path, size_t path_capacity) {
  explorer_lock();
  int valid = generation == s_explorer_generation &&
              item_index < s_explorer_item_count &&
              !s_explorer_items[item_index].is_dir;
  if (valid) {
    snprintf(name, name_capacity, "%s", s_explorer_items[item_index].name);
    valid = join_explorer_path(s_explorer_dir, name, path, path_capacity);
  }
  explorer_unlock();
  return valid;
}

int c2t_file_explorer_handle_callback(const char *callback_query_id,
                                      const char *callback_data) {
  if (!callback_data || strncmp(callback_data, "fl_", 3) != 0) return 0;

  c2t_log_info("files", "File Explorer callback received: '%s' (query ID: %s)",
               callback_data, callback_query_id ? callback_query_id : "");

  explorer_lock();
  int64_t msg_id = s_explorer_msg_id;
  int curr_page = s_explorer_page;
  uint32_t current_generation = s_explorer_generation;
  char cur_dir[1024];
  snprintf(cur_dir, sizeof(cur_dir), "%s", s_explorer_dir);
  explorer_unlock();

  uint32_t generation = 0;
  size_t value = 0;
  int parsed = 0;

#define REQUIRE_CURRENT_CALLBACK(condition)                                    \
  do {                                                                         \
    if (!(condition) || generation != current_generation) {                    \
      (void)telegram_answer_callback_query(                                    \
          callback_query_id, "⚠️ Explorer expired. Open /files again.");       \
      return 1;                                                                \
    }                                                                          \
  } while (0)

  parsed = parse_callback_generation(callback_data, "fl_close:", &generation);
  if (parsed || strncmp(callback_data, "fl_close", 8) == 0) {
    REQUIRE_CURRENT_CALLBACK(parsed);
    (void)telegram_answer_callback_query(callback_query_id,
                                         "❌ File Explorer closed");
    if (msg_id > 0)
      (void)telegram_edit_message_html(
          msg_id,
          "⚪ <i>File Explorer closed. Use <code>/files</code> to re-open.</i>",
          nullptr);
    return 1;
  }

  parsed = parse_callback_generation(callback_data, "fl_rf:", &generation);
  if (parsed || strncmp(callback_data, "fl_rf", 5) == 0) {
    REQUIRE_CURRENT_CALLBACK(parsed);
    (void)telegram_answer_callback_query(callback_query_id, "🔄 Refreshed");
    return c2t_file_explorer_show(cur_dir, curr_page, msg_id);
  }

  parsed = parse_callback_generation(callback_data, "fl_up:", &generation);
  if (parsed || strncmp(callback_data, "fl_up", 5) == 0) {
    REQUIRE_CURRENT_CALLBACK(parsed);
    char parent[1024];
    get_parent_directory(cur_dir, parent, sizeof(parent));
    (void)telegram_answer_callback_query(callback_query_id,
                                         "⬆️ Navigating up...");
    return c2t_file_explorer_show(parent, 0, msg_id);
  }

  parsed = parse_callback_generation(callback_data, "fl_home:", &generation);
  if (parsed || strncmp(callback_data, "fl_home", 7) == 0) {
    REQUIRE_CURRENT_CALLBACK(parsed);
    const char *home = c2t_getenv("HOME");
#ifdef _WIN32
    if (!home) home = c2t_getenv("USERPROFILE");
#endif
    (void)telegram_answer_callback_query(callback_query_id,
                                         "🏠 Home directory");
    return c2t_file_explorer_show(home ? home : ".", 0, msg_id);
  }

  parsed = parse_callback_generation(callback_data, "fl_root:", &generation);
  if (parsed || strncmp(callback_data, "fl_root", 7) == 0) {
    REQUIRE_CURRENT_CALLBACK(parsed);
    (void)telegram_answer_callback_query(callback_query_id,
                                         "🌐 Root directory");
#ifdef _WIN32
    return c2t_file_explorer_show("drives", 0, msg_id);
#else
    return c2t_file_explorer_show("/", 0, msg_id);
#endif
  }

  parsed = parse_callback_item(callback_data, "fl_p:", &generation, &value);
  if (parsed || strncmp(callback_data, "fl_p:", 5) == 0) {
    REQUIRE_CURRENT_CALLBACK(parsed && value <= INT_MAX);
    char answer[32];
    snprintf(answer, sizeof(answer), "📑 Page %llu",
             (unsigned long long)value + 1);
    (void)telegram_answer_callback_query(callback_query_id, answer);
    return c2t_file_explorer_show(nullptr, (int)value, msg_id);
  }

  parsed = parse_callback_item(callback_data, "fl_cd:", &generation, &value);
  if (parsed || strncmp(callback_data, "fl_cd:", 6) == 0) {
    REQUIRE_CURRENT_CALLBACK(parsed);
    char item_name[768] = {0};
    explorer_lock();
    int valid = value < s_explorer_item_count &&
                s_explorer_items[value].is_dir;
    if (valid)
      snprintf(item_name, sizeof(item_name), "%s",
               s_explorer_items[value].name);
    explorer_unlock();
    char next_dir[1024];
    REQUIRE_CURRENT_CALLBACK(valid && join_explorer_path(
                                          cur_dir, item_name, next_dir,
                                          sizeof(next_dir)));
    (void)telegram_answer_callback_query(callback_query_id,
                                         "📁 Opening directory...");
    return c2t_file_explorer_show(next_dir, 0, msg_id);
  }

  parsed = parse_callback_item(callback_data, "fl_sel:", &generation, &value);
  if (parsed || strncmp(callback_data, "fl_sel:", 7) == 0) {
    REQUIRE_CURRENT_CALLBACK(parsed);
    (void)telegram_answer_callback_query(callback_query_id, "📄 File options");
    if (!show_file_action_dialog(value, generation, msg_id)) {
      (void)telegram_send_html("⚠️ <i>The selected file is no longer available.</i>");
    }
    return 1;
  }

  parsed = parse_callback_generation(callback_data, "fl_back:", &generation);
  if (parsed || strncmp(callback_data, "fl_back", 7) == 0) {
    REQUIRE_CURRENT_CALLBACK(parsed);
    (void)telegram_answer_callback_query(callback_query_id,
                                         "⬅️ Back to explorer");
    return c2t_file_explorer_show(nullptr, curr_page, msg_id);
  }

  const char *action_prefix = NULL;
  if (strncmp(callback_data, "fl_get:", 7) == 0) action_prefix = "fl_get:";
  else if (strncmp(callback_data, "fl_cat:", 7) == 0) action_prefix = "fl_cat:";
  else if (strncmp(callback_data, "fl_info:", 8) == 0) action_prefix = "fl_info:";

  if (action_prefix) {
    parsed = parse_callback_item(callback_data, action_prefix, &generation,
                                 &value);
    REQUIRE_CURRENT_CALLBACK(parsed);
    char item_name[768] = {0};
    char full_path[1024] = {0};
    REQUIRE_CURRENT_CALLBACK(get_explorer_file(
        generation, value, item_name, sizeof(item_name), full_path,
        sizeof(full_path)));

    if (strcmp(action_prefix, "fl_get:") == 0) {
      (void)telegram_answer_callback_query(callback_query_id,
                                           "📥 Uploading file...");
      (void)telegram_send_chat_action("upload_document");
      int send_result = c2t_file_send_path(full_path, nullptr);
      if (send_result == C2T_FILE_SENT) {
        char escaped_name[256] = {0};
        size_t escaped_offset = 0;
        append_escaped_html(escaped_name, &escaped_offset,
                            sizeof(escaped_name), item_name);
        char confirmation[512];
        snprintf(confirmation, sizeof(confirmation),
                 "✅ <b>File Delivered:</b> <code>%s</code>", escaped_name);
        (void)telegram_send_html(confirmation);
      }
      return 1;
    }

    char keyboard[320];
    snprintf(keyboard, sizeof(keyboard),
             "{\"inline_keyboard\":[["
             "{\"text\":\"📥 Download File\",\"callback_data\":\"fl_get:%u:%llu\"},"
             "{\"text\":\"⬅️ Back to Explorer\",\"callback_data\":\"fl_back:%u\"}"
             "]]}", generation, (unsigned long long)value, generation);

    if (strcmp(action_prefix, "fl_cat:") == 0) {
      (void)telegram_answer_callback_query(callback_query_id,
                                           "👁️ Loading preview...");
      char preview[3200];
      (void)c2t_file_read_text_preview(full_path, preview, sizeof(preview),
                                       2500);
      char escaped_name[256] = {0};
      size_t escaped_offset = 0;
      append_escaped_html(escaped_name, &escaped_offset,
                          sizeof(escaped_name), item_name);
      char html[3800];
      snprintf(html, sizeof(html),
               "👁️ <b>File Preview:</b> <code>%s</code>\n\n%s",
               escaped_name, preview);
      return telegram_edit_message_html(msg_id, html, keyboard);
    }

    (void)telegram_answer_callback_query(callback_query_id,
                                         "ℹ️ Loading info...");
    char info_text[1200];
    (void)c2t_file_get_info(full_path, info_text, sizeof(info_text));
    return telegram_edit_message_html(msg_id, info_text, keyboard);
  }

  (void)telegram_answer_callback_query(callback_query_id,
                                       "⚠️ Invalid explorer action");
  return 1;
#undef REQUIRE_CURRENT_CALLBACK
}
