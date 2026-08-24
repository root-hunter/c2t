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

#ifndef C2T_LOGGING_H
#define C2T_LOGGING_H

#include <stddef.h>

#define C2T_LOG_MEMORY_CAPACITY (64U * 1024U)

void c2t_log_init(void);
[[nodiscard]] int c2t_log_is_verbose(void);

#if defined(__GNUC__) || defined(__clang__)
#define C2T_PRINTF_FORMAT(format_index, arguments_index)                       \
  __attribute__((format(printf, format_index, arguments_index)))
#else
#define C2T_PRINTF_FORMAT(format_index, arguments_index)
#endif

void c2t_log_error(const char *component, const char *format, ...)
    C2T_PRINTF_FORMAT(2, 3);
void c2t_log_warning(const char *component, const char *format, ...)
    C2T_PRINTF_FORMAT(2, 3);
void c2t_log_info(const char *component, const char *format, ...)
    C2T_PRINTF_FORMAT(2, 3);
void c2t_log_debug(const char *component, const char *format, ...)
    C2T_PRINTF_FORMAT(2, 3);

[[nodiscard]] char *c2t_log_get_unread(size_t *out_length);
/* Copies a consistent snapshot of the oldest unread bytes. The destination is
 * always NUL-terminated when capacity is non-zero. No read offset is moved. */
[[nodiscard]] size_t c2t_log_copy_unread(char *destination, size_t capacity);
void c2t_log_advance_read_offset(size_t bytes_consumed);
void c2t_log_cleanup(void);

#endif
