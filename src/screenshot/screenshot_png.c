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

#include "screenshot_png.h"
#include <stdlib.h>
#include <string.h>

/* Miniz lightweight deflate encoder for PNG IDAT chunks */
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ZLIB_APIS

/* Minimal adler32 calculation */
static uint32_t calc_adler32(const uint8_t *data, size_t len) {
  uint32_t s1 = 1;
  uint32_t s2 = 0;
  for (size_t i = 0; i < len; ++i) {
    s1 = (s1 + data[i]) % 65521U;
    s2 = (s2 + s1) % 65521U;
  }
  return (s2 << 16) | s1;
}

/* Minimal CRC32 for PNG chunk integrity */
static uint32_t calc_crc32(const uint8_t *buf, size_t len) {
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t i = 0; i < len; ++i) {
    crc ^= buf[i];
    for (int k = 0; k < 8; ++k) {
      crc = (crc >> 1) ^ (0xEDB88320U & (-(int32_t)(crc & 1)));
    }
  }
  return ~crc;
}

/* Big-endian helpers */
static inline void put_u32_be(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)((v >> 24) & 0xFF);
  p[1] = (uint8_t)((v >> 16) & 0xFF);
  p[2] = (uint8_t)((v >> 8) & 0xFF);
  p[3] = (uint8_t)(v & 0xFF);
}

/* Simple fast Deflate uncompressed blocks stream (Zlib RFC 1950 header + deflate blocks + adler32) */
static uint8_t *deflate_fast_zlib(const uint8_t *data, size_t len, size_t *out_len) {
  /* Worst-case uncompressed deflate overhead: 2 bytes zlib header + 5 bytes per 65535-byte block + 4 bytes adler32 */
  size_t num_blocks = (len + 65534U) / 65535U;
  if (num_blocks == 0) num_blocks = 1;
  size_t max_out = 2 + num_blocks * 5 + len + 4;

  uint8_t *out = (uint8_t *)malloc(max_out);
  if (!out) return NULL;

  size_t out_pos = 0;
  /* Zlib header: CM=8 (deflate), CINFO=7 (32K window) -> 0x78, FCHECK to make 0x7801 % 31 == 0 */
  out[out_pos++] = 0x78;
  out[out_pos++] = 0x01;

  size_t in_pos = 0;
  while (in_pos < len || (len == 0 && in_pos == 0)) {
    size_t chunk = len - in_pos;
    if (chunk > 65535U) chunk = 65535U;
    int is_final = (in_pos + chunk >= len) ? 1 : 0;

    out[out_pos++] = (uint8_t)(is_final ? 0x01 : 0x00);
    out[out_pos++] = (uint8_t)(chunk & 0xFF);
    out[out_pos++] = (uint8_t)((chunk >> 8) & 0xFF);
    uint16_t nlen = (uint16_t)(~chunk);
    out[out_pos++] = (uint8_t)(nlen & 0xFF);
    out[out_pos++] = (uint8_t)((nlen >> 8) & 0xFF);

    if (chunk > 0) {
      memcpy(out + out_pos, data + in_pos, chunk);
      out_pos += chunk;
      in_pos += chunk;
    }
    if (len == 0) break;
  }

  uint32_t adler = calc_adler32(data, len);
  put_u32_be(out + out_pos, adler);
  out_pos += 4;

  *out_len = out_pos;
  return out;
}

