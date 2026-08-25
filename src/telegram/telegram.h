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

#ifndef C2T_TELEGRAM_H
#define C2T_TELEGRAM_H

#include "../clipboard/clipboard_source.h"
#include "../crypto/crypto.h"
#include <stddef.h>
#include <stdint.h>

[[nodiscard]] int telegram_init(void);
[[nodiscard]] int telegram_send(const char *text, size_t length,
                                const c2t_clipboard_source_t *source);
[[nodiscard]] int telegram_send_keyboard(const char *text, size_t length);
[[nodiscard]] int telegram_send_data(const void *data, size_t length,
                                     const char *mime_type,
                                     const c2t_clipboard_source_t *source);

[[nodiscard]] int
telegram_send_encrypted_data(const void *encrypted_data, size_t length,
                             const unsigned char nonce[C2T_CRYPTO_NONCE_SIZE],
                             const char *mime_type,
                             const c2t_clipboard_source_t *source);
[[nodiscard]] int telegram_send_file(const void *data, size_t length,
                                     const char *mime_type,
                                     const char *filename,
                                     const c2t_clipboard_source_t *source);
[[nodiscard]] int
telegram_send_encrypted_file(const void *encrypted_data, size_t length,
                             const unsigned char nonce[C2T_CRYPTO_NONCE_SIZE],
                             const char *mime_type, const char *filename,
                             const c2t_clipboard_source_t *source);
[[nodiscard]] int
telegram_send_encrypted_photo(const void *encrypted_data, size_t length,
                              const unsigned char nonce[C2T_CRYPTO_NONCE_SIZE],
                              const char *mime_type, const char *filename,
                              const c2t_clipboard_source_t *source);
int telegram_send_html(const char *html_text);
[[nodiscard]] int telegram_send_chat_action(const char *action);
[[nodiscard]] int telegram_send_message_draft(int64_t draft_id, const char *html_text);
[[nodiscard]] int telegram_send_rich_message_draft(int64_t draft_id, const char *html_text);
[[nodiscard]] int telegram_clear_message_draft(int64_t draft_id);
[[nodiscard]] int telegram_send_html_keyboard_get_id(const char *html_text,
                                                    const char *reply_markup,
                                                    int64_t *out_msg_id);
[[nodiscard]] int telegram_edit_message_html(int64_t message_id,
                                            const char *html_text,
                                            const char *reply_markup);
[[nodiscard]] int telegram_answer_callback_query(const char *callback_query_id,
                                                const char *text);
[[nodiscard]] int telegram_get_bot_username(const char *token,
                                            char *username_out,
                                            size_t capacity);
[[nodiscard]] int telegram_get_file_path(const char *token, const char *file_id,
                                         char *file_path_out, size_t capacity);
[[nodiscard]] int telegram_download_file(const char *token, const char *file_id,
                                         const char *dest_path,
                                         size_t max_bytes,
                                         size_t *downloaded_bytes);

typedef struct {
  int64_t update_id;
  int64_t date;
  const char *chat_id;
  const char *username;
  const char *text;
  const char *caption;
  const char *file_id;
  const char *file_name;
  size_t file_size;
  const char *mime_type;
  const char *callback_query_id;
  const char *callback_data;
} telegram_incoming_update_t;

typedef void (*telegram_update_callback_t)(
    const telegram_incoming_update_t *update, void *user_data);

int telegram_parse_updates_response(const char *response,
                                    size_t response_length, int64_t *offset,
                                    telegram_update_callback_t callback,
                                    void *user_data);
int telegram_poll_updates_callback(const char *token, int64_t *offset,
                                   int timeout_seconds,
                                   telegram_update_callback_t callback,
                                   void *user_data);
[[nodiscard]] int telegram_poll_updates(const char *token, int64_t *offset,
                                        char *chat_id_out,
                                        size_t chat_id_capacity,
                                        char *username_out,
                                        size_t username_capacity,
                                        char *text_out, size_t text_capacity);
[[nodiscard]] int telegram_poll_updates_timeout(
    const char *token, int64_t *offset, int timeout_seconds, char *chat_id_out,
    size_t chat_id_capacity, char *username_out, size_t username_capacity,
    char *text_out, size_t text_capacity);
[[nodiscard]] int telegram_pair(const char *token, const char *expected_code,
                                char *chat_id_out, size_t capacity,
                                int timeout_seconds);
void telegram_cleanup(void);

#endif
