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

#ifndef C2T_CRYPTO_H
#define C2T_CRYPTO_H

#include <stddef.h>

#define C2T_CRYPTO_KEY_SIZE 32U
#define C2T_CRYPTO_NONCE_SIZE 12U

typedef size_t (*c2t_stream_read_fn)(void *user_data, void *buffer,
                                     size_t max_len);

typedef struct {
  c2t_stream_read_fn read;
  size_t total_size;
  void *user_data;
} c2t_stream_t;

typedef struct {
  const char *prefix;
  size_t prefix_len;
  const unsigned char *ciphertext;
  size_t ciphertext_len;
  unsigned char nonce[C2T_CRYPTO_NONCE_SIZE];
  const char *suffix;
  size_t suffix_len;
  size_t offset;
} c2t_encrypted_stream_t;

[[nodiscard]] int c2t_crypto_init(void);
void c2t_crypto_cleanup(void);

void c2t_secure_zero(void *ptr, size_t len);
void c2t_secure_lock(void *ptr, size_t len);
void c2t_secure_unlock(void *ptr, size_t len);

[[nodiscard]] int c2t_crypto_get_random_bytes(void *buf, size_t len);

[[nodiscard]] int
c2t_crypto_encrypt(const void *plaintext, size_t len,
                   const unsigned char nonce[C2T_CRYPTO_NONCE_SIZE],
                   void *ciphertext);

[[nodiscard]] int
c2t_crypto_decrypt(const void *ciphertext, size_t len,
                   const unsigned char nonce[C2T_CRYPTO_NONCE_SIZE],
                   void *plaintext);

[[nodiscard]] int
c2t_crypto_decrypt_offset(const void *ciphertext, size_t offset, size_t len,
                          const unsigned char nonce[C2T_CRYPTO_NONCE_SIZE],
                          void *plaintext);

void c2t_encrypted_stream_init(c2t_encrypted_stream_t *stream,
                               const char *prefix, size_t prefix_len,
                               const unsigned char *ciphertext,
                               size_t ciphertext_len,
                               const unsigned char nonce[C2T_CRYPTO_NONCE_SIZE],
                               const char *suffix, size_t suffix_len);

size_t c2t_encrypted_stream_read(void *user_data, void *buffer, size_t max_len);

#endif