int screenshot_encode_png_rgba(uint32_t width, uint32_t height,
                               const uint8_t *pixel_data, int is_bgra,
                               void **out_png, size_t *out_size) {
  if (!pixel_data || width == 0 || height == 0 || !out_png || !out_size) {
    return 0;
  }
  *out_png = NULL;
  *out_size = 0;

  size_t row_bytes = (size_t)width * 4U;
  size_t raw_scanline_len = 1U + row_bytes; /* 1 filter byte + row pixels */
  size_t raw_total = raw_scanline_len * (size_t)height;

  uint8_t *raw_buf = (uint8_t *)malloc(raw_total);
  if (!raw_buf) {
    return 0;
  }

  /* Copy pixels line by line with filter byte 0 (None) and convert BGRA -> RGBA if needed */
  for (uint32_t y = 0; y < height; ++y) {
    size_t out_row_offset = (size_t)y * raw_scanline_len;
    raw_buf[out_row_offset] = 0; /* Filter: None */

    const uint8_t *src_row = pixel_data + ((size_t)y * row_bytes);
    uint8_t *dst_row = raw_buf + out_row_offset + 1U;

    if (is_bgra) {
      for (uint32_t x = 0; x < width; ++x) {
        uint8_t b = src_row[x * 4 + 0];
        uint8_t g = src_row[x * 4 + 1];
        uint8_t r = src_row[x * 4 + 2];
        uint8_t a = src_row[x * 4 + 3];
        dst_row[x * 4 + 0] = r;
        dst_row[x * 4 + 1] = g;
        dst_row[x * 4 + 2] = b;
        dst_row[x * 4 + 3] = a != 0 ? a : 255; /* Ensure opaque alpha if 0 */
      }
    } else {
      memcpy(dst_row, src_row, row_bytes);
    }
  }

  size_t idat_len = 0;
  uint8_t *idat_data = deflate_fast_zlib(raw_buf, raw_total, &idat_len);
  free(raw_buf);
  if (!idat_data) {
    return 0;
  }

  /* Construct PNG file:
   * 8 bytes signature
   * IHDR chunk: 4 len + 4 type + 13 data + 4 crc = 25 bytes
   * IDAT chunk: 4 len + 4 type + idat_len data + 4 crc = 12 + idat_len bytes
   * IEND chunk: 4 len + 4 type + 0 data + 4 crc = 12 bytes
   */
  size_t total_png_size = 8 + 25 + (12 + idat_len) + 12;
  uint8_t *png = (uint8_t *)malloc(total_png_size);
  if (!png) {
    free(idat_data);
    return 0;
  }

  size_t pos = 0;
  /* PNG Signature */
  static const uint8_t png_sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  memcpy(png + pos, png_sig, 8);
  pos += 8;

  /* IHDR */
  put_u32_be(png + pos, 13);
  pos += 4;
  uint8_t *ihdr_start = png + pos;
  memcpy(png + pos, "IHDR", 4);
  pos += 4;
  put_u32_be(png + pos, width);
  pos += 4;
  put_u32_be(png + pos, height);
  pos += 4;
  png[pos++] = 8; /* bit depth */
  png[pos++] = 6; /* color type 6: RGBA */
  png[pos++] = 0; /* compression method 0 */
  png[pos++] = 0; /* filter method 0 */
  png[pos++] = 0; /* interlace method 0 */
  uint32_t ihdr_crc = calc_crc32(ihdr_start, 17);
  put_u32_be(png + pos, ihdr_crc);
  pos += 4;

  /* IDAT */
  put_u32_be(png + pos, (uint32_t)idat_len);
  pos += 4;
  uint8_t *idat_header = png + pos;
  memcpy(png + pos, "IDAT", 4);
  pos += 4;
  memcpy(png + pos, idat_data, idat_len);
  pos += idat_len;
  /* CRC covers 'IDAT' + data */
  uint8_t *crc_block = (uint8_t *)malloc(4 + idat_len);
  if (crc_block) {
    memcpy(crc_block, "IDAT", 4);
    memcpy(crc_block + 4, idat_data, idat_len);
    uint32_t idat_crc = calc_crc32(crc_block, 4 + idat_len);
    free(crc_block);
    put_u32_be(png + pos, idat_crc);
  } else {
    /* Fallback calculate with chunk pointers */
    uint32_t idat_crc = calc_crc32(idat_header, 4 + idat_len);
    put_u32_be(png + pos, idat_crc);
  }
  pos += 4;
  free(idat_data);

  /* IEND */
  put_u32_be(png + pos, 0);
  pos += 4;
  uint8_t *iend_start = png + pos;
  memcpy(png + pos, "IEND", 4);
  pos += 4;
  uint32_t iend_crc = calc_crc32(iend_start, 4);
  put_u32_be(png + pos, iend_crc);
  pos += 4;

  *out_png = png;
  *out_size = pos;
  return 1;
}
