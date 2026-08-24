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

#if defined(C2T_ARM_SVE2_SIMD)
#include <asm/hwcap.h>
#include <sys/auxv.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include "../win32/win32_api.h"

static NTSTATUS c2t_BCryptGenRandom(BCRYPT_ALG_HANDLE hAlgorithm,
                                     PUCHAR pbBuffer, ULONG cbBuffer,
                                     ULONG dwFlags) {
  c2t_win32_api_init();
  if (g_c2t_win32.BCryptGenRandom)
    return g_c2t_win32.BCryptGenRandom(hAlgorithm, pbBuffer, cbBuffer, dwFlags);
  return (NTSTATUS)0xC0000001L; /* STATUS_UNSUCCESSFUL */
}
static HANDLE c2t_GetCurrentProcess(VOID) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetCurrentProcess)
    return g_c2t_win32.GetCurrentProcess();
  return NULL;
}
static BOOL c2t_GetProcessWorkingSetSize(HANDLE hProcess,
                                          PSIZE_T lpMinimumWorkingSetSize,
                                          PSIZE_T lpMaximumWorkingSetSize) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetProcessWorkingSetSize)
    return g_c2t_win32.GetProcessWorkingSetSize(
        hProcess, lpMinimumWorkingSetSize, lpMaximumWorkingSetSize);
  return FALSE;
}
static BOOL c2t_SetProcessWorkingSetSize(HANDLE hProcess,
                                          SIZE_T dwMinimumWorkingSetSize,
                                          SIZE_T dwMaximumWorkingSetSize) {
  c2t_win32_api_init();
  if (g_c2t_win32.SetProcessWorkingSetSize)
    return g_c2t_win32.SetProcessWorkingSetSize(
        hProcess, dwMinimumWorkingSetSize, dwMaximumWorkingSetSize);
  return FALSE;
}
static BOOL c2t_VirtualLock(LPVOID lpAddress, SIZE_T dwSize) {
  c2t_win32_api_init();
  if (g_c2t_win32.VirtualLock)
    return g_c2t_win32.VirtualLock(lpAddress, dwSize);
  return FALSE;
}
static BOOL c2t_VirtualUnlock(LPVOID lpAddress, SIZE_T dwSize) {
  c2t_win32_api_init();
  if (g_c2t_win32.VirtualUnlock)
    return g_c2t_win32.VirtualUnlock(lpAddress, dwSize);
  return FALSE;
}
static HMODULE c2t_LoadLibraryA(LPCSTR lpLibFileName) {
  c2t_win32_api_init();
  if (g_c2t_win32.LoadLibraryA)
    return g_c2t_win32.LoadLibraryA(lpLibFileName);
  return NULL;
}
static FARPROC c2t_GetProcAddress(HMODULE hModule, LPCSTR lpProcName) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetProcAddress)
    return g_c2t_win32.GetProcAddress(hModule, lpProcName);
  return NULL;
}
static BOOL c2t_FreeLibrary(HMODULE hLibModule) {
  c2t_win32_api_init();
  if (g_c2t_win32.FreeLibrary)
    return g_c2t_win32.FreeLibrary(hLibModule);
  return FALSE;
}
static BOOL c2t_HeapSetInformation(
    HANDLE HeapHandle, HEAP_INFORMATION_CLASS HeapInformationClass,
    PVOID HeapInformation, SIZE_T HeapInformationLength) {
  c2t_win32_api_init();
  if (g_c2t_win32.HeapSetInformation)
    return g_c2t_win32.HeapSetInformation(HeapHandle, HeapInformationClass,
                                          HeapInformation,
                                          HeapInformationLength);
  return FALSE;
}

#define BCryptGenRandom c2t_BCryptGenRandom
#define GetCurrentProcess c2t_GetCurrentProcess
#define GetProcessWorkingSetSize c2t_GetProcessWorkingSetSize
#define SetProcessWorkingSetSize c2t_SetProcessWorkingSetSize
#define VirtualLock c2t_VirtualLock
#define VirtualUnlock c2t_VirtualUnlock
#define LoadLibraryA c2t_LoadLibraryA
#define GetProcAddress c2t_GetProcAddress
#define FreeLibrary c2t_FreeLibrary
#define HeapSetInformation c2t_HeapSetInformation
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

#if defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define C2T_HAS_NEON 1
#endif

