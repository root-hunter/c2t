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

#ifndef C2T_SCREENSHOT_JPEG_H
#define C2T_SCREENSHOT_JPEG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Encodes 32-bit RGBA or BGRA pixel data to a JPEG in memory.
 *
 * @param width Image width in pixels.
 * @param height Image height in pixels.
 * @param pixel_data Pointer to raw 32-bit pixel buffer (4 bytes per pixel).
 * @param is_bgra 1 if the input format is BGRA (Windows GDI / X11), 0 for RGBA.
 * @param quality Compression quality factor from 1 to 100 (default recommended: 85).
 * @param out_jpeg Pointer to output buffer pointer, allocated via malloc().
 * @param out_size Pointer to output size in bytes.
 * @return 1 on success, 0 on failure or out of memory.
 */
[[nodiscard]] int screenshot_encode_jpeg_rgba(uint32_t width, uint32_t height,
                                              const uint8_t *pixel_data,
                                              int is_bgra, int quality,
                                              void **out_jpeg,
                                              size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif /* C2T_SCREENSHOT_JPEG_H */
