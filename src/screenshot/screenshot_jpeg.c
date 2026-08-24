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

/* Standard JPEG luminance and chrominance quantization tables */
static const uint8_t s_default_qtable_y[64] = {
    16, 11, 10, 16, 24,  40,  51,  61,  12, 12, 14, 19, 26,  58,  60,  55,
    14, 13, 16, 24, 40,  57,  69,  56,  14, 17, 22, 29, 51,  87,  80,  62,
    18, 22, 37, 56, 68,  109, 103, 77,  24, 35, 55, 64, 81,  104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};

static const uint8_t s_default_qtable_cbcr[64] = {
    17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};

static const uint8_t s_zigzag[64] = {
    0,  1,  5,  6,  14, 15, 27, 28, 2,  4,  7,  13, 16, 26, 29, 42,
    3,  8,  12, 17, 25, 30, 41, 43, 9,  11, 18, 24, 31, 40, 44, 53,
    10, 19, 23, 32, 39, 45, 52, 54, 20, 22, 33, 38, 46, 51, 55, 60,
    21, 34, 37, 47, 50, 56, 59, 61, 35, 36, 48, 49, 57, 58, 62, 63};

/* Standard Huffman tables */
static const uint8_t s_dc_lum_bits[17] = {0, 0, 1, 5, 1, 1, 1, 1, 1,
                                          1, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t s_dc_lum_val[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

static const uint8_t s_dc_chrom_bits[17] = {0, 0, 3, 1, 1, 1, 1, 1, 1,
                                            1, 1, 1, 0, 0, 0, 0, 0};
static const uint8_t s_dc_chrom_val[] = {0, 1, 2, 3, 4, 5,
                                         6, 7, 8, 9, 10, 11};

static const uint8_t s_ac_lum_bits[17] = {0, 0, 2, 1, 3, 3, 2, 4, 3,
                                          5, 5, 4, 4, 0, 0, 1, 0x7d};
static const uint8_t s_ac_lum_val[] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06,
    0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
    0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72,
    0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45,
    0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75,
    0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3,
    0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
    0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9,
    0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4,
    0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};

static const uint8_t s_ac_chrom_bits[17] = {0, 0, 2, 1, 2, 4, 4, 3, 4,
                                            7, 5, 4, 4, 0, 1, 2, 0x77};
static const uint8_t s_ac_chrom_val[] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41,
    0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
    0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1,
    0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44,
    0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74,
    0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a,
    0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
    0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
    0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4,
    0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};

typedef struct {
  uint16_t code[256];
  uint8_t size[256];
} huffman_table_t;

typedef struct {
  uint8_t *data;
  size_t capacity;
  size_t size;
  uint32_t bit_buffer;
  int bit_count;
} jpeg_writer_t;

static int writer_ensure_capacity(jpeg_writer_t *w, size_t needed) {
  if (w->size + needed <= w->capacity)
    return 1;
  size_t new_cap = w->capacity ? w->capacity * 2 : 16384;
  while (w->size + needed > new_cap) {
    if (new_cap > SIZE_MAX / 2)
      return 0;
    new_cap *= 2;
  }
  uint8_t *new_data = (uint8_t *)realloc(w->data, new_cap);
  if (!new_data)
    return 0;
  w->data = new_data;
  w->capacity = new_cap;
  return 1;
}

static inline void write_byte(jpeg_writer_t *w, uint8_t b) {
  if (w->size < w->capacity || writer_ensure_capacity(w, 1)) {
    w->data[w->size++] = b;
  }
}

static inline void write_word(jpeg_writer_t *w, uint16_t v) {
  write_byte(w, (uint8_t)(v >> 8));
  write_byte(w, (uint8_t)(v & 0xff));
}

static void write_bits(jpeg_writer_t *w, uint32_t bits, int length) {
  w->bit_buffer = (w->bit_buffer << length) | (bits & ((1U << length) - 1U));
  w->bit_count += length;
  while (w->bit_count >= 8) {
    uint8_t b = (uint8_t)((w->bit_buffer >> (w->bit_count - 8)) & 0xff);
    write_byte(w, b);
    if (b == 0xff) {
      write_byte(w, 0x00); /* Byte stuffing */
    }
    w->bit_count -= 8;
  }
}

