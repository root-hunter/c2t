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

#ifndef C2T_SHELL_UNIX_H
#define C2T_SHELL_UNIX_H

#ifndef _WIN32

#include "shell.h"
#include <stdint.h>

[[nodiscard]] int c2t_shell_unix_execute(const char *command,
                                         c2t_shell_result_t *result,
                                         uint32_t timeout_ms);

[[nodiscard]] int c2t_shell_unix_execute_ex(const c2t_shell_options_t *options,
                                            c2t_shell_result_t *result);

[[nodiscard]] int c2t_shell_unix_session_start(c2t_shell_type_t shell_type,
                                               char *out_msg,
                                               size_t out_msg_cap);

[[nodiscard]] int c2t_shell_unix_session_write(const char *input,
                                               size_t input_len,
                                               c2t_shell_result_t *result,
                                               uint32_t wait_ms);

[[nodiscard]] int c2t_shell_unix_session_stop(char *out_msg,
                                              size_t out_msg_cap);

int c2t_shell_unix_session_get_info(c2t_shell_session_info_t *info);

#endif /* !_WIN32 */

#endif /* C2T_SHELL_UNIX_H */