#if defined(C2T_HAS_SSE2) &&                                                   \
    (defined(__GNUC__) || defined(__clang__) || defined(__AVX2__))
#include <immintrin.h>
#define C2T_HAS_AVX2_DISPATCH 1
#define C2T_HAS_AVX2_KERNEL 1
#if defined(__GNUC__) || defined(__clang__)
#define C2T_TARGET_AVX2 __attribute__((target("avx2")))
#else
#define C2T_TARGET_AVX2
#endif
#endif

#if defined(C2T_HAS_SSE2) &&                                                   \
    (defined(__GNUC__) || defined(__clang__) || defined(__AVX512F__))
#if !defined(C2T_HAS_AVX2_DISPATCH)
#include <immintrin.h>
#endif
#define C2T_HAS_AVX512_DISPATCH 1
#define C2T_HAS_AVX512_KERNEL 1
#if defined(__GNUC__) || defined(__clang__)
#define C2T_TARGET_AVX512 __attribute__((target("avx512f")))
#else
#define C2T_TARGET_AVX512
#endif
#endif

#if defined(C2T_MSVC_X86_SIMD)
#include <immintrin.h>
#define C2T_HAS_AVX2_DISPATCH 1
#define C2T_HAS_AVX512_DISPATCH 1
void c2t_chacha20_crypt_8blocks(const uint32_t state[16],
                                const unsigned char *input,
                                unsigned char *output);
void c2t_chacha20_crypt_16blocks(const uint32_t state[16],
                                 const unsigned char *input,
                                 unsigned char *output);
#endif

#if defined(C2T_ARM_SVE2_SIMD)
size_t c2t_chacha20_sve2_block_count(void);
void c2t_chacha20_crypt_sve2(const uint32_t state[16],
                              const unsigned char *input,
                              unsigned char *output);

static int chacha20_has_sve2(void) {
#if defined(HWCAP_SVE) && defined(HWCAP2_SVE2)
  return (getauxval(AT_HWCAP) & HWCAP_SVE) != 0 &&
         (getauxval(AT_HWCAP2) & HWCAP2_SVE2) != 0;
#else
  return 0;
#endif
}
#endif

#if defined(C2T_HAS_SSE2)
#define ROTL32_SSE2(v, n)                                                     \
  _mm_or_si128(_mm_slli_epi32((v), (n)), _mm_srli_epi32((v), 32 - (n)))
#define ROTL16_SSE2(v)                                                         \
  _mm_shufflehi_epi16(                                                        \
      _mm_shufflelo_epi16((v), _MM_SHUFFLE(2, 3, 0, 1)),                     \
      _MM_SHUFFLE(2, 3, 0, 1))
#define CHACHA20_QUARTERROUND_SSE2(a, b, c, d)                                \
  do {                                                                         \
    (a) = _mm_add_epi32((a), (b));                                             \
    (d) = ROTL16_SSE2(_mm_xor_si128((d), (a)));                               \
    (c) = _mm_add_epi32((c), (d));                                             \
    (b) = ROTL32_SSE2(_mm_xor_si128((b), (c)), 12);                           \
    (a) = _mm_add_epi32((a), (b));                                             \
    (d) = ROTL32_SSE2(_mm_xor_si128((d), (a)), 8);                            \
    (c) = _mm_add_epi32((c), (d));                                             \
    (b) = ROTL32_SSE2(_mm_xor_si128((b), (c)), 7);                            \
  } while (0)

static inline void chacha20_xor_4words(__m128i x0, __m128i x1, __m128i x2,
                                        __m128i x3,
                                        const unsigned char *input,
                                        unsigned char *output) {
  __m128i t0 = _mm_unpacklo_epi32(x0, x1);
  __m128i t1 = _mm_unpackhi_epi32(x0, x1);
  __m128i t2 = _mm_unpacklo_epi32(x2, x3);
  __m128i t3 = _mm_unpackhi_epi32(x2, x3);
  __m128i b0 = _mm_unpacklo_epi64(t0, t2);
  __m128i b1 = _mm_unpackhi_epi64(t0, t2);
  __m128i b2 = _mm_unpacklo_epi64(t1, t3);
  __m128i b3 = _mm_unpackhi_epi64(t1, t3);

  b0 = _mm_xor_si128(
      b0, _mm_loadu_si128((const __m128i *)(const void *)(input + 0)));
  b1 = _mm_xor_si128(
      b1, _mm_loadu_si128((const __m128i *)(const void *)(input + 64)));
  b2 = _mm_xor_si128(
      b2, _mm_loadu_si128((const __m128i *)(const void *)(input + 128)));
  b3 = _mm_xor_si128(
      b3, _mm_loadu_si128((const __m128i *)(const void *)(input + 192)));

  _mm_storeu_si128((__m128i *)(void *)(output + 0), b0);
  _mm_storeu_si128((__m128i *)(void *)(output + 64), b1);
  _mm_storeu_si128((__m128i *)(void *)(output + 128), b2);
  _mm_storeu_si128((__m128i *)(void *)(output + 192), b3);
}

