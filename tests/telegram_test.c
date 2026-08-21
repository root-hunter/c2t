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

#define _POSIX_C_SOURCE 200809L

#include "telegram/telegram.h"
#include "telegram/telegram_platform.h"
#include "config/config.h"
#include "clipboard/clipboard_output.h"
#include "files/files.h"
#include "logging/logging.h"
#include "logging/log_sender.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char last_method[32];
static char last_content_type[128];
static unsigned char *last_body;
static size_t last_body_length;
static int http_init_calls;
static int http_post_calls;
static int http_post_result = 1;

static int fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static int body_contains(const void *value, size_t length)
{
    if (length > last_body_length)
        return 0;
    for (size_t index = 0; index <= last_body_length - length; ++index) {
        if (memcmp(last_body + index, value, length) == 0)
            return 1;
    }
    return 0;
}

int telegram_http_init(void)
{
    ++http_init_calls;
    return 1;
}

int telegram_http_post(const char *token, const char *method,
                       const char *content_type, const void *body,
                       size_t body_length)
{
    (void)token;
    ++http_post_calls;
    snprintf(last_method, sizeof(last_method), "%s", method);
    snprintf(last_content_type, sizeof(last_content_type), "%s", content_type);
    free(last_body);
    last_body = malloc(body_length);
    if (!last_body)
        return 0;
    memcpy(last_body, body, body_length);
    last_body_length = body_length;
    return http_post_result;
}

int telegram_http_get(const char *token, const char *method_and_query,
                      char *response_out, size_t response_capacity)
{
    (void)token;
    (void)method_and_query;
    if (response_out && response_capacity > 0) {
        snprintf(response_out, response_capacity,
                 "{\"ok\":true,\"result\":{\"username\":\"mock_bot\",\"id\":123456,\"chat\":{\"id\":123456}}}");
    }
    return 1;
}

void telegram_http_cleanup(void)
{
}

