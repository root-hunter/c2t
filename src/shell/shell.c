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

#include "shell.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include "shell_windows.h"
#else
#include "shell_unix.h"
#endif

int c2t_shell_execute(const char *command, c2t_shell_result_t *result,
                      uint32_t timeout_ms) {
  if (!command || !result)
    return 0;

#ifdef _WIN32
  return c2t_shell_windows_execute(command, result, timeout_ms);
#else
  return c2t_shell_unix_execute(command, result, timeout_ms);
#endif
}

void c2t_shell_result_free(c2t_shell_result_t *result) {
  if (!result)
    return;

  if (result->output) {
    free(result->output);
    result->output = nullptr;
  }
  result->output_len = 0;
  result->exit_code = 0;
  result->timed_out = 0;
  result->execution_error = 0;
  result->duration_ms = 0;
}