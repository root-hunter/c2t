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

#ifndef C2T_TELEGRAM_H
#define C2T_TELEGRAM_H

#include <stddef.h>
#include "../clipboard/clipboard_source.h"

int telegram_init(void);
int telegram_send(const char *text, size_t length,
                  const c2t_clipboard_source_t *source);
int telegram_send_data(const void *data, size_t length, const char *mime_type,
                       const c2t_clipboard_source_t *source);
int telegram_send_file(const void *data, size_t length, const char *mime_type,
                       const char *filename,
                       const c2t_clipboard_source_t *source);
int telegram_get_bot_username(const char *token, char *username_out, size_t capacity);
int telegram_poll_updates(const char *token, int64_t *offset,
                         char *chat_id_out, size_t chat_id_capacity,
                         char *username_out, size_t username_capacity,
                         char *text_out, size_t text_capacity);
int telegram_pair(const char *token, const char *expected_code,
                  char *chat_id_out, size_t capacity, int timeout_seconds);
void telegram_cleanup(void);

#endif
