#include "telegram_platform.h"
#include "../logging/logging.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HINTERNET session;
static HINTERNET connection;

#define TELEGRAM_RESPONSE_CAPACITY 1024

static void read_error_response(HINTERNET request,
                                char response[TELEGRAM_RESPONSE_CAPACITY])
{
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

int telegram_http_init(void)
{
    session = WinHttpOpen(L"c2t/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                          WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS, 0);
    if (session) {
        WinHttpSetTimeouts(session, 5000, 5000, 5000, 15000);
        connection = WinHttpConnect(session, L"api.telegram.org",
                                    INTERNET_DEFAULT_HTTPS_PORT, 0);
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
                       size_t body_length)
{
    c2t_log_debug("https", "POST %s (%llu-byte body, content-type=%s)",
                  method, (unsigned long long)body_length, content_type);
    int token_length = MultiByteToWideChar(
        CP_UTF8, 0, token, -1, NULL, 0);
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
    int method_length = MultiByteToWideChar(
        CP_UTF8, 0, method, -1, position, (int)(300 - (position - path)));
    if (method_length <= 0)
        return 0;

    HINTERNET request = WinHttpOpenRequest(
        connection, L"POST", path, NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request)
        return 0;

    int header_size = MultiByteToWideChar(
        CP_UTF8, 0, content_type, -1, NULL, 0);
    wchar_t *header = header_size > 0 ?
        malloc(((size_t)header_size + 14) * sizeof(wchar_t)) : NULL;
    if (!header) {
        WinHttpCloseHandle(request);
        return 0;
    }
    memcpy(header, L"Content-Type: ", 14 * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, content_type, -1, header + 14,
                        header_size);
    BOOL success = WinHttpSendRequest(
        request, header, (DWORD)-1L, (void *)body, (DWORD)body_length,
        (DWORD)body_length, 0);
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
            c2t_log_error("https", "Telegram request failed: method=%s, "
                          "HTTP=%lu, winhttp_error=%lu, response=%s", method,
                          (unsigned long)status, (unsigned long)request_error,
                          response);
        else
            c2t_log_error("https", "Telegram request failed: method=%s, "
                          "HTTP=%lu, winhttp_error=%lu", method,
                          (unsigned long)status,
                          (unsigned long)request_error);
        WinHttpCloseHandle(request);
        return 0;
    }
    WinHttpCloseHandle(request);
    c2t_log_debug("https", "Telegram request completed: method=%s, HTTP=%lu",
                  method, (unsigned long)status);
    return 1;
}

void telegram_http_cleanup(void)
{
    c2t_log_debug("https", "Cleaning up WinHTTP transport");
    if (connection)
        WinHttpCloseHandle(connection);
    if (session)
        WinHttpCloseHandle(session);
    connection = NULL;
    session = NULL;
}
