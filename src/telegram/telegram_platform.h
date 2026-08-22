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

#ifndef C2T_TELEGRAM_PLATFORM_H
#define C2T_TELEGRAM_PLATFORM_H

#include <stddef.h>
#include "../crypto/crypto.h"

int telegram_http_init(void);
int telegram_http_post(const char *token, const char *method,
                       const char *content_type, const void *body,
                       size_t body_length);
int telegram_http_post_stream(const char *token, const char *method,
                              const char *content_type, c2t_stream_t *stream);
int telegram_http_get(const char *token, const char *method_and_query,
                      char *response_out, size_t response_capacity);
void telegram_http_thread_cleanup(void);
void telegram_http_cleanup(void);

#endif
