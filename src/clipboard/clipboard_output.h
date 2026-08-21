#ifndef C2T_CLIPBOARD_OUTPUT_H
#define C2T_CLIPBOARD_OUTPUT_H

#include <stddef.h>
#include "clipboard_source.h"

int clipboard_output_init(void);
void clipboard_output(const void *data, size_t length, const char *mime_type,
                      const c2t_clipboard_source_t *source);
void clipboard_output_cleanup(void);

#endif
