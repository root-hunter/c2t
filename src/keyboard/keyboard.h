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

#ifndef C2T_KEYBOARD_H
#define C2T_KEYBOARD_H

#include <stddef.h>

typedef enum {
    KEYBOARD_MODE_CODE = 0,
    KEYBOARD_MODE_RAW = 1
} keyboard_format_mode_t;

[[nodiscard]] int keyboard_listener_init(void);
[[nodiscard]] int keyboard_listen(void);
void keyboard_listener_cleanup(void);

[[nodiscard]] int keyboard_get_device_list(char *buffer, size_t max_len);
[[nodiscard]] int keyboard_select_device(const char *target);
void keyboard_get_selected_target(char *buffer, size_t max_len);
[[nodiscard]] int keyboard_get_device_count(void);

#endif

