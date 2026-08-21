#ifndef C2T_CLIPBOARD_SOURCE_H
#define C2T_CLIPBOARD_SOURCE_H

#include <stdint.h>

/* Fixed-size, owned metadata keeps clipboard delivery synchronous and avoids
 * platform-specific allocations crossing module boundaries. */
#define C2T_SOURCE_APPLICATION_CAPACITY 256
#define C2T_SOURCE_TITLE_CAPACITY 768

typedef struct {
    char application[C2T_SOURCE_APPLICATION_CAPACITY];
    char title[C2T_SOURCE_TITLE_CAPACITY];
    uint32_t process_id;
} c2t_clipboard_source_t;

#endif
