/*
 * Copyright (C) 2026 roothunter
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <stddef.h>
#include <stdint.h>

#if defined(C2T_MSVC_X86_SIMD)
#include <immintrin.h>

#if !defined(C2T_MSVC_AVX512_OBJECT)
#define ROTL32_AVX2(v, n)                                                     \
  _mm256_or_si256(_mm256_slli_epi32((v), (n)),                               \
                  _mm256_srli_epi32((v), 32 - (n)))
#define ROTL16_AVX2(v)                                                         \
  _mm256_shuffle_epi8(                                                        \
      (v), _mm256_setr_epi8(2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15,   \
                             12, 13, 2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9,   \
                             14, 15, 12, 13))
#define ROTL8_AVX2(v)                                                          \
  _mm256_shuffle_epi8(                                                        \
      (v), _mm256_setr_epi8(3, 0, 1, 2, 7, 4, 5, 6, 11, 8, 9, 10, 15, 12,   \
                             13, 14, 3, 0, 1, 2, 7, 4, 5, 6, 11, 8, 9, 10,   \
                             15, 12, 13, 14))
#define CHACHA20_QUARTERROUND_AVX2(a, b, c, d)                                \
  do {                                                                         \
    (a) = _mm256_add_epi32((a), (b));                                          \
    (d) = ROTL16_AVX2(_mm256_xor_si256((d), (a)));                            \
    (c) = _mm256_add_epi32((c), (d));                                          \
    (b) = ROTL32_AVX2(_mm256_xor_si256((b), (c)), 12);                        \
    (a) = _mm256_add_epi32((a), (b));                                          \
    (d) = ROTL8_AVX2(_mm256_xor_si256((d), (a)));                             \
    (c) = _mm256_add_epi32((c), (d));                                          \
    (b) = ROTL32_AVX2(_mm256_xor_si256((b), (c)), 7);                         \
  } while (0)

static inline void chacha20_xor_8words(__m256i x0, __m256i x1, __m256i x2,
                                        __m256i x3,
                                        const unsigned char *input,
                                        unsigned char *output) {
  __m256i t0 = _mm256_unpacklo_epi32(x0, x1);
  __m256i t1 = _mm256_unpackhi_epi32(x0, x1);
  __m256i t2 = _mm256_unpacklo_epi32(x2, x3);
  __m256i t3 = _mm256_unpackhi_epi32(x2, x3);
  __m256i b04 = _mm256_unpacklo_epi64(t0, t2);
  __m256i b15 = _mm256_unpackhi_epi64(t0, t2);
  __m256i b26 = _mm256_unpacklo_epi64(t1, t3);
  __m256i b37 = _mm256_unpackhi_epi64(t1, t3);
  __m128i blocks[8] = {
      _mm256_castsi256_si128(b04),      _mm256_castsi256_si128(b15),
      _mm256_castsi256_si128(b26),      _mm256_castsi256_si128(b37),
      _mm256_extracti128_si256(b04, 1), _mm256_extracti128_si256(b15, 1),
      _mm256_extracti128_si256(b26, 1), _mm256_extracti128_si256(b37, 1)};

  for (size_t block_index = 0; block_index < 8; ++block_index) {
    size_t block_offset = block_index * 64;
    __m128i in = _mm_loadu_si128(
        (const __m128i *)(const void *)(input + block_offset));
    _mm_storeu_si128((__m128i *)(void *)(output + block_offset),
                     _mm_xor_si128(in, blocks[block_index]));
  }
}

void c2t_chacha20_crypt_8blocks(const uint32_t state[16],
                                const unsigned char *input,
                                unsigned char *output) {
#define BROADCAST_AVX2(n) __m256i x##n = _mm256_set1_epi32((int)state[n])
  BROADCAST_AVX2(0);
  BROADCAST_AVX2(1);
  BROADCAST_AVX2(2);
  BROADCAST_AVX2(3);
  BROADCAST_AVX2(4);
  BROADCAST_AVX2(5);
  BROADCAST_AVX2(6);
  BROADCAST_AVX2(7);
  BROADCAST_AVX2(8);
  BROADCAST_AVX2(9);
  BROADCAST_AVX2(10);
  BROADCAST_AVX2(11);
#undef BROADCAST_AVX2
  __m256i x12 = _mm256_set_epi32(
      (int)(state[12] + 7U), (int)(state[12] + 6U),
      (int)(state[12] + 5U), (int)(state[12] + 4U),
      (int)(state[12] + 3U), (int)(state[12] + 2U),
      (int)(state[12] + 1U), (int)state[12]);
  __m256i x13 = _mm256_set1_epi32((int)state[13]);
  __m256i x14 = _mm256_set1_epi32((int)state[14]);
  __m256i x15 = _mm256_set1_epi32((int)state[15]);

  for (size_t i = 0; i < 10; ++i) {
    CHACHA20_QUARTERROUND_AVX2(x0, x4, x8, x12);
    CHACHA20_QUARTERROUND_AVX2(x1, x5, x9, x13);
    CHACHA20_QUARTERROUND_AVX2(x2, x6, x10, x14);
    CHACHA20_QUARTERROUND_AVX2(x3, x7, x11, x15);
    CHACHA20_QUARTERROUND_AVX2(x0, x5, x10, x15);
    CHACHA20_QUARTERROUND_AVX2(x1, x6, x11, x12);
    CHACHA20_QUARTERROUND_AVX2(x2, x7, x8, x13);
    CHACHA20_QUARTERROUND_AVX2(x3, x4, x9, x14);
  }

#define ADD_ORIGINAL_AVX2(n)                                                   \
  x##n = _mm256_add_epi32(x##n, _mm256_set1_epi32((int)state[n]))
  ADD_ORIGINAL_AVX2(0);
  ADD_ORIGINAL_AVX2(1);
  ADD_ORIGINAL_AVX2(2);
  ADD_ORIGINAL_AVX2(3);
  ADD_ORIGINAL_AVX2(4);
  ADD_ORIGINAL_AVX2(5);
  ADD_ORIGINAL_AVX2(6);
  ADD_ORIGINAL_AVX2(7);
  ADD_ORIGINAL_AVX2(8);
  ADD_ORIGINAL_AVX2(9);
  ADD_ORIGINAL_AVX2(10);
  ADD_ORIGINAL_AVX2(11);
  x12 = _mm256_add_epi32(
      x12, _mm256_set_epi32(
               (int)(state[12] + 7U), (int)(state[12] + 6U),
               (int)(state[12] + 5U), (int)(state[12] + 4U),
               (int)(state[12] + 3U), (int)(state[12] + 2U),
               (int)(state[12] + 1U), (int)state[12]));
  ADD_ORIGINAL_AVX2(13);
  ADD_ORIGINAL_AVX2(14);
  ADD_ORIGINAL_AVX2(15);
#undef ADD_ORIGINAL_AVX2

  chacha20_xor_8words(x0, x1, x2, x3, input + 0, output + 0);
  chacha20_xor_8words(x4, x5, x6, x7, input + 16, output + 16);
  chacha20_xor_8words(x8, x9, x10, x11, input + 32, output + 32);
  chacha20_xor_8words(x12, x13, x14, x15, input + 48, output + 48);
  _mm256_zeroupper();
}

#else
#define ROTL32_AVX512(v, n)                                                    \
  _mm512_or_si512(_mm512_slli_epi32((v), (n)),                               \
                  _mm512_srli_epi32((v), 32 - (n)))
#define CHACHA20_QUARTERROUND_AVX512(a, b, c, d)                              \
  do {                                                                         \
    (a) = _mm512_add_epi32((a), (b));                                          \
    (d) = ROTL32_AVX512(_mm512_xor_si512((d), (a)), 16);                      \
    (c) = _mm512_add_epi32((c), (d));                                          \
    (b) = ROTL32_AVX512(_mm512_xor_si512((b), (c)), 12);                      \
    (a) = _mm512_add_epi32((a), (b));                                          \
    (d) = ROTL32_AVX512(_mm512_xor_si512((d), (a)), 8);                       \
    (c) = _mm512_add_epi32((c), (d));                                          \
    (b) = ROTL32_AVX512(_mm512_xor_si512((b), (c)), 7);                       \
  } while (0)
#define CHACHA20_COUNTERS_AVX512(counter)                                      \
  _mm512_set_epi32(                                                            \
      (int)((counter) + 15U), (int)((counter) + 14U),                         \
      (int)((counter) + 13U), (int)((counter) + 12U),                         \
      (int)((counter) + 11U), (int)((counter) + 10U),                         \
      (int)((counter) + 9U), (int)((counter) + 8U),                           \
      (int)((counter) + 7U), (int)((counter) + 6U),                           \
      (int)((counter) + 5U), (int)((counter) + 4U),                           \
      (int)((counter) + 3U), (int)((counter) + 2U),                           \
      (int)((counter) + 1U), (int)(counter))

static inline void chacha20_xor_16words(__m512i x0, __m512i x1, __m512i x2,
                                         __m512i x3,
                                         const unsigned char *input,
                                         unsigned char *output) {
  __m512i t0 = _mm512_unpacklo_epi32(x0, x1);
  __m512i t1 = _mm512_unpackhi_epi32(x0, x1);
  __m512i t2 = _mm512_unpacklo_epi32(x2, x3);
  __m512i t3 = _mm512_unpackhi_epi32(x2, x3);
  __m512i b0 = _mm512_unpacklo_epi64(t0, t2);
  __m512i b1 = _mm512_unpackhi_epi64(t0, t2);
  __m512i b2 = _mm512_unpacklo_epi64(t1, t3);
  __m512i b3 = _mm512_unpackhi_epi64(t1, t3);

#define XOR_AVX512_BLOCK(block, vector, lane)                                 \
  do {                                                                         \
    __m128i words = _mm512_extracti32x4_epi32((vector), (lane));              \
    __m128i in = _mm_loadu_si128(                                              \
        (const __m128i *)(const void *)(input + (block) * 64));                \
    _mm_storeu_si128((__m128i *)(void *)(output + (block) * 64),               \
                     _mm_xor_si128(in, words));                                \
  } while (0)
  XOR_AVX512_BLOCK(0, b0, 0);
  XOR_AVX512_BLOCK(1, b1, 0);
  XOR_AVX512_BLOCK(2, b2, 0);
  XOR_AVX512_BLOCK(3, b3, 0);
  XOR_AVX512_BLOCK(4, b0, 1);
  XOR_AVX512_BLOCK(5, b1, 1);
  XOR_AVX512_BLOCK(6, b2, 1);
  XOR_AVX512_BLOCK(7, b3, 1);
  XOR_AVX512_BLOCK(8, b0, 2);
  XOR_AVX512_BLOCK(9, b1, 2);
  XOR_AVX512_BLOCK(10, b2, 2);
  XOR_AVX512_BLOCK(11, b3, 2);
  XOR_AVX512_BLOCK(12, b0, 3);
  XOR_AVX512_BLOCK(13, b1, 3);
  XOR_AVX512_BLOCK(14, b2, 3);
  XOR_AVX512_BLOCK(15, b3, 3);
#undef XOR_AVX512_BLOCK
}

void c2t_chacha20_crypt_16blocks(const uint32_t state[16],
                                 const unsigned char *input,
                                 unsigned char *output) {
#define BROADCAST_AVX512(n) __m512i x##n = _mm512_set1_epi32((int)state[n])
  BROADCAST_AVX512(0);
  BROADCAST_AVX512(1);
  BROADCAST_AVX512(2);
  BROADCAST_AVX512(3);
  BROADCAST_AVX512(4);
  BROADCAST_AVX512(5);
  BROADCAST_AVX512(6);
  BROADCAST_AVX512(7);
  BROADCAST_AVX512(8);
  BROADCAST_AVX512(9);
  BROADCAST_AVX512(10);
  BROADCAST_AVX512(11);
#undef BROADCAST_AVX512
  __m512i x12 = CHACHA20_COUNTERS_AVX512(state[12]);
  __m512i x13 = _mm512_set1_epi32((int)state[13]);
  __m512i x14 = _mm512_set1_epi32((int)state[14]);
  __m512i x15 = _mm512_set1_epi32((int)state[15]);

  for (size_t i = 0; i < 10; ++i) {
    CHACHA20_QUARTERROUND_AVX512(x0, x4, x8, x12);
    CHACHA20_QUARTERROUND_AVX512(x1, x5, x9, x13);
    CHACHA20_QUARTERROUND_AVX512(x2, x6, x10, x14);
    CHACHA20_QUARTERROUND_AVX512(x3, x7, x11, x15);
    CHACHA20_QUARTERROUND_AVX512(x0, x5, x10, x15);
    CHACHA20_QUARTERROUND_AVX512(x1, x6, x11, x12);
    CHACHA20_QUARTERROUND_AVX512(x2, x7, x8, x13);
    CHACHA20_QUARTERROUND_AVX512(x3, x4, x9, x14);
  }

  const volatile uint32_t *original = state;
#define ADD_ORIGINAL_AVX512(n)                                                \
  x##n = _mm512_add_epi32(x##n, _mm512_set1_epi32((int)original[n]))
  ADD_ORIGINAL_AVX512(0);
  ADD_ORIGINAL_AVX512(1);
  ADD_ORIGINAL_AVX512(2);
  ADD_ORIGINAL_AVX512(3);
  ADD_ORIGINAL_AVX512(4);
  ADD_ORIGINAL_AVX512(5);
  ADD_ORIGINAL_AVX512(6);
  ADD_ORIGINAL_AVX512(7);
  ADD_ORIGINAL_AVX512(8);
  ADD_ORIGINAL_AVX512(9);
  ADD_ORIGINAL_AVX512(10);
  ADD_ORIGINAL_AVX512(11);
  uint32_t final_counter = original[12];
  x12 = _mm512_add_epi32(x12, CHACHA20_COUNTERS_AVX512(final_counter));
  ADD_ORIGINAL_AVX512(13);
  ADD_ORIGINAL_AVX512(14);
  ADD_ORIGINAL_AVX512(15);
#undef ADD_ORIGINAL_AVX512

  chacha20_xor_16words(x0, x1, x2, x3, input + 0, output + 0);
  chacha20_xor_16words(x4, x5, x6, x7, input + 16, output + 16);
  chacha20_xor_16words(x8, x9, x10, x11, input + 32, output + 32);
  chacha20_xor_16words(x12, x13, x14, x15, input + 48, output + 48);
  _mm256_zeroupper();
}
#endif

#endif
