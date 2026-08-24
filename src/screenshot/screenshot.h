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
 *
 * @param out_data Pointer to receive allocated buffer containing encoded image (BMP or PNG).
 * @param out_size Pointer to receive the buffer size in bytes.
 * @param out_mime_type Pointer to receive static MIME type string (e.g. "image/bmp" or "image/png").
 * @param out_filename Pointer to receive suggested filename (e.g. "screenshot.bmp" or "screenshot.png").
 * @return 1 on success, 0 on failure.
 */
[[nodiscard]] int screenshot_capture(void **out_data, size_t *out_size,
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
 * Returns a human-readable name of the active screen capture backend (e.g. "X11 (XCB)", "Win32 (GDI)", "macOS (CoreGraphics)").
 */
[[nodiscard]] const char *screenshot_get_backend_name(void);

#ifdef __cplusplus
}
#endif

#endif