static void flush_bits(jpeg_writer_t *w) {
  if (w->bit_count > 0) {
    uint8_t b = (uint8_t)((w->bit_buffer << (8 - w->bit_count)) & 0xff);
    b |= (uint8_t)((1U << (8 - w->bit_count)) - 1U); /* Pad with 1s */
    write_byte(w, b);
    if (b == 0xff) {
      write_byte(w, 0x00);
    }
    w->bit_buffer = 0;
    w->bit_count = 0;
  }
}

static void build_huffman_table(const uint8_t *bits, const uint8_t *values,
                                huffman_table_t *table) {
  memset(table, 0, sizeof(*table));
  uint16_t code = 0;
  int k = 0;
  for (int len = 1; len <= 16; ++len) {
    for (int i = 0; i < bits[len]; ++i) {
      uint8_t val = values[k++];
      table->code[val] = code;
      table->size[val] = (uint8_t)len;
      ++code;
    }
    code <<= 1;
  }
}

static void scale_quantization_table(const uint8_t *base, int quality,
                                     uint8_t *out_table, float *out_fquant) {
  quality = quality < 1 ? 1 : (quality > 100 ? 100 : quality);
  int scale = quality < 50 ? 5000 / quality : 200 - quality * 2;

  /* AAN DCT scale factors */
  static const float s_aan_scales[8] = {
      1.0f,
      1.387039845f,
      1.306562965f,
      1.175875602f,
      1.0f,
      0.785694958f,
      0.541196100f,
      0.275899379f,
  };

  for (int i = 0; i < 64; ++i) {
    int val = (base[i] * scale + 50) / 100;
    val = val < 1 ? 1 : (val > 255 ? 255 : val);
    out_table[i] = (uint8_t)val;

    int row = i / 8;
    int col = i % 8;
    out_fquant[i] = 1.0f / ((float)val * s_aan_scales[row] * s_aan_scales[col] * 8.0f);
  }
}

/* Fast forward 2D DCT (AAN algorithm) */
static void forward_dct_float(const float in[64], float out[64]) {
  float workspace[64];
  /* Pass 1: process rows */
  for (int i = 0; i < 8; ++i) {
    int row = i * 8;
    float tmp0 = in[row + 0] + in[row + 7];
    float tmp7 = in[row + 0] - in[row + 7];
    float tmp1 = in[row + 1] + in[row + 6];
    float tmp6 = in[row + 1] - in[row + 6];
    float tmp2 = in[row + 2] + in[row + 5];
    float tmp5 = in[row + 2] - in[row + 5];
    float tmp3 = in[row + 3] + in[row + 4];
    float tmp4 = in[row + 3] - in[row + 4];

    /* Even part */
    float tmp10 = tmp0 + tmp3;
    float tmp13 = tmp0 - tmp3;
    float tmp11 = tmp1 + tmp2;
    float tmp12 = tmp1 - tmp2;

    workspace[row + 0] = tmp10 + tmp11;
    workspace[row + 4] = tmp10 - tmp11;

    float z1 = (tmp12 + tmp13) * 0.707106781f;
    workspace[row + 2] = tmp13 + z1;
    workspace[row + 6] = tmp13 - z1;

    /* Odd part */
    tmp10 = tmp4 + tmp5;
    tmp11 = tmp5 + tmp6;
    tmp12 = tmp6 + tmp7;

    float z5 = (tmp10 - tmp12) * 0.382683432f;
    float z2 = 0.541196100f * tmp10 + z5;
    float z4 = 1.306562965f * tmp12 + z5;
    float z3 = tmp11 * 0.707106781f;

    float z11 = tmp7 + z3;
    float z13 = tmp7 - z3;

    workspace[row + 5] = z13 + z2;
    workspace[row + 3] = z13 - z2;
    workspace[row + 1] = z11 + z4;
    workspace[row + 7] = z11 - z4;
  }

  /* Pass 2: process columns */
  for (int i = 0; i < 8; ++i) {
    float tmp0 = workspace[0 * 8 + i] + workspace[7 * 8 + i];
    float tmp7 = workspace[0 * 8 + i] - workspace[7 * 8 + i];
    float tmp1 = workspace[1 * 8 + i] + workspace[6 * 8 + i];
    float tmp6 = workspace[1 * 8 + i] - workspace[6 * 8 + i];
    float tmp2 = workspace[2 * 8 + i] + workspace[5 * 8 + i];
    float tmp5 = workspace[2 * 8 + i] - workspace[5 * 8 + i];
    float tmp3 = workspace[3 * 8 + i] + workspace[4 * 8 + i];
    float tmp4 = workspace[3 * 8 + i] - workspace[4 * 8 + i];

    /* Even part */
    float tmp10 = tmp0 + tmp3;
    float tmp13 = tmp0 - tmp3;
    float tmp11 = tmp1 + tmp2;
    float tmp12 = tmp1 - tmp2;

    out[0 * 8 + i] = tmp10 + tmp11;
    out[4 * 8 + i] = tmp10 - tmp11;

    float z1 = (tmp12 + tmp13) * 0.707106781f;
    out[2 * 8 + i] = tmp13 + z1;
    out[6 * 8 + i] = tmp13 - z1;

    /* Odd part */
    tmp10 = tmp4 + tmp5;
    tmp11 = tmp5 + tmp6;
    tmp12 = tmp6 + tmp7;

    float z5 = (tmp10 - tmp12) * 0.382683432f;
    float z2 = 0.541196100f * tmp10 + z5;
    float z4 = 1.306562965f * tmp12 + z5;
    float z3 = tmp11 * 0.707106781f;

    float z11 = tmp7 + z3;
    float z13 = tmp7 - z3;

    out[5 * 8 + i] = z13 + z2;
    out[3 * 8 + i] = z13 - z2;
    out[1 * 8 + i] = z11 + z4;
    out[7 * 8 + i] = z11 - z4;
  }
}