/* Process four independent blocks across the four 32-bit SIMD lanes. */
static void chacha20_crypt_4blocks(const uint32_t state[16],
                                   const unsigned char *input,
                                   unsigned char *output) {
  __m128i x0 = _mm_set1_epi32((int)state[0]);
  __m128i x1 = _mm_set1_epi32((int)state[1]);
  __m128i x2 = _mm_set1_epi32((int)state[2]);
  __m128i x3 = _mm_set1_epi32((int)state[3]);
  __m128i x4 = _mm_set1_epi32((int)state[4]);
  __m128i x5 = _mm_set1_epi32((int)state[5]);
  __m128i x6 = _mm_set1_epi32((int)state[6]);
  __m128i x7 = _mm_set1_epi32((int)state[7]);
  __m128i x8 = _mm_set1_epi32((int)state[8]);
  __m128i x9 = _mm_set1_epi32((int)state[9]);
  __m128i x10 = _mm_set1_epi32((int)state[10]);
  __m128i x11 = _mm_set1_epi32((int)state[11]);
  __m128i x12 = _mm_set_epi32((int)(state[12] + 3U),
                              (int)(state[12] + 2U),
                              (int)(state[12] + 1U), (int)state[12]);
  __m128i x13 = _mm_set1_epi32((int)state[13]);
  __m128i x14 = _mm_set1_epi32((int)state[14]);
  __m128i x15 = _mm_set1_epi32((int)state[15]);

  for (size_t i = 0; i < 10; ++i) {
    CHACHA20_QUARTERROUND_SSE2(x0, x4, x8, x12);
    CHACHA20_QUARTERROUND_SSE2(x1, x5, x9, x13);
    CHACHA20_QUARTERROUND_SSE2(x2, x6, x10, x14);
    CHACHA20_QUARTERROUND_SSE2(x3, x7, x11, x15);
    CHACHA20_QUARTERROUND_SSE2(x0, x5, x10, x15);
    CHACHA20_QUARTERROUND_SSE2(x1, x6, x11, x12);
    CHACHA20_QUARTERROUND_SSE2(x2, x7, x8, x13);
    CHACHA20_QUARTERROUND_SSE2(x3, x4, x9, x14);
  }

#define CHACHA20_ADD_ORIGINAL(n)                                               \
  x##n = _mm_add_epi32(x##n, _mm_set1_epi32((int)state[n]))
  CHACHA20_ADD_ORIGINAL(0);
  CHACHA20_ADD_ORIGINAL(1);
  CHACHA20_ADD_ORIGINAL(2);
  CHACHA20_ADD_ORIGINAL(3);
  CHACHA20_ADD_ORIGINAL(4);
  CHACHA20_ADD_ORIGINAL(5);
  CHACHA20_ADD_ORIGINAL(6);
  CHACHA20_ADD_ORIGINAL(7);
  CHACHA20_ADD_ORIGINAL(8);
  CHACHA20_ADD_ORIGINAL(9);
  CHACHA20_ADD_ORIGINAL(10);
  CHACHA20_ADD_ORIGINAL(11);
  x12 = _mm_add_epi32(
      x12, _mm_set_epi32((int)(state[12] + 3U), (int)(state[12] + 2U),
                         (int)(state[12] + 1U), (int)state[12]));
  CHACHA20_ADD_ORIGINAL(13);
  CHACHA20_ADD_ORIGINAL(14);
  CHACHA20_ADD_ORIGINAL(15);
#undef CHACHA20_ADD_ORIGINAL

  chacha20_xor_4words(x0, x1, x2, x3, input + 0, output + 0);
  chacha20_xor_4words(x4, x5, x6, x7, input + 16, output + 16);
  chacha20_xor_4words(x8, x9, x10, x11, input + 32, output + 32);
  chacha20_xor_4words(x12, x13, x14, x15, input + 48, output + 48);
}
#endif

