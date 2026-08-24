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

#include "screenshot_jpeg.h"

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

typedef struct {
  uint8_t *data;
  size_t capacity;
  size_t size;
  int error;
} jpeg_mem_context_t;

static void jpeg_write_callback(void *context, void *data, int size) {
  if (size <= 0 || !data || !context)
    return;

  jpeg_mem_context_t *ctx = (jpeg_mem_context_t *)context;
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

int screenshot_encode_jpeg_rgba(uint32_t width, uint32_t height,
                                const uint8_t *pixel_data, int is_bgra,
                                int quality, void **out_jpeg,
                                size_t *out_size) {
  if (out_jpeg)
    *out_jpeg = NULL;
  if (out_size)
    *out_size = 0;
  if (!pixel_data || width == 0 || height == 0 || !out_jpeg || !out_size)
    return 0;

  if (quality <= 0)
    quality = 85;
  if (quality > 100)
    quality = 100;

  size_t total_pixels = (size_t)width * (size_t)height;
  if (total_pixels > SIZE_MAX / 3U)
    return 0;

  size_t rgb_size = total_pixels * 3U;
  uint8_t *rgb_buffer = (uint8_t *)malloc(rgb_size);
  if (!rgb_buffer)
    return 0;

  /* Fast conversion from 32-bit BGRA/RGBA to 24-bit RGB */
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

  jpeg_mem_context_t ctx = {0};
  ctx.capacity = rgb_size / 4 > 65536 ? rgb_size / 4 : 65536;
  ctx.data = (uint8_t *)malloc(ctx.capacity);
  if (!ctx.data) {
    free(rgb_buffer);
    return 0;
  }

  int success = stbi_write_jpg_to_func(jpeg_write_callback, &ctx, (int)width,
                                       (int)height, 3, rgb_buffer, quality);
  free(rgb_buffer);

  if (!success || ctx.error || ctx.size == 0) {
    free(ctx.data);
    return 0;
  }

  *out_jpeg = ctx.data;
  *out_size = ctx.size;
  return 1;
}
