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

#ifndef C2T_SCREENSHOT_OUTPUT_H
#define C2T_SCREENSHOT_OUTPUT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

[[nodiscard]] int screenshot_output_init(void);
void screenshot_output_cleanup(void);

[[nodiscard]] int screenshot_capture_and_send(const char *caption);

[[nodiscard]] int screenshot_is_paused(void);
void screenshot_set_paused(int paused);
[[nodiscard]] int screenshot_toggle_paused(void);

void screenshot_set_interval(size_t interval_sec);
[[nodiscard]] size_t screenshot_get_interval(void);

void screenshot_get_status_info(char *buffer, size_t max_len);
[[nodiscard]] uint64_t screenshot_get_total_captures(void);
[[nodiscard]] uint64_t screenshot_get_total_bytes(void);

#ifdef __cplusplus
}
#endif

#endif
