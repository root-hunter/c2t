#ifndef C2T_EMBEDDED_CONFIG_H
#define C2T_EMBEDDED_CONFIG_H

#include <stddef.h>

#define C2T_EMBEDDED_PAYLOAD_CAPACITY 4096U

/* Copies an embedded value to output and returns 1 when it is present. */
int c2t_embedded_config_get(const char *name, char *output,
                            size_t output_capacity);

#endif
