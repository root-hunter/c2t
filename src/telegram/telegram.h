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
void telegram_cleanup(void);

#endif
