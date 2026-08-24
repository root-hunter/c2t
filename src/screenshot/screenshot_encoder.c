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

  while (isspace((unsigned char)*format_str))
    format_str++;

  char lower[16] = {0};
  size_t len = 0;
  while (format_str[len] && !isspace((unsigned char)format_str[len]) && len < sizeof(lower) - 1) {
    lower[len] = (char)tolower((unsigned char)format_str[len]);
    len++;
  }
  lower[len] = '\0';

  if (len == 3) {
    uint32_t tag = (uint8_t)lower[0] | ((uint32_t)(uint8_t)lower[1] << 8) | ((uint32_t)(uint8_t)lower[2] << 16);
    if (tag == (('p') | ('n' << 8) | ('g' << 16))) return C2T_IMAGE_FORMAT_PNG;
    if (tag == (('j') | ('p' << 8) | ('g' << 16))) return C2T_IMAGE_FORMAT_JPG;
    if (tag == (('b') | ('m' << 8) | ('p' << 16))) return C2T_IMAGE_FORMAT_BMP;
    if (tag == (('t') | ('g' << 8) | ('a' << 16))) return C2T_IMAGE_FORMAT_TGA;
    if (tag == (('h') | ('d' << 8) | ('r' << 16))) return C2T_IMAGE_FORMAT_HDR;
    if (tag == (('r') | ('a' << 8) | ('w' << 16))) return C2T_IMAGE_FORMAT_BMP;
  } else if (len == 4) {
    uint32_t tag = (uint8_t)lower[0] | ((uint32_t)(uint8_t)lower[1] << 8) |
                   ((uint32_t)(uint8_t)lower[2] << 16) | ((uint32_t)(uint8_t)lower[3] << 24);
    if (tag == (('j') | ('p' << 8) | ('e' << 16) | ('g' << 24))) return C2T_IMAGE_FORMAT_JPG;
    if (tag == (('n') | ('o' << 8) | ('n' << 16) | ('e' << 24))) return C2T_IMAGE_FORMAT_BMP;
  } else if (len == 5) {
    if (memcmp(lower, "plain", 5) == 0) return C2T_IMAGE_FORMAT_BMP;
  } else if (len == 12) {
    if (memcmp(lower, "uncompressed", 12) == 0) return C2T_IMAGE_FORMAT_BMP;
  }

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

#if defined(__ARM_NEON) || defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define C2T_HAS_NEON_PIXEL_CONVERT 1
#elif (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86))
#include <tmmintrin.h>
#include <immintrin.h>
#define C2T_HAS_SSSE3_PIXEL_CONVERT 1
#define C2T_HAS_AVX2_PIXEL_CONVERT 1
#if defined(__GNUC__) || defined(__clang__)
#define C2T_TARGET_SSSE3 __attribute__((target("ssse3")))
#define C2T_TARGET_AVX2 __attribute__((target("avx2")))
#else
#define C2T_TARGET_SSSE3
#define C2T_TARGET_AVX2
#endif
#endif

#if defined(C2T_HAS_AVX2_PIXEL_CONVERT)
static C2T_TARGET_AVX2 void convert_pixels_avx2(const uint8_t *src, uint8_t *dst, size_t total_pixels, int is_bgra) {
  const __m256i shuf_bgra = _mm256_setr_epi8(
      2, 1, 0, 6, 5, 4, 10, 9, 8, 14, 13, 12, (char)0x80, (char)0x80, (char)0x80, (char)0x80,
      2, 1, 0, 6, 5, 4, 10, 9, 8, 14, 13, 12, (char)0x80, (char)0x80, (char)0x80, (char)0x80);
  const __m256i shuf_rgba = _mm256_setr_epi8(
      0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14, (char)0x80, (char)0x80, (char)0x80, (char)0x80,
      0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14, (char)0x80, (char)0x80, (char)0x80, (char)0x80);
  const __m256i shuf_mask = is_bgra ? shuf_bgra : shuf_rgba;
  size_t i = 0;

  /* 8 pixels (32 bytes BGRA/RGBA -> 24 bytes RGB) per AVX2 iteration */
  for (; i + 8 <= total_pixels; i += 8) {
    __m256i in = _mm256_loadu_si256((const __m256i *)src);
    __m256i out = _mm256_shuffle_epi8(in, shuf_mask);
    __m128i lo = _mm256_castsi256_si128(out);
    __m128i hi = _mm256_extracti128_si256(out, 1);
    memcpy(dst, &lo, 8);
    memcpy(dst + 8, ((const char *)&lo) + 8, 4);
    memcpy(dst + 12, &hi, 8);
    memcpy(dst + 20, ((const char *)&hi) + 8, 4);
    src += 32;
    dst += 24;
  }

  for (; i < total_pixels; ++i) {
    if (is_bgra) {
      dst[0] = src[2];
      dst[1] = src[1];
      dst[2] = src[0];
    } else {
      dst[0] = src[0];
      dst[1] = src[1];
      dst[2] = src[2];
    }
    src += 4;
    dst += 3;
  }
}
#endif

