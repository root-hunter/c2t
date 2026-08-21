#include "embedded_config.h"

#include <stdint.h>
#include <string.h>

#define C2T_EMBEDDED_HEADER_SIZE 32U
#define C2T_EMBEDDED_REGION_SIZE \
    (C2T_EMBEDDED_HEADER_SIZE + C2T_EMBEDDED_PAYLOAD_CAPACITY)

/*
 * Keep this byte layout stable: tools/embed_config.py patches it after link.
 * Volatile reads prevent LTO from replacing the reserved bytes with constants.
 */
#if defined(_MSC_VER)
#pragma section(".c2tcfg", read)
__declspec(allocate(".c2tcfg"))
#define C2T_EMBEDDED_USED
#elif defined(__GNUC__) || defined(__clang__)
#define C2T_EMBEDDED_USED __attribute__((section(".c2tcfg"), used))
#else
#define C2T_EMBEDDED_USED
#endif

C2T_EMBEDDED_USED const volatile unsigned char
    c2t_embedded_region[C2T_EMBEDDED_REGION_SIZE] = {
        'C', '2', 'T', 'C', 'F', 'G', 0, 0xa7,
        0x31, 0xd5, 0x6c, 0x92, 0xe8, 0x4b, 0xf0, 0x1d,
        1, 0, 0, 0 /* format version, followed by length and CRC32 */
    };

static uint32_t read_u32_le(size_t offset)
{
    return (uint32_t)c2t_embedded_region[offset] |
           ((uint32_t)c2t_embedded_region[offset + 1] << 8) |
           ((uint32_t)c2t_embedded_region[offset + 2] << 16) |
           ((uint32_t)c2t_embedded_region[offset + 3] << 24);
}

static uint32_t payload_crc32(size_t length)
{
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t index = 0; index < length; ++index) {
        crc ^= c2t_embedded_region[C2T_EMBEDDED_HEADER_SIZE + index];
        for (unsigned int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^
                  (UINT32_C(0xedb88320) & (uint32_t)-(int32_t)(crc & 1));
    }
    return crc ^ UINT32_C(0xffffffff);
}

static size_t valid_payload_length(void)
{
    if (read_u32_le(16) != 1)
        return 0;

    uint32_t length = read_u32_le(20);
    if (length == 0 || length > C2T_EMBEDDED_PAYLOAD_CAPACITY)
        return 0;
    if (payload_crc32(length) != read_u32_le(24))
        return 0;
    return length;
}

int c2t_embedded_config_get(const char *name, char *output,
                            size_t output_capacity)
{
    if (!name || !*name || !output || output_capacity == 0)
        return 0;

    size_t payload_length = valid_payload_length();
    size_t name_length = strlen(name);
    size_t position = 0;
    while (position < payload_length) {
        size_t line_start = position;
        while (position < payload_length &&
               c2t_embedded_region[C2T_EMBEDDED_HEADER_SIZE + position] != '\n')
            ++position;
        size_t line_length = position - line_start;
        if (line_length > name_length &&
            c2t_embedded_region[C2T_EMBEDDED_HEADER_SIZE + line_start +
                                name_length] == '=' &&
            line_length - name_length <= output_capacity) {
            size_t index;
            for (index = 0; index < name_length; ++index) {
                if (c2t_embedded_region[C2T_EMBEDDED_HEADER_SIZE + line_start +
                                        index] != (unsigned char)name[index])
                    break;
            }
            if (index == name_length) {
                size_t value_length = line_length - name_length - 1;
                for (index = 0; index < value_length; ++index)
                    output[index] = (char)c2t_embedded_region[
                        C2T_EMBEDDED_HEADER_SIZE + line_start + name_length +
                        1 + index];
                output[value_length] = '\0';
                return 1;
            }
        }
        ++position;
    }
    return 0;
}