#if defined(C2T_HAS_NEON)
#define ROTL32_NEON(v, n)                                                     \
  vorrq_u32(vshlq_n_u32((v), (n)), vshrq_n_u32((v), 32 - (n)))

static inline uint32x4_t chacha20_rotl16_neon(uint32x4_t value) {
  return vreinterpretq_u32_u16(vrev32q_u16(vreinterpretq_u16_u32(value)));
}

static inline uint32x4_t chacha20_rotl8_neon(uint32x4_t value) {
  static const uint8_t rotate8_bytes[16] = {3,  0,  1,  2,  7,  4, 5,  6,
                                             11, 8,  9,  10, 15, 12, 13, 14};
  return vreinterpretq_u32_u8(
      vqtbl1q_u8(vreinterpretq_u8_u32(value), vld1q_u8(rotate8_bytes)));
}

#define CHACHA20_QUARTERROUND_NEON(a, b, c, d)                                \
  do {                                                                         \
    (a) = vaddq_u32((a), (b));                                                  \
    (d) = chacha20_rotl16_neon(veorq_u32((d), (a)));                           \
    (c) = vaddq_u32((c), (d));                                                  \
    (b) = ROTL32_NEON(veorq_u32((b), (c)), 12);                               \
    (a) = vaddq_u32((a), (b));                                                  \
    (d) = chacha20_rotl8_neon(veorq_u32((d), (a)));                            \
    (c) = vaddq_u32((c), (d));                                                  \
    (b) = ROTL32_NEON(veorq_u32((b), (c)), 7);                                \
  } while (0)

static inline void chacha20_xor_4words_neon(
    uint32x4_t x0, uint32x4_t x1, uint32x4_t x2, uint32x4_t x3,
    const unsigned char *input, unsigned char *output) {
  uint32x4x2_t t01 = vzipq_u32(x0, x1);
  uint32x4x2_t t23 = vzipq_u32(x2, x3);
  uint32x4_t blocks[4] = {
      vcombine_u32(vget_low_u32(t01.val[0]), vget_low_u32(t23.val[0])),
      vcombine_u32(vget_high_u32(t01.val[0]), vget_high_u32(t23.val[0])),
      vcombine_u32(vget_low_u32(t01.val[1]), vget_low_u32(t23.val[1])),
      vcombine_u32(vget_high_u32(t01.val[1]), vget_high_u32(t23.val[1]))};

  for (size_t block_index = 0; block_index < 4; ++block_index) {
    size_t block_offset = block_index * 64;
    uint8x16_t in = vld1q_u8(input + block_offset);
    vst1q_u8(output + block_offset,
             veorq_u8(in, vreinterpretq_u8_u32(blocks[block_index])));
  }
}

/* AArch64 has 32 SIMD registers, enough to keep all four-block state words
 * and round temporaries in registers without spilling. */
static inline uint32x4_t chacha20_counters_neon(uint32_t counter) {
  const uint32_t counters[4] = {counter, counter + 1U, counter + 2U,
                                counter + 3U};
  return vld1q_u32(counters);
}