#if defined(C2T_HAS_SSSE3_PIXEL_CONVERT)
static C2T_TARGET_SSSE3 void convert_pixels_ssse3(const uint8_t *src, uint8_t *dst, size_t total_pixels, int is_bgra) {
  const __m128i shuf_bgra = _mm_setr_epi8(2, 1, 0, 6, 5, 4, 10, 9, 8, 14, 13, 12, (char)0x80, (char)0x80, (char)0x80, (char)0x80);
  const __m128i shuf_rgba = _mm_setr_epi8(0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14, (char)0x80, (char)0x80, (char)0x80, (char)0x80);
  const __m128i shuf_mask = is_bgra ? shuf_bgra : shuf_rgba;
  size_t i = 0;

  for (; i + 4 <= total_pixels; i += 4) {
    __m128i in = _mm_loadu_si128((const __m128i *)src);
    __m128i out = _mm_shuffle_epi8(in, shuf_mask);
    memcpy(dst, &out, 8);
    memcpy(dst + 8, ((const char *)&out) + 8, 4);
    src += 16;
    dst += 12;
  }

  for (; i < total_pixels; ++i) {
    if (is_bgra) {
      dst[0] = src[2];
      dst[1] = src[1];
      dst[2] = src[0];
    } else {
      dst[0] = src[0];
      dst[1] = src[1];
      dst[2] = src[2];
    }
    src += 4;
    dst += 3;
  }
}
#endif

static void convert_pixels_to_rgb(const uint8_t *src, uint8_t *dst, size_t total_pixels, int is_bgra) {
  size_t i = 0;

#if defined(C2T_HAS_NEON_PIXEL_CONVERT)
  /* 16 pixels (64 bytes BGRA/RGBA -> 48 bytes RGB) per NEON iteration */
  for (; i + 16 <= total_pixels; i += 16) {
    uint8x16x4_t quad = vld4q_u8(src);
    uint8x16x3_t rgb;
    if (is_bgra) {
      rgb.val[0] = quad.val[2]; /* R */
      rgb.val[1] = quad.val[1]; /* G */
      rgb.val[2] = quad.val[0]; /* B */
    } else {
      rgb.val[0] = quad.val[0]; /* R */
      rgb.val[1] = quad.val[1]; /* G */
      rgb.val[2] = quad.val[2]; /* B */
    }
    vst3q_u8(dst, rgb);
    src += 64;
    dst += 48;
  }
#elif defined(C2T_HAS_AVX2_PIXEL_CONVERT)
#if defined(__GNUC__) || defined(__clang__)
  if (__builtin_cpu_supports("avx2")) {
    convert_pixels_avx2(src, dst, total_pixels, is_bgra);
    return;
  }
#endif
#if defined(C2T_HAS_SSSE3_PIXEL_CONVERT)
#if defined(__GNUC__) || defined(__clang__)
  if (__builtin_cpu_supports("ssse3")) {
    convert_pixels_ssse3(src, dst, total_pixels, is_bgra);
    return;
  }
#else
  convert_pixels_ssse3(src, dst, total_pixels, is_bgra);
  return;
#endif
#endif
#endif

  /* Unrolled 4-pixel batch fallback for remaining or non-SIMD architectures */
  for (; i + 4 <= total_pixels; i += 4) {
    if (is_bgra) {
      dst[0]  = src[2];  dst[1]  = src[1];  dst[2]  = src[0];
      dst[3]  = src[6];  dst[4]  = src[5];  dst[5]  = src[4];
      dst[6]  = src[10]; dst[7]  = src[9];  dst[8]  = src[8];
      dst[9]  = src[14]; dst[10] = src[13]; dst[11] = src[12];
    } else {
      dst[0]  = src[0];  dst[1]  = src[1];  dst[2]  = src[2];
      dst[3]  = src[4];  dst[4]  = src[5];  dst[5]  = src[6];
      dst[6]  = src[8];  dst[7]  = src[9];  dst[8]  = src[10];
      dst[9]  = src[12]; dst[10] = src[13]; dst[11] = src[14];
    }
    src += 16;
    dst += 12;
  }

  /* Trailing 1-3 pixels */
  for (; i < total_pixels; ++i) {
    if (is_bgra) {
      dst[0] = src[2];
      dst[1] = src[1];
      dst[2] = src[0];
    } else {
      dst[0] = src[0];
      dst[1] = src[1];
      dst[2] = src[2];
    }
    src += 4;
    dst += 3;
  }
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

  /* Fast SIMD / vectorized conversion from 32-bit BGRA/RGBA to 24-bit RGB */
  convert_pixels_to_rgb(pixel_data, rgb_buffer, total_pixels, is_bgra);

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