static void quantize_and_encode_block(jpeg_writer_t *w, const float block[64],
                                      const float fquant[64], int16_t *prev_dc,
                                      const huffman_table_t *dc_table,
                                      const huffman_table_t *ac_table) {
  int16_t qblock[64];
  for (int i = 0; i < 64; ++i) {
    float val = block[i] * fquant[i];
    qblock[i] = (int16_t)(val >= 0 ? val + 0.5f : val - 0.5f);
  }

  /* Encode DC coefficient difference */
  int16_t dc_diff = (int16_t)(qblock[0] - *prev_dc);
  *prev_dc = qblock[0];

  if (dc_diff == 0) {
    write_bits(w, dc_table->code[0], dc_table->size[0]);
  } else {
    uint16_t abs_diff = (uint16_t)(dc_diff < 0 ? -dc_diff : dc_diff);
    int bit_size = 0;
    while (abs_diff > 0) {
      ++bit_size;
      abs_diff >>= 1;
    }
    write_bits(w, dc_table->code[bit_size], dc_table->size[bit_size]);
    uint16_t bits = (uint16_t)(dc_diff < 0 ? dc_diff - 1 : dc_diff);
    write_bits(w, bits, bit_size);
  }

  /* Encode AC coefficients with RLE */
  int zero_run = 0;
  for (int i = 1; i < 64; ++i) {
    int16_t ac = qblock[s_zigzag[i]];
    if (ac == 0) {
      ++zero_run;
    } else {
      while (zero_run >= 16) {
        write_bits(w, ac_table->code[0xf0], ac_table->size[0xf0]); /* ZRL */
        zero_run -= 16;
      }
      uint16_t abs_ac = (uint16_t)(ac < 0 ? -ac : ac);
      int bit_size = 0;
      while (abs_ac > 0) {
        ++bit_size;
        abs_ac >>= 1;
      }
      uint8_t symbol = (uint8_t)((zero_run << 4) | bit_size);
      write_bits(w, ac_table->code[symbol], ac_table->size[symbol]);
      uint16_t bits = (uint16_t)(ac < 0 ? ac - 1 : ac);
      write_bits(w, bits, bit_size);
      zero_run = 0;
    }
  }
  if (zero_run > 0) {
    write_bits(w, ac_table->code[0x00], ac_table->size[0x00]); /* EOB */
  }
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

  /* Initialize quantization tables */
  uint8_t q_lum[64], q_chrom[64];
  float fq_lum[64], fq_chrom[64];
  scale_quantization_table(s_default_qtable_y, quality, q_lum, fq_lum);
  scale_quantization_table(s_default_qtable_cbcr, quality, q_chrom, fq_chrom);

  /* Initialize Huffman tables */
  huffman_table_t dc_lum_ht, dc_chrom_ht, ac_lum_ht, ac_chrom_ht;
  build_huffman_table(s_dc_lum_bits, s_dc_lum_val, &dc_lum_ht);
  build_huffman_table(s_dc_chrom_bits, s_dc_chrom_val, &dc_chrom_ht);
  build_huffman_table(s_ac_lum_bits, s_ac_lum_val, &ac_lum_ht);
  build_huffman_table(s_ac_chrom_bits, s_ac_chrom_val, &ac_chrom_ht);

  jpeg_writer_t writer = {0};
  size_t estimated_size = (size_t)width * (size_t)height / 8 + 4096;
  if (estimated_size < 32768)
    estimated_size = 32768;
  writer.data = (uint8_t *)malloc(estimated_size);
  if (!writer.data)
    return 0;
  writer.capacity = estimated_size;

  /* SOI Marker */
  write_word(&writer, 0xffd8);

  /* APP0 JFIF Marker */
  write_word(&writer, 0xffe0);
  write_word(&writer, 16);
  write_byte(&writer, 'J');
  write_byte(&writer, 'F');
  write_byte(&writer, 'I');
  write_byte(&writer, 'F');
  write_byte(&writer, 0);
  write_byte(&writer, 1); /* Version 1.01 */
  write_byte(&writer, 1);
  write_byte(&writer, 0); /* Aspect ratio */
  write_word(&writer, 1);
  write_word(&writer, 1);
  write_byte(&writer, 0); /* No thumbnail */
  write_byte(&writer, 0);

  /* DQT Markers (Luminance table 0, Chrominance table 1) */
  write_word(&writer, 0xffdb);
  write_word(&writer, 2 + 1 + 64 + 1 + 64);
  write_byte(&writer, 0x00);
  for (int i = 0; i < 64; ++i)
    write_byte(&writer, q_lum[s_zigzag[i]]);
  write_byte(&writer, 0x01);
  for (int i = 0; i < 64; ++i)
    write_byte(&writer, q_chrom[s_zigzag[i]]);

  /* SOF0 Marker (Baseline DCT, 3 components: Y, Cb, Cr, 4:2:0 subsampling) */
  write_word(&writer, 0xffc0);
  write_word(&writer, 2 + 1 + 2 + 2 + 1 + 3 * 3);
  write_byte(&writer, 8); /* 8-bit precision */
  write_word(&writer, (uint16_t)height);
  write_word(&writer, (uint16_t)width);
  write_byte(&writer, 3); /* 3 color components */

  /* Component 1: Y (2x2 sampling factor = 4:2:0) */
  write_byte(&writer, 1);
  write_byte(&writer, 0x22);
  write_byte(&writer, 0);

  /* Component 2: Cb (1x1 sampling factor) */
  write_byte(&writer, 2);
  write_byte(&writer, 0x11);
  write_byte(&writer, 1);

  /* Component 3: Cr (1x1 sampling factor) */
  write_byte(&writer, 3);
  write_byte(&writer, 0x11);
  write_byte(&writer, 1);

  /* DHT Markers (Define Huffman Tables) */
  write_word(&writer, 0xffc4);
  size_t dht_len = 2 + 4 + sizeof(s_dc_lum_val) + sizeof(s_dc_chrom_val) +
                   sizeof(s_ac_lum_val) + sizeof(s_ac_chrom_val) + 16 * 4;
  write_word(&writer, (uint16_t)dht_len);

  /* DC Lum */
  write_byte(&writer, 0x00);
  for (int i = 1; i <= 16; ++i)
    write_byte(&writer, s_dc_lum_bits[i]);
  for (size_t i = 0; i < sizeof(s_dc_lum_val); ++i)
    write_byte(&writer, s_dc_lum_val[i]);

  /* DC Chrom */
  write_byte(&writer, 0x01);
  for (int i = 1; i <= 16; ++i)
    write_byte(&writer, s_dc_chrom_bits[i]);
  for (size_t i = 0; i < sizeof(s_dc_chrom_val); ++i)
    write_byte(&writer, s_dc_chrom_val[i]);

  /* AC Lum */
  write_byte(&writer, 0x10);
  for (int i = 1; i <= 16; ++i)
    write_byte(&writer, s_ac_lum_bits[i]);
  for (size_t i = 0; i < sizeof(s_ac_lum_val); ++i)
    write_byte(&writer, s_ac_lum_val[i]);

  /* AC Chrom */
  write_byte(&writer, 0x11);
  for (int i = 1; i <= 16; ++i)
    write_byte(&writer, s_ac_chrom_bits[i]);
  for (size_t i = 0; i < sizeof(s_ac_chrom_val); ++i)
    write_byte(&writer, s_ac_chrom_val[i]);

  /* SOS Marker (Start of Scan) */
  write_word(&writer, 0xffda);
  write_word(&writer, 2 + 1 + 3 * 2 + 3);
  write_byte(&writer, 3);
  write_byte(&writer, 1);
  write_byte(&writer, 0x00);
  write_byte(&writer, 2);
  write_byte(&writer, 0x11);
  write_byte(&writer, 3);
  write_byte(&writer, 0x11);
  write_byte(&writer, 0); /* Spectral select 0..63 */
  write_byte(&writer, 63);
  write_byte(&writer, 0);

  /* Process MCUs (16x16 pixels per MCU in 4:2:0: Y0, Y1, Y2, Y3, Cb, Cr) */
  int16_t prev_dc_y = 0;
  int16_t prev_dc_cb = 0;
  int16_t prev_dc_cr = 0;

  uint32_t mcu_w = (width + 15U) / 16U;
  uint32_t mcu_h = (height + 15U) / 16U;

  float y_blocks[4][64];
  float cb_block[64];
  float cr_block[64];
  float dct_out[64];

  for (uint32_t my = 0; my < mcu_h; ++my) {
    for (uint32_t mx = 0; mx < mcu_w; ++mx) {
      /* Extract 16x16 pixels and convert to YCbCr */
      for (int by = 0; by < 2; ++by) {
        for (int bx = 0; bx < 2; ++bx) {
          int block_idx = by * 2 + bx;
          for (int py = 0; py < 8; ++py) {
            for (int px = 0; px < 8; ++px) {
              uint32_t x = mx * 16U + (uint32_t)(bx * 8 + px);
              uint32_t y = my * 16U + (uint32_t)(by * 8 + py);
              if (x >= width)
                x = width - 1U;
              if (y >= height)
                y = height - 1U;

              const uint8_t *p = pixel_data + ((size_t)y * (size_t)width + x) * 4U;
              uint8_t r = is_bgra ? p[2] : p[0];
              uint8_t g = p[1];
              uint8_t b = is_bgra ? p[0] : p[2];

              /* Fast integer conversion to Y */
              float y_val = 0.299f * (float)r + 0.587f * (float)g + 0.114f * (float)b;
              y_blocks[block_idx][py * 8 + px] = y_val - 128.0f;
            }
          }
        }
      }

      /* 2x2 subsampling for Cb and Cr */
      for (int py = 0; py < 8; ++py) {
        for (int px = 0; px < 8; ++px) {
          float cb_sum = 0.0f;
          float cr_sum = 0.0f;
          for (int dy = 0; dy < 2; ++dy) {
            for (int dx = 0; dx < 2; ++dx) {
              uint32_t x = mx * 16U + (uint32_t)(px * 2 + dx);
              uint32_t y = my * 16U + (uint32_t)(py * 2 + dy);
              if (x >= width)
                x = width - 1U;
              if (y >= height)
                y = height - 1U;

              const uint8_t *p = pixel_data + ((size_t)y * (size_t)width + x) * 4U;
              uint8_t r = is_bgra ? p[2] : p[0];
              uint8_t g = p[1];
              uint8_t b = is_bgra ? p[0] : p[2];

              cb_sum += -0.168736f * (float)r - 0.331264f * (float)g + 0.5f * (float)b;
              cr_sum += 0.5f * (float)r - 0.418688f * (float)g - 0.081312f * (float)b;
            }
          }
          cb_block[py * 8 + px] = (cb_sum * 0.25f);
          cr_block[py * 8 + px] = (cr_sum * 0.25f);
        }
      }

      /* Encode 4 Y blocks */
      for (int i = 0; i < 4; ++i) {
        forward_dct_float(y_blocks[i], dct_out);
        quantize_and_encode_block(&writer, dct_out, fq_lum, &prev_dc_y,
                                  &dc_lum_ht, &ac_lum_ht);
      }

      /* Encode Cb block */
      forward_dct_float(cb_block, dct_out);
      quantize_and_encode_block(&writer, dct_out, fq_chrom, &prev_dc_cb,
                                &dc_chrom_ht, &ac_chrom_ht);

      /* Encode Cr block */
      forward_dct_float(cr_block, dct_out);
      quantize_and_encode_block(&writer, dct_out, fq_chrom, &prev_dc_cr,
                                &dc_chrom_ht, &ac_chrom_ht);
    }
  }

  flush_bits(&writer);

  /* EOI Marker */
  write_word(&writer, 0xffd9);

  *out_jpeg = writer.data;
  *out_size = writer.size;
  return 1;
}
