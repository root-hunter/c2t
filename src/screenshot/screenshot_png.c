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

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define PNG_DEFLATE_BLOCK_SIZE 65535U
#define PNG_FIXED_OVERHEAD 57U

static inline void put_u32_be(uint8_t *p, uint32_t value) {
  p[0] = (uint8_t)(value >> 24);
  p[1] = (uint8_t)(value >> 16);
  p[2] = (uint8_t)(value >> 8);
  p[3] = (uint8_t)value;
}

static int checked_add_size(size_t left, size_t right, size_t *result) {
  if (left > SIZE_MAX - right)
    return 0;
  *result = left + right;
  return 1;
}

static int checked_mul_size(size_t left, size_t right, size_t *result) {
  if (left != 0 && right > SIZE_MAX / left)
    return 0;
  *result = left * right;
  return 1;
}

static void make_crc32_table(uint32_t table[8][256]) {
  for (uint32_t value = 0; value < 256; ++value) {
    uint32_t crc = value;
    for (unsigned int bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (UINT32_C(0xedb88320) &
                         (uint32_t)-(int32_t)(crc & 1U));
    table[0][value] = crc;
  }
  for (size_t slice = 1; slice < 8; ++slice) {
    for (uint32_t value = 0; value < 256; ++value) {
      table[slice][value] = (table[slice - 1][value] >> 8) ^
                            table[0][table[slice - 1][value] & 0xffU];
    }
  }
}

static uint32_t calc_crc32(const uint32_t table[8][256], const uint8_t *data,
                           size_t length) {
  uint32_t crc = UINT32_MAX;
  while (length >= 8) {
    uint32_t one = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                   ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    uint32_t two = (uint32_t)data[4] | ((uint32_t)data[5] << 8) |
                   ((uint32_t)data[6] << 16) | ((uint32_t)data[7] << 24);
    one ^= crc;
    crc = table[7][one & 0xffU] ^
          table[6][(one >> 8) & 0xffU] ^
          table[5][(one >> 16) & 0xffU] ^
          table[4][one >> 24] ^
          table[3][two & 0xffU] ^
          table[2][(two >> 8) & 0xffU] ^
          table[1][(two >> 16) & 0xffU] ^
          table[0][two >> 24];
    data += 8;
    length -= 8;
  }
  while (length != 0) {
    crc = table[0][(crc ^ *data++) & 0xffU] ^ (crc >> 8);
    --length;
  }
  return ~crc;
}

static void adler32_update(uint32_t *sum1_ptr, uint32_t *sum2_ptr,
                           const uint8_t *data, size_t length) {
  uint32_t s1 = *sum1_ptr;
  uint32_t s2 = *sum2_ptr;
  /* zlib's NMAX keeps the deferred modulo within 32-bit accumulators. */
  while (length != 0) {
    size_t chunk = length > 5552U ? 5552U : length;
    length -= chunk;
    while (chunk >= 16) {
      s1 += data[0]; s2 += s1;
      s1 += data[1]; s2 += s1;
      s1 += data[2]; s2 += s1;
      s1 += data[3]; s2 += s1;
      s1 += data[4]; s2 += s1;
      s1 += data[5]; s2 += s1;
      s1 += data[6]; s2 += s1;
      s1 += data[7]; s2 += s1;
      s1 += data[8]; s2 += s1;
      s1 += data[9]; s2 += s1;
      s1 += data[10]; s2 += s1;
      s1 += data[11]; s2 += s1;
      s1 += data[12]; s2 += s1;
      s1 += data[13]; s2 += s1;
      s1 += data[14]; s2 += s1;
      s1 += data[15]; s2 += s1;
      data += 16;
      chunk -= 16;
    }
    while (chunk-- != 0) {
      s1 += *data++;
      s2 += s1;
    }
    s1 %= 65521U;
    s2 %= 65521U;
  }
  *sum1_ptr = s1;
  *sum2_ptr = s2;
}

typedef struct {
  uint8_t *cursor;
  size_t block_remaining;
  size_t blocks_remaining;
  uint32_t adler_sum1;
  uint32_t adler_sum2;
} png_raw_writer_t;

static void raw_writer_open_block(png_raw_writer_t *writer,
                                  size_t raw_remaining) {
  size_t block_size =
      raw_remaining > PNG_DEFLATE_BLOCK_SIZE ? PNG_DEFLATE_BLOCK_SIZE
                                             : raw_remaining;
  int final_block = writer->blocks_remaining == 1U;
  writer->cursor[0] = (uint8_t)(final_block ? 1U : 0U);
  writer->cursor[1] = (uint8_t)block_size;
  writer->cursor[2] = (uint8_t)(block_size >> 8);
  uint16_t complement = (uint16_t)~(uint16_t)block_size;
  writer->cursor[3] = (uint8_t)complement;
  writer->cursor[4] = (uint8_t)(complement >> 8);
  writer->cursor += 5;
  writer->block_remaining = block_size;
  --writer->blocks_remaining;
}

static void raw_writer_write(png_raw_writer_t *writer, const uint8_t *data,
                             size_t length, size_t *raw_remaining) {
  while (length != 0) {
    if (writer->block_remaining == 0)
      raw_writer_open_block(writer, *raw_remaining);
    size_t amount = length < writer->block_remaining ? length
                                                      : writer->block_remaining;
    memcpy(writer->cursor, data, amount);
    adler32_update(&writer->adler_sum1, &writer->adler_sum2, data, amount);
    writer->cursor += amount;
    writer->block_remaining -= amount;
    *raw_remaining -= amount;
    data += amount;
    length -= amount;
  }
}

static void raw_writer_write_bgra(png_raw_writer_t *writer,
                                  const uint8_t *source, size_t pixels,
                                  size_t *raw_remaining) {
  while (pixels != 0) {
    if (writer->block_remaining < 4U) {
      uint8_t rgba[4] = {source[2], source[1], source[0],
                         source[3] != 0 ? source[3] : 255U};
      raw_writer_write(writer, rgba, sizeof(rgba), raw_remaining);
      source += 4;
      --pixels;
      continue;
    }

    size_t batch = writer->block_remaining / 4U;
    if (batch > pixels)
      batch = pixels;
    uint8_t *destination = writer->cursor;
    for (size_t pixel = 0; pixel < batch; ++pixel) {
      destination[0] = source[2];
      destination[1] = source[1];
      destination[2] = source[0];
      destination[3] = source[3] != 0 ? source[3] : 255U;
      destination += 4;
      source += 4;
    }
    size_t bytes = batch * 4U;
    adler32_update(&writer->adler_sum1, &writer->adler_sum2,
                   writer->cursor, bytes);
    writer->cursor += bytes;
    writer->block_remaining -= bytes;
    *raw_remaining -= bytes;
    pixels -= batch;
  }
}

int screenshot_encode_png_rgba(uint32_t width, uint32_t height,
                               const uint8_t *pixel_data, int is_bgra,
                               void **out_png, size_t *out_size) {
  if (out_png)
    *out_png = NULL;
  if (out_size)
    *out_size = 0;
  if (!pixel_data || width == 0 || height == 0 || !out_png || !out_size)
    return 0;

  size_t row_bytes;
  size_t scanline_bytes;
  size_t raw_size;
  if (!checked_mul_size((size_t)width, 4U, &row_bytes) ||
      !checked_add_size(row_bytes, 1U, &scanline_bytes) ||
      !checked_mul_size(scanline_bytes, (size_t)height, &raw_size))
    return 0;

  size_t block_count = raw_size / PNG_DEFLATE_BLOCK_SIZE;
  if (raw_size % PNG_DEFLATE_BLOCK_SIZE != 0)
    ++block_count;

  size_t block_overhead;
  size_t zlib_size;
  size_t png_size;
  if (!checked_mul_size(block_count, 5U, &block_overhead) ||
      !checked_add_size(raw_size, block_overhead, &zlib_size) ||
      !checked_add_size(zlib_size, 6U, &zlib_size) ||
      zlib_size > UINT32_MAX ||
      !checked_add_size(zlib_size, PNG_FIXED_OVERHEAD, &png_size))
    return 0;

  uint8_t *png = malloc(png_size);
  if (!png)
    return 0;

  static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n',
                                       0x1a, '\n'};
  memcpy(png, signature, sizeof(signature));
  size_t position = sizeof(signature);

  put_u32_be(png + position, 13U);
  position += 4;
  uint8_t *ihdr = png + position;
  memcpy(png + position, "IHDR", 4);
  position += 4;
  put_u32_be(png + position, width);
  position += 4;
  put_u32_be(png + position, height);
  position += 4;
  png[position++] = 8;
  png[position++] = 6;
  png[position++] = 0;
  png[position++] = 0;
  png[position++] = 0;

  uint32_t crc_table[8][256];
  make_crc32_table(crc_table);
  put_u32_be(png + position, calc_crc32(crc_table, ihdr, 17U));
  position += 4;

  put_u32_be(png + position, (uint32_t)zlib_size);
  position += 4;
  uint8_t *idat = png + position;
  memcpy(png + position, "IDAT", 4);
  position += 4;
  png[position++] = 0x78;
  png[position++] = 0x01;

  png_raw_writer_t writer = {.cursor = png + position,
                             .block_remaining = 0,
                             .blocks_remaining = block_count,
                             .adler_sum1 = 1,
                             .adler_sum2 = 0};
  size_t raw_remaining = raw_size;
  static const uint8_t no_filter = 0;
  for (uint32_t row = 0; row < height; ++row) {
    raw_writer_write(&writer, &no_filter, 1U, &raw_remaining);
    const uint8_t *source = pixel_data + (size_t)row * row_bytes;
    if (is_bgra)
      raw_writer_write_bgra(&writer, source, width, &raw_remaining);
    else
      raw_writer_write(&writer, source, row_bytes, &raw_remaining);
  }

  uint32_t adler = (writer.adler_sum2 << 16) | writer.adler_sum1;
  put_u32_be(writer.cursor, adler);
  writer.cursor += 4;
  position = (size_t)(writer.cursor - png);
  put_u32_be(png + position, calc_crc32(crc_table, idat, 4U + zlib_size));
  position += 4;

  put_u32_be(png + position, 0U);
  position += 4;
  uint8_t *iend = png + position;
  memcpy(png + position, "IEND", 4);
  position += 4;
  put_u32_be(png + position, calc_crc32(crc_table, iend, 4U));
  position += 4;

  if (position != png_size) {
    free(png);
    return 0;
  }
  *out_png = png;
  *out_size = png_size;
  return 1;
}