static void chacha20_crypt_4blocks_neon(const uint32_t state[16],
                                        const unsigned char *input,
                                        unsigned char *output) {
  uint32x4_t x0 = vdupq_n_u32(state[0]);
  uint32x4_t x1 = vdupq_n_u32(state[1]);
  uint32x4_t x2 = vdupq_n_u32(state[2]);
  uint32x4_t x3 = vdupq_n_u32(state[3]);
  uint32x4_t x4 = vdupq_n_u32(state[4]);
  uint32x4_t x5 = vdupq_n_u32(state[5]);
  uint32x4_t x6 = vdupq_n_u32(state[6]);
  uint32x4_t x7 = vdupq_n_u32(state[7]);
  uint32x4_t x8 = vdupq_n_u32(state[8]);
  uint32x4_t x9 = vdupq_n_u32(state[9]);
  uint32x4_t x10 = vdupq_n_u32(state[10]);
  uint32x4_t x11 = vdupq_n_u32(state[11]);
  uint32x4_t x12 = chacha20_counters_neon(state[12]);
  uint32x4_t x13 = vdupq_n_u32(state[13]);
  uint32x4_t x14 = vdupq_n_u32(state[14]);
  uint32x4_t x15 = vdupq_n_u32(state[15]);

  for (size_t i = 0; i < 10; ++i) {
    CHACHA20_QUARTERROUND_NEON(x0, x4, x8, x12);
    CHACHA20_QUARTERROUND_NEON(x1, x5, x9, x13);
    CHACHA20_QUARTERROUND_NEON(x2, x6, x10, x14);
    CHACHA20_QUARTERROUND_NEON(x3, x7, x11, x15);
    CHACHA20_QUARTERROUND_NEON(x0, x5, x10, x15);
    CHACHA20_QUARTERROUND_NEON(x1, x6, x11, x12);
    CHACHA20_QUARTERROUND_NEON(x2, x7, x8, x13);
    CHACHA20_QUARTERROUND_NEON(x3, x4, x9, x14);
  }

#define CHACHA20_ADD_ORIGINAL_NEON(n)                                          \
  x##n = vaddq_u32(x##n, vdupq_n_u32(state[n]))
  CHACHA20_ADD_ORIGINAL_NEON(0);
  CHACHA20_ADD_ORIGINAL_NEON(1);
  CHACHA20_ADD_ORIGINAL_NEON(2);
  CHACHA20_ADD_ORIGINAL_NEON(3);
  CHACHA20_ADD_ORIGINAL_NEON(4);
  CHACHA20_ADD_ORIGINAL_NEON(5);
  CHACHA20_ADD_ORIGINAL_NEON(6);
  CHACHA20_ADD_ORIGINAL_NEON(7);
  CHACHA20_ADD_ORIGINAL_NEON(8);
  CHACHA20_ADD_ORIGINAL_NEON(9);
  CHACHA20_ADD_ORIGINAL_NEON(10);
  CHACHA20_ADD_ORIGINAL_NEON(11);
  x12 = vaddq_u32(x12, chacha20_counters_neon(state[12]));
  CHACHA20_ADD_ORIGINAL_NEON(13);
  CHACHA20_ADD_ORIGINAL_NEON(14);
  CHACHA20_ADD_ORIGINAL_NEON(15);
#undef CHACHA20_ADD_ORIGINAL_NEON

  chacha20_xor_4words_neon(x0, x1, x2, x3, input + 0, output + 0);
  chacha20_xor_4words_neon(x4, x5, x6, x7, input + 16, output + 16);
  chacha20_xor_4words_neon(x8, x9, x10, x11, input + 32, output + 32);
  chacha20_xor_4words_neon(x12, x13, x14, x15, input + 48, output + 48);
}
#endif

#if defined(C2T_HAS_AVX512_KERNEL)
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

static C2T_TARGET_AVX512 inline void
chacha20_xor_16words(__m512i x0, __m512i x1, __m512i x2, __m512i x3,
                     const unsigned char *input, unsigned char *output) {
  __m512i t0 = _mm512_unpacklo_epi32(x0, x1);
  __m512i t1 = _mm512_unpackhi_epi32(x0, x1);
  __m512i t2 = _mm512_unpacklo_epi32(x2, x3);
  __m512i t3 = _mm512_unpackhi_epi32(x2, x3);
  __m512i b0 = _mm512_unpacklo_epi64(t0, t2);
  __m512i b1 = _mm512_unpackhi_epi64(t0, t2);
  __m512i b2 = _mm512_unpacklo_epi64(t1, t3);
  __m512i b3 = _mm512_unpackhi_epi64(t1, t3);

#define CHACHA20_XOR_AVX512_BLOCK(block, vector, lane)                         \
  do {                                                                         \
    __m128i block_words = _mm512_extracti32x4_epi32((vector), (lane));         \
    __m128i in = _mm_loadu_si128(                                              \
        (const __m128i *)(const void *)(input + (block) * 64));                \
    _mm_storeu_si128((__m128i *)(void *)(output + (block) * 64),               \
                     _mm_xor_si128(in, block_words));                          \
  } while (0)
  CHACHA20_XOR_AVX512_BLOCK(0, b0, 0);
  CHACHA20_XOR_AVX512_BLOCK(1, b1, 0);
  CHACHA20_XOR_AVX512_BLOCK(2, b2, 0);
  CHACHA20_XOR_AVX512_BLOCK(3, b3, 0);
  CHACHA20_XOR_AVX512_BLOCK(4, b0, 1);
  CHACHA20_XOR_AVX512_BLOCK(5, b1, 1);
  CHACHA20_XOR_AVX512_BLOCK(6, b2, 1);
  CHACHA20_XOR_AVX512_BLOCK(7, b3, 1);
  CHACHA20_XOR_AVX512_BLOCK(8, b0, 2);
  CHACHA20_XOR_AVX512_BLOCK(9, b1, 2);
  CHACHA20_XOR_AVX512_BLOCK(10, b2, 2);
  CHACHA20_XOR_AVX512_BLOCK(11, b3, 2);
  CHACHA20_XOR_AVX512_BLOCK(12, b0, 3);
  CHACHA20_XOR_AVX512_BLOCK(13, b1, 3);
  CHACHA20_XOR_AVX512_BLOCK(14, b2, 3);
  CHACHA20_XOR_AVX512_BLOCK(15, b3, 3);
#undef CHACHA20_XOR_AVX512_BLOCK
}

