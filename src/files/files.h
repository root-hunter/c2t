#ifndef C2T_FILES_H
#define C2T_FILES_H

#include <stddef.h>
#include "../clipboard/clipboard_source.h"

enum {
    C2T_FILE_NOT_HANDLED = 0,
    C2T_FILE_SENT = 1,
    C2T_FILE_ERROR = -1
};

int c2t_file_try_clipboard_path(const void *data, size_t length,
                                const char *mime_type,
                                const c2t_clipboard_source_t *source);

#endif
