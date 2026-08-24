/*
 * Copyright (C) 2026 roothunter
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

/* This file is compiled separately with -march=armv8-a+sve2.  Do not call
 * these functions until Linux HWCAP2_SVE2 has been checked by crypto.c. */
#include <arm_sve.h>
#include <stddef.h>
#include <stdint.h>

#define CHACHA20_QUARTERROUND_SVE(pg, a, b, c, d)                             \
  do {                                                                         \
    (a) = svadd_u32_x((pg), (a), (b));                                         \
    (d) = svxar_n_u32((d), (a), 16);                                           \
    (c) = svadd_u32_x((pg), (c), (d));                                         \
    (b) = svxar_n_u32((b), (c), 20);                                           \
    (a) = svadd_u32_x((pg), (a), (b));                                         \
    (d) = svxar_n_u32((d), (a), 24);                                           \
    (c) = svadd_u32_x((pg), (c), (d));                                         \
    (b) = svxar_n_u32((b), (c), 25);                                           \
  } while (0)

size_t c2t_chacha20_sve2_block_count(void) { return svcntw(); }

void c2t_chacha20_crypt_sve2(const uint32_t state[16],
                              const unsigned char *input,
                              unsigned char *output) {
  const svbool_t pg = svptrue_b32();

#define C2T_SVE_DUP(n) svuint32_t x##n = svdup_u32(state[n])
  C2T_SVE_DUP(0);
  C2T_SVE_DUP(1);
  C2T_SVE_DUP(2);
  C2T_SVE_DUP(3);
  C2T_SVE_DUP(4);
  C2T_SVE_DUP(5);
  C2T_SVE_DUP(6);
  C2T_SVE_DUP(7);
  C2T_SVE_DUP(8);
  C2T_SVE_DUP(9);
  C2T_SVE_DUP(10);
  C2T_SVE_DUP(11);
  svuint32_t x12 = svindex_u32(state[12], 1);
  C2T_SVE_DUP(13);
  C2T_SVE_DUP(14);
  C2T_SVE_DUP(15);
#undef C2T_SVE_DUP

  for (size_t round = 0; round < 10; ++round) {
    CHACHA20_QUARTERROUND_SVE(pg, x0, x4, x8, x12);
    CHACHA20_QUARTERROUND_SVE(pg, x1, x5, x9, x13);
    CHACHA20_QUARTERROUND_SVE(pg, x2, x6, x10, x14);
    CHACHA20_QUARTERROUND_SVE(pg, x3, x7, x11, x15);
    CHACHA20_QUARTERROUND_SVE(pg, x0, x5, x10, x15);
    CHACHA20_QUARTERROUND_SVE(pg, x1, x6, x11, x12);
    CHACHA20_QUARTERROUND_SVE(pg, x2, x7, x8, x13);
    CHACHA20_QUARTERROUND_SVE(pg, x3, x4, x9, x14);
  }

#define C2T_SVE_ADD_ORIGINAL(n)                                                \
  x##n = svadd_u32_x(pg, x##n, svdup_u32(state[n]))
  C2T_SVE_ADD_ORIGINAL(0);
  C2T_SVE_ADD_ORIGINAL(1);
  C2T_SVE_ADD_ORIGINAL(2);
  C2T_SVE_ADD_ORIGINAL(3);
  C2T_SVE_ADD_ORIGINAL(4);
  C2T_SVE_ADD_ORIGINAL(5);
  C2T_SVE_ADD_ORIGINAL(6);
  C2T_SVE_ADD_ORIGINAL(7);
  C2T_SVE_ADD_ORIGINAL(8);
  C2T_SVE_ADD_ORIGINAL(9);
  C2T_SVE_ADD_ORIGINAL(10);
  C2T_SVE_ADD_ORIGINAL(11);
  x12 = svadd_u32_x(pg, x12, svindex_u32(state[12], 1));
  C2T_SVE_ADD_ORIGINAL(13);
  C2T_SVE_ADD_ORIGINAL(14);
  C2T_SVE_ADD_ORIGINAL(15);
#undef C2T_SVE_ADD_ORIGINAL

  /* Each lane represents one block.  Gather/scatter avoids a scalar state
   * transpose and keeps the complete XOR path vectorized, including in-place
   * operation.  Offsets select the same word from consecutive 64-byte blocks. */
#define C2T_SVE_XOR_WORD(n)                                                    \
  do {                                                                         \
    const svuint32_t offsets = svindex_u32((n) * 4U, 64U);                    \
    const svuint32_t in = svld1_gather_u32offset_u32(                          \
        pg, (const uint32_t *)(const void *)input, offsets);                   \
    svst1_scatter_u32offset_u32(                                               \
        pg, (uint32_t *)(void *)output, offsets, sveor_u32_x(pg, in, x##n));   \
  } while (0)
  C2T_SVE_XOR_WORD(0);
  C2T_SVE_XOR_WORD(1);
  C2T_SVE_XOR_WORD(2);
  C2T_SVE_XOR_WORD(3);
  C2T_SVE_XOR_WORD(4);
  C2T_SVE_XOR_WORD(5);
  C2T_SVE_XOR_WORD(6);
  C2T_SVE_XOR_WORD(7);
  C2T_SVE_XOR_WORD(8);
  C2T_SVE_XOR_WORD(9);
  C2T_SVE_XOR_WORD(10);
  C2T_SVE_XOR_WORD(11);
  C2T_SVE_XOR_WORD(12);
  C2T_SVE_XOR_WORD(13);
  C2T_SVE_XOR_WORD(14);
  C2T_SVE_XOR_WORD(15);
#undef C2T_SVE_XOR_WORD
}
