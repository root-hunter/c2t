/*
 * Copyright (C) 2026 Antonio Ricciardi
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

[[nodiscard]] int c2t_crypto_init(void);
void c2t_crypto_cleanup(void);

void c2t_secure_zero(void *ptr, size_t len);
void c2t_secure_lock(void *ptr, size_t len);
void c2t_secure_unlock(void *ptr, size_t len);

[[nodiscard]] int c2t_crypto_get_random_bytes(void *buf, size_t len);

[[nodiscard]] int c2t_crypto_encrypt(const void *plaintext, size_t len,
                                     const unsigned char nonce[C2T_CRYPTO_NONCE_SIZE],
                                     void *ciphertext);

[[nodiscard]] int c2t_crypto_decrypt(const void *ciphertext, size_t len,
                                     const unsigned char nonce[C2T_CRYPTO_NONCE_SIZE],
                                     void *plaintext);

#endif
