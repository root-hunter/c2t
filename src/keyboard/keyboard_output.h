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

#ifndef C2T_KEYBOARD_OUTPUT_H
#define C2T_KEYBOARD_OUTPUT_H

#include <stddef.h>
#include <stdint.h>

#define C2T_KEYBOARD_MIME_TYPE "text/x-c2t-keyboard"

[[nodiscard]] int keyboard_output_init(void);
void keyboard_output_append(const char *text, size_t length);
void keyboard_output_backspace(void);
void keyboard_output_flush(void);
[[nodiscard]] int keyboard_is_paused(void);
void keyboard_set_paused(int paused);
[[nodiscard]] int keyboard_toggle_paused(void);
[[nodiscard]] int keyboard_get_shortcuts_enabled(void);
void keyboard_set_shortcuts_enabled(int enabled);
[[nodiscard]] int keyboard_toggle_shortcuts(void);
void keyboard_set_format_mode(int mode);
[[nodiscard]] int keyboard_get_format_mode(void);
void keyboard_get_status_info(char *buffer, size_t max_len);
[[nodiscard]] uint64_t keyboard_get_total_bytes(void);
[[nodiscard]] uint64_t keyboard_get_total_keystrokes(void);
void keyboard_output_cleanup(void);

#endif

