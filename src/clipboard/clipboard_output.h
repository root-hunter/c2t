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

#ifndef C2T_CLIPBOARD_OUTPUT_H
#define C2T_CLIPBOARD_OUTPUT_H

#include "clipboard_source.h"
#include <stddef.h>

[[nodiscard]] int clipboard_output_init(void);
void clipboard_output(const void *data, size_t length, const char *mime_type,
                      const c2t_clipboard_source_t *source);
[[nodiscard]] int clipboard_is_paused(void);
void clipboard_set_paused(int paused);
[[nodiscard]] int clipboard_toggle_paused(void);
void clipboard_output_flush(void);
void clipboard_get_status_info(char *buffer, size_t max_len);
[[nodiscard]] uint64_t clipboard_get_total_bytes(void);
[[nodiscard]] uint64_t clipboard_get_total_events(void);
void clipboard_output_cleanup(void);

#endif
