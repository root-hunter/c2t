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

#ifndef C2T_SHELL_OUTPUT_H
#define C2T_SHELL_OUTPUT_H

#include "shell.h"
#include <stddef.h>

[[nodiscard]] int c2t_shell_format_telegram(const char *command,
                                            const c2t_shell_result_t *result,
                                            char *output, size_t capacity);

[[nodiscard]] int c2t_shell_run_and_send(const char *command);

[[nodiscard]] int c2t_shell_run_script_file_and_send(const char *script_path,
                                                     const char *args);

[[nodiscard]] int c2t_shell_run_uploaded_script(const char *file_id,
                                                const char *file_name,
                                                const char *caption);

#endif /* C2T_SHELL_OUTPUT_H */
