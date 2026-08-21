#ifndef C2T_TELEGRAM_H
#define C2T_TELEGRAM_H

#include <stddef.h>
#include "../clipboard/clipboard_source.h"

int telegram_init(void);
int telegram_send(const char *text, size_t length,
                  const c2t_clipboard_source_t *source);
int telegram_send_data(const void *data, size_t length, const char *mime_type,
                       const c2t_clipboard_source_t *source);
int telegram_send_file(const void *data, size_t length, const char *mime_type,
                       const char *filename,
                       const c2t_clipboard_source_t *source);
void telegram_cleanup(void);

#endif