int main(void)
{
    if (getenv("C2T_EXPECT_EMBEDDED") &&
        strcmp(getenv("C2T_EXPECT_EMBEDDED"), "1") == 0) {
        unsetenv("TELEGRAM_BOT_TOKEN");
        unsetenv("TELEGRAM_CHAT_ID");
        c2t_config_load_environment();
        if (!c2t_config_get()->telegram_bot_token ||
            strcmp(c2t_config_get()->telegram_bot_token,
                   "987654:embedded-test-token") != 0 ||
            !c2t_config_get()->telegram_chat_id ||
            strcmp(c2t_config_get()->telegram_chat_id, "-987654") != 0)
            return fail("post-link embedded Telegram configuration");
    }

    unsetenv("TELEGRAM_SEND_WINDOW_INFO");
    unsetenv("C2T_VERBOSE");
    c2t_config_load_environment();
    if (c2t_config_get()->telegram_send_window_info)
        return fail("window information must be disabled by default");
    setenv("TELEGRAM_SEND_WINDOW_INFO", "1", 1);
    c2t_config_load_environment();
    if (!c2t_config_get()->telegram_send_window_info)
        return fail("TELEGRAM_SEND_WINDOW_INFO must enable source metadata");
    unsetenv("TELEGRAM_SEND_WINDOW_INFO");
    c2t_config_load_environment();
    char *source_option[] = {(char *)"c2t", (char *)"--send-window-info"};
    if (c2t_config_apply_arguments(2, source_option) != NULL ||
        !c2t_config_get()->telegram_send_window_info)
        return fail("--send-window-info must enable source metadata");
    unsetenv("TELEGRAM_SEND_WINDOW_INFO");
    c2t_config_load_environment();
    c2t_log_init();
    if (c2t_log_is_verbose())
        return fail("verbose logging must be disabled by default");
    setenv("C2T_VERBOSE", "1", 1);
    c2t_config_load_environment();
    c2t_log_init();
    if (!c2t_log_is_verbose())
        return fail("C2T_VERBOSE must enable verbose logging");
    unsetenv("C2T_VERBOSE");
    c2t_config_load_environment();
    c2t_log_init();

    unsetenv("TELEGRAM_ENABLED");
    c2t_config_load_environment();
    if (!telegram_init() || http_init_calls != 0)
        return fail("disabled Telegram must not require initialization");

    setenv("TELEGRAM_ENABLED", "1", 1);
    setenv("TELEGRAM_DEDUPLICATE", "1", 1);
    setenv("TELEGRAM_BOT_TOKEN", "123:test-token", 1);
    setenv("TELEGRAM_CHAT_ID", "-12345", 1);
    c2t_config_load_environment();
    if (!telegram_init() || http_init_calls != 1)
        return fail("enabled Telegram initialization");

    static const unsigned char png[] = {0x89, 'P', 'N', 'G', 0, 0xff};
    if (!telegram_send_data(png, sizeof(png), "image/png", NULL) ||
        strcmp(last_method, "sendPhoto") != 0 ||
        strncmp(last_content_type, "multipart/form-data; boundary=", 30) != 0 ||
        !body_contains(png, sizeof(png)) ||
        !body_contains("name=\"photo\"", 12))
        return fail("PNG multipart photo upload");
    if (!telegram_send_data(png, sizeof(png), "image/png", NULL) ||
        http_post_calls != 1)
        return fail("duplicate PNG must not be uploaded twice");

    static const unsigned char bmp[] = {'B', 'M', 0, 1};
    if (!telegram_send_data(bmp, sizeof(bmp), "image/bmp", NULL) ||
        strcmp(last_method, "sendDocument") != 0 ||
        !body_contains("Content-Type: image/bmp", 23))
        return fail("BMP document upload");

    if (!telegram_send_data("hello world", 11,
                            "text/plain;charset=utf-8", NULL) ||
        strcmp(last_method, "sendMessage") != 0 ||
        strcmp(last_content_type,
               "application/x-www-form-urlencoded") != 0 ||
        !body_contains("text=hello%20world", 18))
        return fail("text form upload");
    if (!telegram_send_data("hello world", 11,
                            "text/plain;charset=utf-8", NULL) ||
        http_post_calls != 3)
        return fail("duplicate text must not be sent twice");

    static const char phone[] = "+39 333-123-4567";
    if (!telegram_send_data(phone, sizeof(phone) - 1, "text/plain", NULL) ||
        strcmp(last_method, "sendContact") != 0 ||
        !body_contains("phone_number=%2B393331234567",
                       sizeof("phone_number=%2B393331234567") - 1) ||
        !body_contains("first_name=Clipboard",
                       sizeof("first_name=Clipboard") - 1))
        return fail("phone number contact card");
    if (!telegram_send_data(phone, sizeof(phone) - 1, "text/plain", NULL) ||
        http_post_calls != 4)
        return fail("duplicate contact must not be sent twice");

    static const char international_phone[] = "0039 333 1234567";
    if (!telegram_send_data(international_phone,
                            sizeof(international_phone) - 1, "text/plain",
                            NULL) ||
        strcmp(last_method, "sendContact") != 0 ||
        !body_contains("phone_number=%2B393331234567",
                       sizeof("phone_number=%2B393331234567") - 1))
        return fail("00 international prefix normalization");

    static const char local_phone[] = "3348668699";
    if (!telegram_send_data(local_phone, sizeof(local_phone) - 1,
                            "text/plain", NULL) ||
        strcmp(last_method, "sendMessage") != 0)
        return fail("phone without international prefix must remain text");

    static const char coordinates[] = "45.4642, 9.1900";
    if (!telegram_send_data(coordinates, sizeof(coordinates) - 1,
                            "text/plain", NULL) ||
        strcmp(last_method, "sendLocation") != 0 ||
        !body_contains("latitude=45.46420000",
                       sizeof("latitude=45.46420000") - 1) ||
        !body_contains("longitude=9.19000000",
                       sizeof("longitude=9.19000000") - 1))
        return fail("coordinate location card");

    static const char vcard[] =
        "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Mario Rossi\r\n"
        "TEL;TYPE=CELL:+39 320 1234567\r\nEND:VCARD";
    if (!telegram_send_data(vcard, sizeof(vcard) - 1, "text/vcard", NULL) ||
        strcmp(last_method, "sendContact") != 0 ||
        !body_contains("first_name=Mario%20Rossi",
                       sizeof("first_name=Mario%20Rossi") - 1) ||
        !body_contains("vcard=BEGIN%3AVCARD",
                       sizeof("vcard=BEGIN%3AVCARD") - 1))
        return fail("vCard contact card");

    static const char url[] = "https://example.com/path";
    if (!telegram_send_data(url, sizeof(url) - 1, "text/plain", NULL) ||
        strcmp(last_method, "sendMessage") != 0)
        return fail("URL message with native preview");

    static const unsigned char gif[] = {'G', 'I', 'F', '8', '9', 'a'};
    http_post_result = 0;
    if (telegram_send_data(gif, sizeof(gif), "image/gif", NULL))
        return fail("failed upload result");
    http_post_result = 1;
    if (!telegram_send_data(gif, sizeof(gif), "image/gif", NULL) ||
        http_post_calls != 11)
        return fail("failed content must remain retryable");
    if (!telegram_send_data(gif, sizeof(gif), "image/gif", NULL) ||
        http_post_calls != 11)
        return fail("retried content must be tracked after success");

    char file_path[] = "/tmp/c2t-file-test-XXXXXX";
    int file_descriptor = mkstemp(file_path);
    static const char file_contents[] = "clipboard file contents";
    if (file_descriptor < 0 ||
        write(file_descriptor, file_contents, sizeof(file_contents) - 1) !=
            (ssize_t)(sizeof(file_contents) - 1) ||
        close(file_descriptor) != 0)
        return fail("temporary file setup");

    if (c2t_file_try_clipboard_path(file_path, strlen(file_path),
                                    "text/plain", NULL) != C2T_FILE_NOT_HANDLED)
        return fail("file paths must be disabled by default");
    setenv("TELEGRAM_SEND_FILES", "1", 1);
    c2t_config_load_environment();
    if (c2t_file_try_clipboard_path(file_path, strlen(file_path),
                                    "text/plain", NULL) != C2T_FILE_SENT ||
        strcmp(last_method, "sendDocument") != 0 ||
        !body_contains(file_contents, sizeof(file_contents) - 1) ||
        !body_contains("filename=\"c2t-file-test-",
                       sizeof("filename=\"c2t-file-test-") - 1))
        return fail("filesystem file upload");

    char file_uri[128];
    int uri_length = snprintf(file_uri, sizeof(file_uri), "file://%s", file_path);
    if (uri_length <= 0 || (size_t)uri_length >= sizeof(file_uri) ||
        c2t_file_try_clipboard_path(file_uri, (size_t)uri_length,
                                    "text/uri-list", NULL) != C2T_FILE_SENT ||
        http_post_calls != 12)
        return fail("filesystem file URI deduplication");
    if (unlink(file_path) != 0)
        return fail("temporary file cleanup");

    telegram_cleanup();

    unsetenv("TELEGRAM_DEDUPLICATE");
    c2t_config_load_environment();
    if (!telegram_init())
        return fail("initialization without deduplication");
    if (!telegram_send_data(png, sizeof(png), "image/png", NULL) ||
        !telegram_send_data(png, sizeof(png), "image/png", NULL) ||
        http_post_calls != 14)
        return fail("duplicates must be sent when deduplication is disabled");

    static const char short_number[] = "123456";
    if (!telegram_send_data(short_number, sizeof(short_number) - 1,
                            "text/plain", NULL) ||
        strcmp(last_method, "sendMessage") != 0)
        return fail("short numeric text must not become a contact");
    static const char invalid_coordinates[] = "91.0000, 9.1900";
    if (!telegram_send_data(invalid_coordinates,
                            sizeof(invalid_coordinates) - 1, "text/plain",
                            NULL) ||
        strcmp(last_method, "sendMessage") != 0)
        return fail("invalid coordinates must remain text");

    telegram_cleanup();

    setenv("TELEGRAM_DEDUPLICATE", "1", 1);
    c2t_config_load_environment();
    if (!telegram_init())
        return fail("source metadata test initialization");
    c2t_clipboard_source_t source = {
        .application = "Firefox",
        .title = "Example\npage",
        .process_id = 4242
    };
    int source_posts = http_post_calls;
    static const char source_message[] = "unique source text";
    if (!telegram_send_data(source_message, sizeof(source_message) - 1,
                            "text/plain", &source) ||
        http_post_calls != source_posts + 1 ||
        !body_contains("text=Source%3A%20Firefox%20%7C%20Example%20page%20%7C%20"
                       "PID%204242%0A%0Aunique%20source%20text",
                       sizeof("text=Source%3A%20Firefox%20%7C%20Example%20page%20"
                              "%7C%20PID%204242%0A%0Aunique%20source%20text") - 1))
        return fail("text source metadata formatting");
    if (!telegram_send_data(source_message, sizeof(source_message) - 1,
                            "text/plain", &source) ||
        http_post_calls != source_posts + 1)
        return fail("same content and source must be deduplicated");
    snprintf(source.title, sizeof(source.title), "%s", "Another window");
    if (!telegram_send_data(source_message, sizeof(source_message) - 1,
                            "text/plain", &source) ||
        http_post_calls != source_posts + 2)
        return fail("different source windows must remain distinguishable");

    static const unsigned char source_png[] = {0x89, 'P', 'N', 'G', 1};
    if (!telegram_send_data(source_png, sizeof(source_png), "image/png",
                            &source) ||
        strcmp(last_method, "sendPhoto") != 0 ||
        !body_contains("name=\"caption\"", sizeof("name=\"caption\"") - 1) ||
        !body_contains("Source: Firefox | Another window | PID 4242",
                       sizeof("Source: Firefox | Another window | PID 4242") - 1))
        return fail("file source metadata caption");

    static const char queued_message[] = "asynchronous clipboard delivery";
    setenv("C2T_QUEUE_MAX_BYTES", "1", 1);
    c2t_config_load_environment();
    int rejected_posts = http_post_calls;
    if (!clipboard_output_init())
        return fail("bounded clipboard queue initialization");
    clipboard_output(queued_message, sizeof(queued_message) - 1,
                     "text/plain", &source);
    clipboard_output_cleanup();
    if (http_post_calls != rejected_posts)
        return fail("oversized clipboard queue event must be rejected");

    unsetenv("C2T_QUEUE_MAX_BYTES");
    c2t_config_load_environment();
    int queued_posts = http_post_calls;
    if (!clipboard_output_init())
        return fail("clipboard delivery worker initialization");
    clipboard_output(queued_message, sizeof(queued_message) - 1,
                     "text/plain", &source);
    clipboard_output_cleanup();
    if (http_post_calls != queued_posts + 1 ||
        !body_contains("asynchronous%20clipboard%20delivery",
                       sizeof("asynchronous%20clipboard%20delivery") - 1))
        return fail("asynchronous clipboard delivery");

    setenv("C2T_DELIVERY_ATTEMPTS", "3", 1);
    setenv("C2T_RETRY_DELAY_MS", "1", 1);
    c2t_config_load_environment();
    int retry_posts = http_post_calls;
    static const char retry_message[] = "retry clipboard delivery";
    http_post_result = 0;
    if (!clipboard_output_init())
        return fail("retry worker initialization");
    clipboard_output(retry_message, sizeof(retry_message) - 1,
                     "text/plain", &source);
    clipboard_output_cleanup();
    http_post_result = 1;
    if (http_post_calls != retry_posts + 3)
        return fail("bounded delivery retry count");
    unsetenv("C2T_DELIVERY_ATTEMPTS");
    unsetenv("C2T_RETRY_DELAY_MS");
    c2t_config_load_environment();

    int cache_posts = http_post_calls;
    char cache_message[32];
    for (size_t index = 0; index <= 1024; ++index) {
        int cache_length = snprintf(cache_message, sizeof(cache_message),
                                    "dedup-cache-%llu",
                                    (unsigned long long)index);
        if (cache_length <= 0 ||
            !telegram_send_data(cache_message, (size_t)cache_length,
                                "text/plain", NULL))
            return fail("bounded deduplication cache population");
    }
    static const char evicted_message[] = "dedup-cache-0";
    if (!telegram_send_data(evicted_message, sizeof(evicted_message) - 1,
                            "text/plain", NULL) ||
        http_post_calls != cache_posts + 1026)
        return fail("oldest deduplication entry must be evicted");

    setenv("TELEGRAM_SEND_LOGS", "1", 1);
    setenv("TELEGRAM_LOG_INTERVAL_SEC", "10", 1);
    c2t_config_load_environment();
    if (c2t_config_get()->telegram_send_logs != 1 ||
        c2t_config_get()->telegram_log_interval_sec != 10)
        return fail("telegram_send_logs and interval configuration parsing");
    if (!c2t_log_sender_init())
        return fail("log sender initialization");
    c2t_log_sender_cleanup();
    unsetenv("TELEGRAM_SEND_LOGS");
    unsetenv("TELEGRAM_LOG_INTERVAL_SEC");
    c2t_config_load_environment();

    if (!telegram_send_html("<b>Test Log</b>"))
        return fail("telegram_send_html execution");
    if (strcmp(last_method, "sendMessage") != 0 || !body_contains("parse_mode=HTML", 15))
        return fail("telegram_send_html payload");

    clipboard_set_paused(1);
    if (!clipboard_is_paused())
        return fail("clipboard_set_paused(1)");
    if (clipboard_toggle_paused() != 0 || clipboard_is_paused() != 0)
        return fail("clipboard_toggle_paused()");
    clipboard_set_paused(0);

    telegram_cleanup();
    free(last_body);
    return 0;
}
