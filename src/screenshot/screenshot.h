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

#ifndef C2T_SCREENSHOT_H
#define C2T_SCREENSHOT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Capture the current screen / desktop and store the encoded image into an allocated buffer.
 * Uses the currently selected display target ("all", "0", "1", etc.).
 */
[[nodiscard]] int screenshot_capture(void **out_data, size_t *out_size,
                                     const char **out_mime_type,
                                     const char **out_filename);

/**
 * Capture a specific display or all displays.
 * @param display_target "all" for full virtual desktop, or "0", "1", etc. for a specific monitor.
 */
[[nodiscard]] int screenshot_capture_display(const char *display_target,
                                             void **out_data, size_t *out_size,
                                             const char **out_mime_type,
                                             const char **out_filename);

/**
 * Free buffer allocated by screenshot_capture.
 */
void screenshot_free_data(void *data);

/**
 * Returns 1 if screen capture is supported and available on current platform / session, 0 otherwise.
 */
[[nodiscard]] int screenshot_is_available(void);

/**
 * Returns a human-readable name of the active screen capture backend.
 */
[[nodiscard]] const char *screenshot_get_backend_name(void);

/**
 * Enumerate connected displays / monitors and format as formatted HTML list.
 */
[[nodiscard]] int screenshot_get_display_list(char *buffer, size_t max_len);

/**
 * Set the active display capture target ("all", "0", "1", etc.).
 */
[[nodiscard]] int screenshot_select_display(const char *target);

/**
 * Get the currently selected display target string.
 */
void screenshot_get_selected_display(char *buffer, size_t max_len);

/**
 * Get the count of detected active display devices.
 */
[[nodiscard]] int screenshot_get_display_count(void);

#ifdef __cplusplus
}
#endif

#endif
