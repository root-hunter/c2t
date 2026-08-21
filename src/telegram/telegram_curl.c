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

#include "telegram_platform.h"
#include "../logging/logging.h"
#include "c2t_version.h"

#include <curl/curl.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TELEGRAM_RESPONSE_CAPACITY 1024

static CURL *request;

typedef struct {
    char data[TELEGRAM_RESPONSE_CAPACITY];
    size_t length;
} response_buffer_t;

static size_t capture_response(char *data, size_t size, size_t count,
                               void *context)
{
    if (count && size > SIZE_MAX / count)
        return 0;
    size_t length = size * count;
    response_buffer_t *response = context;
    size_t available = sizeof(response->data) - 1 - response->length;
    size_t copied = length < available ? length : available;
    memcpy(response->data + response->length, data, copied);
    response->length += copied;
    response->data[response->length] = '\0';
    return length;
}

static void sanitize_response(response_buffer_t *response)
{
    for (size_t index = 0; index < response->length; ++index) {
        unsigned char character = (unsigned char)response->data[index];
        if (character < 0x20 || character == 0x7f)
            response->data[index] = ' ';
    }
}

int telegram_http_init(void)
{
    if (request)
        return 1;
    CURLcode result = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (result != CURLE_OK) {
        c2t_log_error("https", "Unable to initialize libcurl (error %d)",
                      (int)result);
        return 0;
    }

    request = curl_easy_init();
    if (!request) {
        c2t_log_error("https", "Unable to create the libcurl request handle");
        telegram_http_cleanup();
        return 0;
    }
    c2t_log_debug("https", "libcurl transport ready");
    return 1;
}

int telegram_http_post(const char *token, const char *method,
                       const char *content_type, const void *body,
                       size_t body_length)
{
    if (!request) {
        c2t_log_error("https", "Cannot perform HTTP request: transport handle is NULL");
        return 0;
    }
    c2t_log_debug("https", "POST %s (%llu-byte body, content-type=%s)",
                  method, (unsigned long long)body_length, content_type);
    char url[320];
    int url_length = snprintf(url, sizeof(url),
                              "https://api.telegram.org/bot%s/%s",
                              token, method);
    if (url_length < 0 || (size_t)url_length >= sizeof(url))
        return 0;

    char content_type_header[192];
    int header_length = snprintf(content_type_header,
                                 sizeof(content_type_header),
                                 "Content-Type: %s", content_type);
    if (header_length < 0 || (size_t)header_length >=
        sizeof(content_type_header))
        return 0;
    struct curl_slist *request_headers =
        curl_slist_append(NULL, content_type_header);
    if (!request_headers)
        return 0;

    response_buffer_t response = {{0}, 0};
    curl_easy_reset(request);
    curl_easy_setopt(request, CURLOPT_URL, url);
    curl_easy_setopt(request, CURLOPT_HTTPHEADER, request_headers);
    curl_easy_setopt(request, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(request, CURLOPT_POSTFIELDSIZE_LARGE,
                     (curl_off_t)body_length);
    curl_easy_setopt(request, CURLOPT_WRITEFUNCTION, capture_response);
    curl_easy_setopt(request, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(request, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(request, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(request, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(request, CURLOPT_USERAGENT, C2T_USER_AGENT);

    CURLcode result = curl_easy_perform(request);
    long status = 0;
    if (result == CURLE_OK)
        curl_easy_getinfo(request, CURLINFO_RESPONSE_CODE, &status);
    if (result != CURLE_OK || status < 200 || status >= 300) {
        sanitize_response(&response);
        if (response.length)
            c2t_log_error("https", "Telegram request failed: method=%s, "
                          "HTTP=%ld, curl_error=%d, response=%s", method,
                          status, (int)result, response.data);
        else
            c2t_log_error("https", "Telegram request failed: method=%s, "
                          "HTTP=%ld, curl_error=%d", method, status,
                          (int)result);
        curl_slist_free_all(request_headers);
        return 0;
    }
    c2t_log_debug("https", "Telegram request completed: method=%s, HTTP=%ld",
                  method, status);
    curl_slist_free_all(request_headers);
    return 1;
}

void telegram_http_cleanup(void)
{
    c2t_log_debug("https", "Cleaning up libcurl transport");
    if (request)
        curl_easy_cleanup(request);
    request = NULL;
    curl_global_cleanup();
}
