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

#include "embedded_config.h"
#include "../crypto/crypto.h"

#include <stdint.h>
#include <string.h>

#define C2T_EMBEDDED_HEADER_SIZE 32U
#define C2T_EMBEDDED_REGION_SIZE \
    (C2T_EMBEDDED_HEADER_SIZE + C2T_EMBEDDED_PAYLOAD_CAPACITY)

static const unsigned char C2T_CONFIG_KEY[32] = {
    0x53, 0x65, 0x63, 0x75, 0x72, 0x65, 0x43, 0x32,
    0x54, 0x50, 0x72, 0x6f, 0x74, 0x65, 0x63, 0x74,
    0x69, 0x6f, 0x6e, 0x4b, 0x65, 0x79, 0x56, 0x32,
    0x2e, 0x30, 0x5f, 0x8a, 0x19, 0xd4, 0xe2, 0x7b
};

/*
 * Keep this byte layout stable: tools/embed_config.py patches it after link.
 * Volatile reads prevent LTO from replacing the reserved bytes with constants.
 */
#if defined(_MSC_VER)
#pragma section(".c2tcfg", read)
__declspec(allocate(".c2tcfg"))
#define C2T_EMBEDDED_USED
#elif defined(__APPLE__) && (defined(__GNUC__) || defined(__clang__))
#define C2T_EMBEDDED_USED __attribute__((section("__DATA,__c2tcfg"), used))
#elif defined(__GNUC__) || defined(__clang__)
#define C2T_EMBEDDED_USED __attribute__((section(".c2tcfg"), used))
#else
#define C2T_EMBEDDED_USED
#endif

C2T_EMBEDDED_USED const volatile unsigned char
    c2t_embedded_region[C2T_EMBEDDED_REGION_SIZE] = {
        'C', '2', 'T', 'C', 'F', 'G', 0, 0xa7,
        0x31, 0xd5, 0x6c, 0x92, 0xe8, 0x4b, 0xf0, 0x1d,
        2, 0, 0, 0 /* format version 2, followed by length, CRC32 and nonce seed */
    };

[[nodiscard]] static uint32_t read_u32_le(size_t offset)
{
    return (uint32_t)c2t_embedded_region[offset] |
           ((uint32_t)c2t_embedded_region[offset + 1] << 8) |
           ((uint32_t)c2t_embedded_region[offset + 2] << 16) |
           ((uint32_t)c2t_embedded_region[offset + 3] << 24);
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

static void chacha20_decrypt_payload(const unsigned char key[32],
                                     const unsigned char nonce[12],
                                     uint32_t counter,
                                     const volatile unsigned char *input,
                                     unsigned char *output,
                                     size_t len)
{
    uint32_t state[16];
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

    uint32_t block[16];
    unsigned char keystream[64];
    size_t offset = 0;

    while (offset < len) {
        chacha20_block(block, state);
        for (size_t i = 0; i < 16; ++i)
            store32_le(keystream + i * 4, block[i]);

        size_t block_bytes = len - offset < 64 ? len - offset : 64;
        for (size_t i = 0; i < block_bytes; ++i)
            output[offset + i] = (unsigned char)input[offset + i] ^ keystream[i];

        c2t_secure_zero(keystream, sizeof(keystream));
        c2t_secure_zero(block, sizeof(block));

        state[12]++;
        offset += block_bytes;
    }
    c2t_secure_zero(state, sizeof(state));
}

[[nodiscard]] static uint32_t compute_crc32(const unsigned char *data, size_t length)
{
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (unsigned int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^
                  (UINT32_C(0xedb88320) & (uint32_t)-(int32_t)(crc & 1));
    }
    return crc ^ UINT32_C(0xffffffff);
}

int c2t_embedded_config_get(const char *name, char *output,
                            size_t output_capacity)
{
    if (!name || !*name || !output || output_capacity == 0)
        return 0;

    uint32_t version = read_u32_le(16);
    if (version != 1 && version != 2)
        return 0;

    uint32_t payload_length = read_u32_le(20);
    if (payload_length == 0 || payload_length > C2T_EMBEDDED_PAYLOAD_CAPACITY)
        return 0;

    uint32_t expected_crc = read_u32_le(24);
    unsigned char decrypted[C2T_EMBEDDED_PAYLOAD_CAPACITY];
    c2t_secure_lock(decrypted, sizeof(decrypted));

    if (version == 2) {
        uint32_t nonce_seed = read_u32_le(28);
        unsigned char nonce[12];
        store32_le(nonce, nonce_seed);
        for (size_t i = 0; i < 8; ++i)
            nonce[4 + i] = (unsigned char)c2t_embedded_region[8 + i];

        chacha20_decrypt_payload(C2T_CONFIG_KEY, nonce, 1,
                                 c2t_embedded_region + C2T_EMBEDDED_HEADER_SIZE,
                                 decrypted, payload_length);
    } else {
        for (size_t i = 0; i < payload_length; ++i)
            decrypted[i] = (unsigned char)c2t_embedded_region[C2T_EMBEDDED_HEADER_SIZE + i];
    }

    if (compute_crc32(decrypted, payload_length) != expected_crc) {
        c2t_secure_zero(decrypted, sizeof(decrypted));
        c2t_secure_unlock(decrypted, sizeof(decrypted));
        return 0;
    }

    int found = 0;
    size_t name_length = strlen(name);
    size_t position = 0;

    while (position < payload_length) {
        size_t line_start = position;
        while (position < payload_length && decrypted[position] != '\n')
            ++position;
        size_t line_length = position - line_start;
        if (line_length > name_length &&
            decrypted[line_start + name_length] == '=' &&
            line_length - name_length <= output_capacity) {
            size_t index;
            for (index = 0; index < name_length; ++index) {
                if (decrypted[line_start + index] != (unsigned char)name[index])
                    break;
            }
            if (index == name_length) {
                size_t value_length = line_length - name_length - 1;
                for (index = 0; index < value_length; ++index)
                    output[index] = (char)decrypted[line_start + name_length + 1 + index];
                output[value_length] = '\0';
                found = 1;
                break;
            }
        }
        ++position;
    }

    c2t_secure_zero(decrypted, sizeof(decrypted));
    c2t_secure_unlock(decrypted, sizeof(decrypted));
    return found;
}