/* Process sixteen independent blocks across the sixteen 32-bit SIMD lanes. */
static C2T_TARGET_AVX512 void
chacha20_crypt_16blocks(const uint32_t state[restrict 16],
                        const unsigned char *input, unsigned char *output) {
  __m512i x0 = _mm512_set1_epi32((int)state[0]);
  __m512i x1 = _mm512_set1_epi32((int)state[1]);
  __m512i x2 = _mm512_set1_epi32((int)state[2]);
  __m512i x3 = _mm512_set1_epi32((int)state[3]);
  __m512i x4 = _mm512_set1_epi32((int)state[4]);
  __m512i x5 = _mm512_set1_epi32((int)state[5]);
  __m512i x6 = _mm512_set1_epi32((int)state[6]);
  __m512i x7 = _mm512_set1_epi32((int)state[7]);
  __m512i x8 = _mm512_set1_epi32((int)state[8]);
  __m512i x9 = _mm512_set1_epi32((int)state[9]);
  __m512i x10 = _mm512_set1_epi32((int)state[10]);
  __m512i x11 = _mm512_set1_epi32((int)state[11]);
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

  /* Reload the compact scalar state after the rounds instead of keeping a
   * second copy in ZMM registers, which would force round-state spills. */
  const volatile uint32_t *original = state;
#define CHACHA20_ADD_ORIGINAL_AVX512(n)                                       \
  x##n = _mm512_add_epi32(x##n, _mm512_set1_epi32((int)original[n]))
  CHACHA20_ADD_ORIGINAL_AVX512(0);
  CHACHA20_ADD_ORIGINAL_AVX512(1);
  CHACHA20_ADD_ORIGINAL_AVX512(2);
  CHACHA20_ADD_ORIGINAL_AVX512(3);
  CHACHA20_ADD_ORIGINAL_AVX512(4);
  CHACHA20_ADD_ORIGINAL_AVX512(5);
  CHACHA20_ADD_ORIGINAL_AVX512(6);
  CHACHA20_ADD_ORIGINAL_AVX512(7);
  CHACHA20_ADD_ORIGINAL_AVX512(8);
  CHACHA20_ADD_ORIGINAL_AVX512(9);
  CHACHA20_ADD_ORIGINAL_AVX512(10);
  CHACHA20_ADD_ORIGINAL_AVX512(11);
  uint32_t final_counter = original[12];
  x12 = _mm512_add_epi32(x12, CHACHA20_COUNTERS_AVX512(final_counter));
  CHACHA20_ADD_ORIGINAL_AVX512(13);
  CHACHA20_ADD_ORIGINAL_AVX512(14);
  CHACHA20_ADD_ORIGINAL_AVX512(15);
#undef CHACHA20_ADD_ORIGINAL_AVX512

  chacha20_xor_16words(x0, x1, x2, x3, input + 0, output + 0);
  chacha20_xor_16words(x4, x5, x6, x7, input + 16, output + 16);
  chacha20_xor_16words(x8, x9, x10, x11, input + 32, output + 32);
  chacha20_xor_16words(x12, x13, x14, x15, input + 48, output + 48);
  _mm256_zeroupper();
}

#endif

#if defined(C2T_HAS_AVX2_KERNEL)
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

