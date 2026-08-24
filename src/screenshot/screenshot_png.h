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

#ifndef C2T_SCREENSHOT_PNG_H
#define C2T_SCREENSHOT_PNG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Encodes raw 32-bit pixel buffer (RGBA, BGRA, ARGB) into a standard PNG in memory.
 *
 * @param width Image width in pixels.
 * @param height Image height in pixels.
 * @param pixel_data Input pixel buffer (stride = width * 4).
 * @param is_bgra 1 if input pixels are BGRA (Windows GDI / X11 ZPixmap standard), 0 if RGBA.
 * @param out_png Pointer to store allocated PNG buffer (caller must free with free()).
 * @param out_size Pointer to store output PNG byte size.
 * @return 1 on success, 0 on failure.
 */
[[nodiscard]] int screenshot_encode_png_rgba(uint32_t width, uint32_t height,
                                            const uint8_t *pixel_data, int is_bgra,
                                            void **out_png, size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif /* C2T_SCREENSHOT_PNG_H */
