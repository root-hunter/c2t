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

#include "screenshot_encoder.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

c2t_image_format_t screenshot_parse_format(const char *format_str) {
  if (!format_str || !*format_str)
    return C2T_IMAGE_FORMAT_PNG;

  char lower[16] = {0};
  for (size_t i = 0; i < sizeof(lower) - 1 && format_str[i] != '\0'; ++i) {
    lower[i] = (char)tolower((unsigned char)format_str[i]);
  }

  if (strcmp(lower, "jpg") == 0 || strcmp(lower, "jpeg") == 0)
    return C2T_IMAGE_FORMAT_JPG;
  if (strcmp(lower, "bmp") == 0)
    return C2T_IMAGE_FORMAT_BMP;
  if (strcmp(lower, "tga") == 0)
    return C2T_IMAGE_FORMAT_TGA;
  if (strcmp(lower, "hdr") == 0)
    return C2T_IMAGE_FORMAT_HDR;
  if (strcmp(lower, "png") == 0)
    return C2T_IMAGE_FORMAT_PNG;

  return C2T_IMAGE_FORMAT_PNG;
}

const char *screenshot_format_to_string(c2t_image_format_t format) {
  switch (format) {
  case C2T_IMAGE_FORMAT_JPG:
    return "jpg";
  case C2T_IMAGE_FORMAT_BMP:
    return "bmp";
  case C2T_IMAGE_FORMAT_TGA:
    return "tga";
  case C2T_IMAGE_FORMAT_HDR:
    return "hdr";
  case C2T_IMAGE_FORMAT_PNG:
  default:
    return "png";
  }
}

const char *screenshot_format_mime(c2t_image_format_t format) {
  switch (format) {
  case C2T_IMAGE_FORMAT_JPG:
    return "image/jpeg";
  case C2T_IMAGE_FORMAT_BMP:
    return "image/bmp";
  case C2T_IMAGE_FORMAT_TGA:
    return "image/x-tga";
  case C2T_IMAGE_FORMAT_HDR:
    return "image/vnd.radiance";
  case C2T_IMAGE_FORMAT_PNG:
  default:
    return "image/png";
  }
}

const char *screenshot_format_filename(c2t_image_format_t format) {
  switch (format) {
  case C2T_IMAGE_FORMAT_JPG:
    return "screenshot.jpg";
  case C2T_IMAGE_FORMAT_BMP:
    return "screenshot.bmp";
  case C2T_IMAGE_FORMAT_TGA:
    return "screenshot.tga";
  case C2T_IMAGE_FORMAT_HDR:
    return "screenshot.hdr";
  case C2T_IMAGE_FORMAT_PNG:
  default:
    return "screenshot.png";
  }
}

typedef struct {
  uint8_t *data;
  size_t capacity;
  size_t size;
  int error;
} img_mem_context_t;

static void img_write_callback(void *context, void *data, int size) {
  if (size <= 0 || !data || !context)
    return;

  img_mem_context_t *ctx = (img_mem_context_t *)context;
  if (ctx->error)
    return;

  size_t write_len = (size_t)size;
  if (ctx->size + write_len > ctx->capacity) {
    size_t new_capacity = ctx->capacity ? ctx->capacity * 2 : 65536;
    while (ctx->size + write_len > new_capacity) {
      if (new_capacity > SIZE_MAX / 2) {
        ctx->error = 1;
        return;
      }
      new_capacity *= 2;
    }

    uint8_t *new_buf = (uint8_t *)realloc(ctx->data, new_capacity);
    if (!new_buf) {
      ctx->error = 1;
      return;
    }
    ctx->data = new_buf;
    ctx->capacity = new_capacity;
  }

  memcpy(ctx->data + ctx->size, data, write_len);
  ctx->size += write_len;
}

int screenshot_encode_image(c2t_image_format_t format,
                            uint32_t width, uint32_t height,
                            const uint8_t *pixel_data, int is_bgra,
                            int quality, void **out_data,
                            size_t *out_size) {
  if (out_data)
    *out_data = NULL;
  if (out_size)
    *out_size = 0;
  if (!pixel_data || width == 0 || height == 0 || !out_data || !out_size)
    return 0;

  if (quality <= 0)
    quality = 85;
  if (quality > 100)
    quality = 100;

  size_t total_pixels = (size_t)width * (size_t)height;
  if (total_pixels > SIZE_MAX / (3U * sizeof(float)))
    return 0;

  size_t rgb_size = total_pixels * 3U;
  uint8_t *rgb_buffer = (uint8_t *)malloc(rgb_size);
  if (!rgb_buffer)
    return 0;

  /* Conversion from 32-bit BGRA/RGBA to 24-bit RGB */
  const uint8_t *src = pixel_data;
  uint8_t *dst = rgb_buffer;

  if (is_bgra) {
    for (size_t i = 0; i < total_pixels; ++i) {
      dst[0] = src[2]; /* R */
      dst[1] = src[1]; /* G */
      dst[2] = src[0]; /* B */
      dst += 3;
      src += 4;
    }
  } else {
    for (size_t i = 0; i < total_pixels; ++i) {
      dst[0] = src[0]; /* R */
      dst[1] = src[1]; /* G */
      dst[2] = src[2]; /* B */
      dst += 3;
      src += 4;
    }
  }

  img_mem_context_t ctx = {0};
  ctx.capacity = rgb_size / 2 > 65536 ? rgb_size / 2 : 65536;
  ctx.data = (uint8_t *)malloc(ctx.capacity);
  if (!ctx.data) {
    free(rgb_buffer);
    return 0;
  }

  int success = 0;
  switch (format) {
  case C2T_IMAGE_FORMAT_PNG:
    success = stbi_write_png_to_func(img_write_callback, &ctx, (int)width,
                                     (int)height, 3, rgb_buffer,
                                     (int)width * 3);
    break;

  case C2T_IMAGE_FORMAT_JPG:
    success = stbi_write_jpg_to_func(img_write_callback, &ctx, (int)width,
                                     (int)height, 3, rgb_buffer, quality);
    break;

  case C2T_IMAGE_FORMAT_BMP:
    success = stbi_write_bmp_to_func(img_write_callback, &ctx, (int)width,
                                     (int)height, 3, rgb_buffer);
    break;

  case C2T_IMAGE_FORMAT_TGA:
    success = stbi_write_tga_to_func(img_write_callback, &ctx, (int)width,
                                     (int)height, 3, rgb_buffer);
    break;

  case C2T_IMAGE_FORMAT_HDR: {
    float *hdr_buffer = (float *)malloc(total_pixels * 3U * sizeof(float));
    if (hdr_buffer) {
      for (size_t i = 0; i < total_pixels * 3U; ++i) {
        hdr_buffer[i] = (float)rgb_buffer[i] / 255.0f;
      }
      success = stbi_write_hdr_to_func(img_write_callback, &ctx, (int)width,
                                       (int)height, 3, hdr_buffer);
      free(hdr_buffer);
    }
    break;
  }

  default:
    success = stbi_write_png_to_func(img_write_callback, &ctx, (int)width,
                                     (int)height, 3, rgb_buffer,
                                     (int)width * 3);
    break;
  }

  free(rgb_buffer);

  if (!success || ctx.error || ctx.size == 0) {
    free(ctx.data);
    return 0;
  }

  *out_data = ctx.data;
  *out_size = ctx.size;
  return 1;
}