static C2T_TARGET_AVX2 inline void
chacha20_xor_8words(__m256i x0, __m256i x1, __m256i x2, __m256i x3,
                    const unsigned char *input, unsigned char *output) {
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

static C2T_TARGET_AVX2 void
chacha20_crypt_8blocks(const uint32_t state[16], const unsigned char *input,
                       unsigned char *output) {
  __m256i x0 = _mm256_set1_epi32((int)state[0]);
  __m256i x1 = _mm256_set1_epi32((int)state[1]);
  __m256i x2 = _mm256_set1_epi32((int)state[2]);
  __m256i x3 = _mm256_set1_epi32((int)state[3]);
  __m256i x4 = _mm256_set1_epi32((int)state[4]);
  __m256i x5 = _mm256_set1_epi32((int)state[5]);
  __m256i x6 = _mm256_set1_epi32((int)state[6]);
  __m256i x7 = _mm256_set1_epi32((int)state[7]);
  __m256i x8 = _mm256_set1_epi32((int)state[8]);
  __m256i x9 = _mm256_set1_epi32((int)state[9]);
  __m256i x10 = _mm256_set1_epi32((int)state[10]);
  __m256i x11 = _mm256_set1_epi32((int)state[11]);
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

#define CHACHA20_ADD_ORIGINAL_AVX2(n)                                          \
  x##n = _mm256_add_epi32(x##n, _mm256_set1_epi32((int)state[n]))
  CHACHA20_ADD_ORIGINAL_AVX2(0);
  CHACHA20_ADD_ORIGINAL_AVX2(1);
  CHACHA20_ADD_ORIGINAL_AVX2(2);
  CHACHA20_ADD_ORIGINAL_AVX2(3);
  CHACHA20_ADD_ORIGINAL_AVX2(4);
  CHACHA20_ADD_ORIGINAL_AVX2(5);
  CHACHA20_ADD_ORIGINAL_AVX2(6);
  CHACHA20_ADD_ORIGINAL_AVX2(7);
  CHACHA20_ADD_ORIGINAL_AVX2(8);
  CHACHA20_ADD_ORIGINAL_AVX2(9);
  CHACHA20_ADD_ORIGINAL_AVX2(10);
  CHACHA20_ADD_ORIGINAL_AVX2(11);
  x12 = _mm256_add_epi32(
      x12, _mm256_set_epi32(
               (int)(state[12] + 7U), (int)(state[12] + 6U),
               (int)(state[12] + 5U), (int)(state[12] + 4U),
               (int)(state[12] + 3U), (int)(state[12] + 2U),
               (int)(state[12] + 1U), (int)state[12]));
  CHACHA20_ADD_ORIGINAL_AVX2(13);
  CHACHA20_ADD_ORIGINAL_AVX2(14);
  CHACHA20_ADD_ORIGINAL_AVX2(15);
#undef CHACHA20_ADD_ORIGINAL_AVX2

  chacha20_xor_8words(x0, x1, x2, x3, input + 0, output + 0);
  chacha20_xor_8words(x4, x5, x6, x7, input + 16, output + 16);
  chacha20_xor_8words(x8, x9, x10, x11, input + 32, output + 32);
  chacha20_xor_8words(x12, x13, x14, x15, input + 48, output + 48);
  _mm256_zeroupper();
}

#endif

#if defined(C2T_HAS_AVX512_DISPATCH)
static int chacha20_has_avx512(void) {
#if defined(C2T_MSVC_X86_SIMD)
  return __check_isa_support(__IA_SUPPORT_VECTOR512, 0) != 0;
#else
  return __builtin_cpu_supports("avx512f") != 0;
#endif
}
#endif

#if defined(C2T_HAS_AVX2_DISPATCH)
static int chacha20_has_avx2(void) {
#if defined(C2T_MSVC_X86_SIMD)
  return __check_isa_support(__IA_SUPPORT_VECTOR256, 0) != 0;
#else
  return __builtin_cpu_supports("avx2") != 0;
#endif
}
#endif

const char *c2t_crypto_simd_capabilities(void) {
#if defined(C2T_HAS_AVX512_DISPATCH) && defined(C2T_HAS_AVX2_DISPATCH)
  int avx512 = chacha20_has_avx512();
  int avx2 = chacha20_has_avx2();
  if (avx512 && avx2)
    return "SSE2, AVX2, AVX-512F";
  if (avx512)
    return "SSE2, AVX-512F";
  if (avx2)
    return "SSE2, AVX2";
  return "SSE2";
#elif defined(C2T_ARM_SVE2_SIMD)
  if (chacha20_has_sve2())
    return "NEON, SVE2";
  return "NEON";
#elif defined(C2T_HAS_NEON)
  return "NEON";
#elif defined(C2T_HAS_SSE2)
  return "SSE2";
#else
  return "scalar";
#endif
}

const char *c2t_crypto_chacha20_backend(void) {
#if defined(C2T_HAS_AVX512_DISPATCH)
  if (chacha20_has_avx512())
    return "AVX-512F (16 blocks)";
#endif
#if defined(C2T_HAS_AVX2_DISPATCH)
  if (chacha20_has_avx2())
    return "AVX2 (8 blocks)";
#endif
#if defined(C2T_ARM_SVE2_SIMD)
  if (chacha20_has_sve2())
    return "SVE2 (scalable blocks)";
#endif
#if defined(C2T_HAS_NEON)
  return "NEON (4 blocks)";
#elif defined(C2T_HAS_SSE2)
  return "SSE2 (4 blocks)";
#else
  return "scalar";
#endif
}

static void chacha20_crypt(const unsigned char key[32],
                           const unsigned char nonce[12], uint32_t counter,
                           const unsigned char *input, unsigned char *output,
                           size_t len) {
  uint32_t state[16];
  uint32_t block[16];
  unsigned char keystream[64];

  chacha20_init_state(state, key, nonce, counter);

  size_t offset = 0;
#if defined(C2T_ARM_SVE2_SIMD)
  if (chacha20_has_sve2()) {
    const size_t sve2_blocks = c2t_chacha20_sve2_block_count();
    const size_t sve2_bytes = sve2_blocks * 64U;
    while (len - offset >= sve2_bytes) {
      c2t_chacha20_crypt_sve2(state, input + offset, output + offset);
      state[12] += (uint32_t)sve2_blocks;
      offset += sve2_bytes;
    }
  }
#endif
#if defined(C2T_HAS_AVX512_DISPATCH)
  if (chacha20_has_avx512()) {
    while (len - offset >= 1024) {
#if defined(C2T_MSVC_X86_SIMD)
      c2t_chacha20_crypt_16blocks(state, input + offset, output + offset);
#else
      chacha20_crypt_16blocks(state, input + offset, output + offset);
#endif
      state[12] += 16U;
      offset += 1024;
    }
  }
#endif
#if defined(C2T_HAS_AVX2_DISPATCH)
  if (chacha20_has_avx2()) {
    while (len - offset >= 512) {
#if defined(C2T_MSVC_X86_SIMD)
      c2t_chacha20_crypt_8blocks(state, input + offset, output + offset);
#else
      chacha20_crypt_8blocks(state, input + offset, output + offset);
#endif
      state[12] += 8U;
      offset += 512;
    }
  }
#endif
#if defined(C2T_HAS_NEON)
  while (len - offset >= 256) {
    chacha20_crypt_4blocks_neon(state, input + offset, output + offset);
    state[12] += 4U;
    offset += 256;
  }
#endif
#if defined(C2T_HAS_SSE2)
  while (len - offset >= 256) {
    chacha20_crypt_4blocks(state, input + offset, output + offset);
    state[12] += 4U;
    offset += 256;
  }
#endif
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
#elif defined(C2T_HAS_NEON)
    if (block_bytes == 64) {
      uint8x16_t k0 = vld1q_u8(keystream + 0);
      uint8x16_t k1 = vld1q_u8(keystream + 16);
      uint8x16_t k2 = vld1q_u8(keystream + 32);
      uint8x16_t k3 = vld1q_u8(keystream + 48);
      uint8x16_t i0 = vld1q_u8(input + offset + 0);
      uint8x16_t i1 = vld1q_u8(input + offset + 16);
      uint8x16_t i2 = vld1q_u8(input + offset + 32);
      uint8x16_t i3 = vld1q_u8(input + offset + 48);
      vst1q_u8(output + offset + 0, veorq_u8(i0, k0));
      vst1q_u8(output + offset + 16, veorq_u8(i1, k1));
      vst1q_u8(output + offset + 32, veorq_u8(i2, k2));
      vst1q_u8(output + offset + 48, veorq_u8(i3, k3));
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
  c2t_win32_api_init();
  (void)HeapSetInformation(GetCurrentProcess(),
                           HeapEnableTerminationOnCorruption, NULL, 0);
  if (g_c2t_win32.WerSetFlags) {
    (void)g_c2t_win32.WerSetFlags(1U);
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
