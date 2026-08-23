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

#ifndef C2T_FILES_H
#define C2T_FILES_H

#include "../clipboard/clipboard_source.h"
#include <stddef.h>

enum { C2T_FILE_NOT_HANDLED = 0, C2T_FILE_SENT = 1, C2T_FILE_ERROR = -1 };

[[nodiscard]] int
c2t_file_try_clipboard_path(const void *data, size_t length,
                            const char *mime_type,
                            const c2t_clipboard_source_t *source);

[[nodiscard]] int c2t_file_send_path(const char *path,
                                     const c2t_clipboard_source_t *source);

[[nodiscard]] int c2t_file_list_directory(const char *path, char *output,
                                          size_t capacity);

[[nodiscard]] int c2t_file_read_text_preview(const char *path, char *output,
                                             size_t capacity, size_t max_bytes);

[[nodiscard]] int c2t_file_get_info(const char *path, char *output,
                                    size_t capacity);

[[nodiscard]] int c2t_file_save_uploaded(const char *file_id,
                                         const char *file_name,
                                         const char *caption);

[[nodiscard]] uint64_t c2t_files_get_total_bytes(void);
[[nodiscard]] uint64_t c2t_files_get_total_files(void);

#endif
