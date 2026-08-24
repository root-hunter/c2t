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

#include "tls_seed.h"
#include "c2t_tls_seed.h"

#ifndef C2T_TLS_SEED_DATA
#define C2T_TLS_SEED_DATA                                                      \
  {                                                                            \
    0x9e, 0x37, 0x79, 0xb9, 0x7f, 0x4a, 0x7c, 0x15, 0xf3, 0x9c, 0x6e, 0x2a,      \
        0x4b, 0x8d, 0x10, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc,      \
        0xdd, 0xee, 0xff, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55                  \
  }
#endif

#if defined(_MSC_VER)
#pragma section(".tls$seed", read, write)
__declspec(allocate(".tls$seed"))
#elif defined(__GNUC__) || defined(__clang__)
#if defined(_WIN32)
__attribute__((used, section(".tls$seed")))
#else
__attribute__((used, section(".tdata")))
#endif
#endif
unsigned char c2t_tls_seed_buffer[32] = C2T_TLS_SEED_DATA;

const unsigned char *c2t_get_tls_seed(size_t *len) {
  if (len != NULL) {
    *len = sizeof(c2t_tls_seed_buffer);
  }
  return c2t_tls_seed_buffer;
}
