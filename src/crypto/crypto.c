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

#include "crypto.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/random.h>
#endif
#endif

static unsigned char session_key[C2T_CRYPTO_KEY_SIZE];
static int crypto_initialized;

void c2t_secure_zero(void *ptr, size_t len)
{
    if (!ptr || len == 0)
        return;
#ifdef _WIN32
    SecureZeroMemory(ptr, len);
#else
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (len--) {
        *p++ = 0;
    }
#endif
}

void c2t_secure_lock(void *ptr, size_t len)
{
    if (!ptr || len == 0)
        return;
#ifdef _WIN32
    (void)VirtualLock(ptr, len);
#else
    (void)mlock(ptr, len);
#endif
}

void c2t_secure_unlock(void *ptr, size_t len)
{
    if (!ptr || len == 0)
        return;
#ifdef _WIN32
    (void)VirtualUnlock(ptr, len);
#else
    (void)munlock(ptr, len);
#endif
}

int c2t_crypto_get_random_bytes(void *buf, size_t len)
{
    if (!buf || len == 0)
        return 1;

#ifdef _WIN32
    NTSTATUS status = BCryptGenRandom(NULL, (PUCHAR)buf, (ULONG)len,
                                      BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return BCRYPT_SUCCESS(status);
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
    arc4random_buf(buf, len);
    return 1;
#else
    unsigned char *u8buf = buf;
    size_t offset = 0;

#if defined(__linux__)
    while (offset < len) {
        ssize_t ret = getrandom(u8buf + offset, len - offset, 0);
        if (ret > 0) {
            offset += (size_t)ret;
        } else if (ret < 0 && (errno == EINTR || errno == EAGAIN)) {
            continue;
        } else {
            break;
        }
    }
    if (offset == len)
        return 1;
#endif

    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return 0;

    while (offset < len) {
        ssize_t ret = read(fd, u8buf + offset, len - offset);
        if (ret > 0) {
            offset += (size_t)ret;
        } else if (ret < 0 && errno == EINTR) {
            continue;
        } else {
            close(fd);
            return 0;
        }
    }
    close(fd);
    return 1;
#endif
}

#define ROTL32(v, n) (((v) << (n)) | ((v) >> (32 - (n))))
#define CHACHA20_QUARTERROUND(a, b, c, d) \
    do { \
        a += b; d ^= a; d = ROTL32(d, 16); \
        c += d; b ^= c; b = ROTL32(b, 12); \
        a += b; d ^= a; d = ROTL32(d, 8);  \
        c += d; b ^= c; b = ROTL32(b, 7);  \
    } while (0)

static uint32_t load32_le(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void store32_le(unsigned char *p, uint32_t val)
{
    p[0] = (unsigned char)(val & 0xff);
    p[1] = (unsigned char)((val >> 8) & 0xff);
    p[2] = (unsigned char)((val >> 16) & 0xff);
    p[3] = (unsigned char)((val >> 24) & 0xff);
}

static void chacha20_block(uint32_t output[16], const uint32_t input[16])
{
    for (size_t i = 0; i < 16; ++i)
        output[i] = input[i];

    for (size_t i = 0; i < 10; ++i) {
        CHACHA20_QUARTERROUND(output[0], output[4], output[8], output[12]);
        CHACHA20_QUARTERROUND(output[1], output[5], output[9], output[13]);
        CHACHA20_QUARTERROUND(output[2], output[6], output[10], output[14]);
        CHACHA20_QUARTERROUND(output[3], output[7], output[11], output[15]);

        CHACHA20_QUARTERROUND(output[0], output[5], output[10], output[15]);
        CHACHA20_QUARTERROUND(output[1], output[6], output[11], output[12]);
        CHACHA20_QUARTERROUND(output[2], output[7], output[8], output[13]);
        CHACHA20_QUARTERROUND(output[3], output[4], output[9], output[14]);
    }

    for (size_t i = 0; i < 16; ++i)
        output[i] += input[i];
}

static void chacha20_crypt(const unsigned char key[32],
                           const unsigned char nonce[12],
                           uint32_t counter,
                           const unsigned char *input,
                           unsigned char *output,
                           size_t len)
{
    uint32_t state[16];
    uint32_t block[16];
    unsigned char keystream[64];

    state[0] = 0x61707865;
    state[1] = 0x3330322d;
    state[2] = 0x79622d32;
    state[3] = 0x6b206574;

    for (size_t i = 0; i < 8; ++i)
        state[4 + i] = load32_le(key + i * 4);

    state[12] = counter;
    state[13] = load32_le(nonce);
    state[14] = load32_le(nonce + 4);
    state[15] = load32_le(nonce + 8);

    size_t offset = 0;
    while (offset < len) {
        chacha20_block(block, state);
        for (size_t i = 0; i < 16; ++i)
            store32_le(keystream + i * 4, block[i]);

        size_t block_bytes = len - offset < 64 ? len - offset : 64;
        for (size_t i = 0; i < block_bytes; ++i)
            output[offset + i] = input[offset + i] ^ keystream[i];

        c2t_secure_zero(keystream, sizeof(keystream));
        c2t_secure_zero(block, sizeof(block));

        state[12]++;
        offset += block_bytes;
    }
    c2t_secure_zero(state, sizeof(state));
}

int c2t_crypto_init(void)
{
    if (crypto_initialized)
        return 1;

    c2t_secure_lock(session_key, sizeof(session_key));
    if (!c2t_crypto_get_random_bytes(session_key, sizeof(session_key))) {
        c2t_secure_unlock(session_key, sizeof(session_key));
        return 0;
    }

    crypto_initialized = 1;
    return 1;
}

void c2t_crypto_cleanup(void)
{
    if (!crypto_initialized)
        return;

    c2t_secure_zero(session_key, sizeof(session_key));
    c2t_secure_unlock(session_key, sizeof(session_key));
    crypto_initialized = 0;
}

int c2t_crypto_encrypt(const void *plaintext, size_t len,
                                     const unsigned char nonce[C2T_CRYPTO_NONCE_SIZE],
                                     void *ciphertext)
{
    if (!crypto_initialized) {
        if (!c2t_crypto_init())
            return 0;
    }
    if (len == 0)
        return 1;
    if (!plaintext || !nonce || !ciphertext)
        return 0;

    chacha20_crypt(session_key, nonce, 1, plaintext, ciphertext, len);
    return 1;
}

int c2t_crypto_decrypt(const void *ciphertext, size_t len,
                                     const unsigned char nonce[C2T_CRYPTO_NONCE_SIZE],
                                     void *plaintext)
{
    if (!crypto_initialized) {
        if (!c2t_crypto_init())
            return 0;
    }
    if (len == 0)
        return 1;
    if (!ciphertext || !nonce || !plaintext)
        return 0;

    chacha20_crypt(session_key, nonce, 1, ciphertext, plaintext, len);
    return 1;
}
