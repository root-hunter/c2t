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

void c2t_secure_zero(void *ptr, size_t len) {
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

void c2t_secure_lock(void *ptr, size_t len) {
  if (!ptr || len == 0)
    return;
#ifdef _WIN32
  SIZE_T min_ws = 0, max_ws = 0;
  HANDLE proc = GetCurrentProcess();
  if (GetProcessWorkingSetSize(proc, &min_ws, &max_ws)) {
    if (min_ws < len + 64 * 1024) {
      (void)SetProcessWorkingSetSize(proc, min_ws + len + 64 * 1024,
                                     max_ws + len + 64 * 1024);
    }
  }
  (void)VirtualLock(ptr, len);
#else
  (void)mlock(ptr, len);
#if defined(__linux__) && defined(MADV_DONTDUMP)
  (void)madvise(ptr, len, MADV_DONTDUMP);
#endif
#if defined(__linux__) && defined(MADV_WIPEONFORK)
  (void)madvise(ptr, len, MADV_WIPEONFORK);
#endif
#endif
}

void c2t_secure_unlock(void *ptr, size_t len) {
  if (!ptr || len == 0)
    return;
#ifdef _WIN32
  (void)VirtualUnlock(ptr, len);
#else
  (void)munlock(ptr, len);
#endif
}

int c2t_crypto_get_random_bytes(void *buf, size_t len) {
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
#define CHACHA20_QUARTERROUND(a, b, c, d)                                      \
  do {                                                                         \
    a += b;                                                                    \
    d ^= a;                                                                    \
    d = ROTL32(d, 16);                                                         \
    c += d;                                                                    \
    b ^= c;                                                                    \
    b = ROTL32(b, 12);                                                         \
    a += b;                                                                    \
    d ^= a;                                                                    \
    d = ROTL32(d, 8);                                                          \
    c += d;                                                                    \
    b ^= c;                                                                    \
    b = ROTL32(b, 7);                                                          \
  } while (0)

static uint32_t load32_le(const unsigned char *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static void store32_le(unsigned char *p, uint32_t val) {
  p[0] = (unsigned char)(val & 0xff);
  p[1] = (unsigned char)((val >> 8) & 0xff);
  p[2] = (unsigned char)((val >> 16) & 0xff);
  p[3] = (unsigned char)((val >> 24) & 0xff);
}

static void chacha20_block(uint32_t output[16], const uint32_t input[16]) {
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

static void chacha20_init_state(uint32_t state[16], const unsigned char key[32],
                                const unsigned char nonce[12],
                                uint32_t counter) {
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
}

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||             \
    defined(_M_IX86)
#include <emmintrin.h>
#define C2T_HAS_SSE2 1
#endif

static void chacha20_crypt(const unsigned char key[32],
                           const unsigned char nonce[12], uint32_t counter,
                           const unsigned char *input, unsigned char *output,
                           size_t len) {
  uint32_t state[16];
  uint32_t block[16];
  unsigned char keystream[64];

  chacha20_init_state(state, key, nonce, counter);

  size_t offset = 0;
  while (offset < len) {
    chacha20_block(block, state);
    for (size_t i = 0; i < 16; ++i)
      store32_le(keystream + i * 4, block[i]);

    size_t block_bytes = len - offset < 64 ? len - offset : 64;
#if defined(C2T_HAS_SSE2)
    if (block_bytes == 64) {
      __m128i k0 =
          _mm_loadu_si128((const __m128i *)(const void *)(keystream + 0));
      __m128i k1 =
          _mm_loadu_si128((const __m128i *)(const void *)(keystream + 16));
      __m128i k2 =
          _mm_loadu_si128((const __m128i *)(const void *)(keystream + 32));
      __m128i k3 =
          _mm_loadu_si128((const __m128i *)(const void *)(keystream + 48));

      __m128i i0 =
          _mm_loadu_si128((const __m128i *)(const void *)(input + offset + 0));
      __m128i i1 =
          _mm_loadu_si128((const __m128i *)(const void *)(input + offset + 16));
      __m128i i2 =
          _mm_loadu_si128((const __m128i *)(const void *)(input + offset + 32));
      __m128i i3 =
          _mm_loadu_si128((const __m128i *)(const void *)(input + offset + 48));

      _mm_storeu_si128((__m128i *)(void *)(output + offset + 0),
                       _mm_xor_si128(i0, k0));
      _mm_storeu_si128((__m128i *)(void *)(output + offset + 16),
                       _mm_xor_si128(i1, k1));
      _mm_storeu_si128((__m128i *)(void *)(output + offset + 32),
                       _mm_xor_si128(i2, k2));
      _mm_storeu_si128((__m128i *)(void *)(output + offset + 48),
                       _mm_xor_si128(i3, k3));
    } else
#endif
        if (block_bytes == 64) {
      for (size_t i = 0; i < 8; ++i) {
        uint64_t in_word, key_word;
        memcpy(&in_word, input + offset + i * 8, sizeof(uint64_t));
        memcpy(&key_word, keystream + i * 8, sizeof(uint64_t));
        uint64_t out_word = in_word ^ key_word;
        memcpy(output + offset + i * 8, &out_word, sizeof(uint64_t));
      }
    } else {
      size_t words = block_bytes / 8;
      for (size_t i = 0; i < words; ++i) {
        uint64_t in_word, key_word;
        memcpy(&in_word, input + offset + i * 8, sizeof(uint64_t));
        memcpy(&key_word, keystream + i * 8, sizeof(uint64_t));
        uint64_t out_word = in_word ^ key_word;
        memcpy(output + offset + i * 8, &out_word, sizeof(uint64_t));
      }
      for (size_t i = words * 8; i < block_bytes; ++i)
        output[offset + i] = input[offset + i] ^ keystream[i];
    }

    state[12]++;
    offset += block_bytes;
  }
  c2t_secure_zero(keystream, sizeof(keystream));
  c2t_secure_zero(block, sizeof(block));
  c2t_secure_zero(state, sizeof(state));
}

int c2t_crypto_init(void) {
  if (crypto_initialized)
    return 1;

#ifdef _WIN32
  (void)HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0);
  HMODULE wer_lib = LoadLibraryA("wer.dll");
  if (wer_lib) {
    typedef HRESULT(WINAPI * pfnWerSetFlags)(DWORD);
    pfnWerSetFlags set_flags =
        (pfnWerSetFlags)GetProcAddress(wer_lib, "WerSetFlags");
    if (set_flags) {
      (void)set_flags(1U);
    }
    FreeLibrary(wer_lib);
  }
#endif

  c2t_secure_lock(session_key, sizeof(session_key));
  if (!c2t_crypto_get_random_bytes(session_key, sizeof(session_key))) {
    c2t_secure_unlock(session_key, sizeof(session_key));
    return 0;
  }

  crypto_initialized = 1;
  return 1;
}

void c2t_crypto_cleanup(void) {
  if (!crypto_initialized)
    return;

  c2t_secure_zero(session_key, sizeof(session_key));
  c2t_secure_unlock(session_key, sizeof(session_key));
  crypto_initialized = 0;
}

int c2t_crypto_encrypt(const void *plaintext, size_t len,
                       const unsigned char nonce[C2T_CRYPTO_NONCE_SIZE],
                       void *ciphertext) {
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
                       void *plaintext) {
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

static const unsigned char persistent_state_key[C2T_CRYPTO_KEY_SIZE] = {
    0x8f, 0x1d, 0x4e, 0x93, 0x6a, 0x2b, 0x5c, 0x71, 0x3e, 0x09, 0xba,
    0xd4, 0x2f, 0x88, 0x19, 0xc3, 0x77, 0x51, 0x9a, 0x42, 0xe6, 0x3d,
    0x1b, 0x68, 0x54, 0x0e, 0x82, 0xbf, 0x33, 0x7a, 0x9c, 0xd0};

int c2t_crypto_state_encrypt(const void *plaintext, size_t len,
                             const unsigned char nonce[C2T_CRYPTO_NONCE_SIZE],
                             void *ciphertext) {
  if (len == 0)
    return 1;
  if (!plaintext || !nonce || !ciphertext)
    return 0;
  chacha20_crypt(persistent_state_key, nonce, 1, plaintext, ciphertext, len);
  return 1;
}

int c2t_crypto_state_decrypt(const void *ciphertext, size_t len,
                             const unsigned char nonce[C2T_CRYPTO_NONCE_SIZE],
                             void *plaintext) {
  if (len == 0)
    return 1;
  if (!ciphertext || !nonce || !plaintext)
    return 0;
  chacha20_crypt(persistent_state_key, nonce, 1, ciphertext, plaintext, len);
  return 1;
}

int c2t_crypto_decrypt_offset(const void *ciphertext, size_t offset, size_t len,
                              const unsigned char nonce[C2T_CRYPTO_NONCE_SIZE],
                              void *plaintext) {
  if (!crypto_initialized) {
    if (!c2t_crypto_init())
      return 0;
  }
  if (len == 0)
    return 1;
  if (!ciphertext || !nonce || !plaintext)
    return 0;

  uint32_t initial_counter = 1 + (uint32_t)(offset / 64);
  size_t skip = offset % 64;

  if (skip == 0) {
    chacha20_crypt(session_key, nonce, initial_counter, ciphertext, plaintext,
                   len);
  } else {
    uint32_t state[16], block[16];
    unsigned char keystream[64];

    chacha20_init_state(state, session_key, nonce, initial_counter);

    chacha20_block(block, state);
    for (size_t i = 0; i < 16; ++i)
      store32_le(keystream + i * 4, block[i]);

    const unsigned char *in_bytes = (const unsigned char *)ciphertext;
    unsigned char *out_bytes = (unsigned char *)plaintext;

    size_t first_avail = 64 - skip;
    size_t first_chunk = len < first_avail ? len : first_avail;
    size_t first_words = first_chunk / 8;
    for (size_t i = 0; i < first_words; ++i) {
      uint64_t in_w, key_w;
      memcpy(&in_w, in_bytes + i * 8, sizeof(uint64_t));
      memcpy(&key_w, keystream + skip + i * 8, sizeof(uint64_t));
      uint64_t out_w = in_w ^ key_w;
      memcpy(out_bytes + i * 8, &out_w, sizeof(uint64_t));
    }
    for (size_t i = first_words * 8; i < first_chunk; ++i)
      out_bytes[i] = in_bytes[i] ^ keystream[skip + i];

    c2t_secure_zero(keystream, sizeof(keystream));
    c2t_secure_zero(block, sizeof(block));
    c2t_secure_zero(state, sizeof(state));

    if (len > first_chunk) {
      chacha20_crypt(session_key, nonce, initial_counter + 1,
                     in_bytes + first_chunk, out_bytes + first_chunk,
                     len - first_chunk);
    }
  }
  return 1;
}

void c2t_encrypted_stream_init(c2t_encrypted_stream_t *stream,
                               const char *prefix, size_t prefix_len,
                               const unsigned char *ciphertext,
                               size_t ciphertext_len,
                               const unsigned char nonce[C2T_CRYPTO_NONCE_SIZE],
                               const char *suffix, size_t suffix_len) {
  if (!stream)
    return;
  stream->prefix = prefix;
  stream->prefix_len = prefix_len;
  stream->ciphertext = ciphertext;
  stream->ciphertext_len = ciphertext_len;
  if (nonce)
    memcpy(stream->nonce, nonce, C2T_CRYPTO_NONCE_SIZE);
  else
    memset(stream->nonce, 0, C2T_CRYPTO_NONCE_SIZE);
  stream->suffix = suffix;
  stream->suffix_len = suffix_len;
  stream->offset = 0;
}

size_t c2t_encrypted_stream_read(void *user_data, void *buffer,
                                 size_t max_len) {
  c2t_encrypted_stream_t *stream = (c2t_encrypted_stream_t *)user_data;
  if (!stream || !buffer || max_len == 0)
    return 0;

  size_t total_written = 0;
  unsigned char *out = (unsigned char *)buffer;

  while (total_written < max_len) {
    size_t current = stream->offset;
    size_t remaining_wanted = max_len - total_written;

    if (current < stream->prefix_len) {
      size_t avail = stream->prefix_len - current;
      size_t chunk = remaining_wanted < avail ? remaining_wanted : avail;
      memcpy(out + total_written, stream->prefix + current, chunk);
      stream->offset += chunk;
      total_written += chunk;
    } else if (current < stream->prefix_len + stream->ciphertext_len) {
      size_t cipher_offset = current - stream->prefix_len;
      size_t avail = stream->ciphertext_len - cipher_offset;
      size_t chunk = remaining_wanted < avail ? remaining_wanted : avail;

      if (!c2t_crypto_decrypt_offset(stream->ciphertext + cipher_offset,
                                     cipher_offset, chunk, stream->nonce,
                                     out + total_written)) {
        break;
      }
      stream->offset += chunk;
      total_written += chunk;
    } else if (current < stream->prefix_len + stream->ciphertext_len +
                             stream->suffix_len) {
      size_t suffix_offset =
          current - stream->prefix_len - stream->ciphertext_len;
      size_t avail = stream->suffix_len - suffix_offset;
      size_t chunk = remaining_wanted < avail ? remaining_wanted : avail;
      memcpy(out + total_written, stream->suffix + suffix_offset, chunk);
      stream->offset += chunk;
      total_written += chunk;
    } else {
      break;
    }
  }

  return total_written;
}
