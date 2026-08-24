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

#ifndef C2T_SCREENSHOT_ENCODER_H
#define C2T_SCREENSHOT_ENCODER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  C2T_IMAGE_FORMAT_PNG = 0,
  C2T_IMAGE_FORMAT_JPG,
  C2T_IMAGE_FORMAT_BMP,
  C2T_IMAGE_FORMAT_TGA,
  C2T_IMAGE_FORMAT_HDR,
  C2T_IMAGE_FORMAT_PLAIN,
  C2T_IMAGE_FORMAT_RAW = C2T_IMAGE_FORMAT_PLAIN,
} c2t_image_format_t;

c2t_image_format_t screenshot_parse_format(const char *format_str);
const char *screenshot_format_to_string(c2t_image_format_t format);
const char *screenshot_format_mime(c2t_image_format_t format);
const char *screenshot_format_filename(c2t_image_format_t format);

int screenshot_encode_image(c2t_image_format_t format,
                            uint32_t width, uint32_t height,
                            const uint8_t *pixel_data, int is_bgra,
                            int quality, void **out_data,
                            size_t *out_size);

int screenshot_transcode_image(const void *in_data, size_t in_size,
                               c2t_image_format_t target_format,
                               int quality,
                               void **out_data, size_t *out_size);

int screenshot_is_image_all_black(const void *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* C2T_SCREENSHOT_ENCODER_H */
