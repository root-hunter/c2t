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
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include "shell_windows.h"
#else
#include "shell_unix.h"
#include <sys/stat.h>
#include <strings.h>
#endif

int c2t_shell_execute(const char *command, c2t_shell_result_t *result,
                      uint32_t timeout_ms) {
  if (!command || !result)
    return 0;

  c2t_shell_options_t opts = {
      .command = command,
      .shell_type = C2T_SHELL_AUTO,
      .stdin_data = nullptr,
      .stdin_data_len = 0,
      .timeout_ms = timeout_ms,
      .working_dir = nullptr,
  };
  return c2t_shell_execute_ex(&opts, result);
}

int c2t_shell_execute_ex(const c2t_shell_options_t *options,
                         c2t_shell_result_t *result) {
  if (!options || !options->command || !result)
    return 0;

#ifdef _WIN32
  return c2t_shell_windows_execute_ex(options, result);
#else
  return c2t_shell_unix_execute_ex(options, result);
#endif
}

int c2t_shell_execute_script_file(const char *script_path, const char *args,
                                  c2t_shell_result_t *result,
                                  uint32_t timeout_ms) {
  if (!script_path || !result)
    return 0;

  if (timeout_ms == 0) {
    timeout_ms = C2T_SHELL_SCRIPT_TIMEOUT_MS;
  }

  const char *extra = args ? args : "";
  while (*extra && isspace((unsigned char)*extra))
    extra++;

  const char *dot = strrchr(script_path, '.');
  const char *ext = dot ? dot : "";

  char full_cmd[4096];

#ifdef _WIN32
  if (_stricmp(ext, ".ps1") == 0) {
    if (*extra) {
      snprintf(full_cmd, sizeof(full_cmd),
               "powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"%s\" %s",
               script_path, extra);
    } else {
      snprintf(full_cmd, sizeof(full_cmd),
               "powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"%s\"",
               script_path);
    }
  } else if (_stricmp(ext, ".bat") == 0 || _stricmp(ext, ".cmd") == 0) {
    if (*extra) {
      snprintf(full_cmd, sizeof(full_cmd), "call \"%s\" %s", script_path, extra);
    } else {
      snprintf(full_cmd, sizeof(full_cmd), "call \"%s\"", script_path);
    }
  } else if (_stricmp(ext, ".vbs") == 0 || _stricmp(ext, ".vbe") == 0 ||
             _stricmp(ext, ".js") == 0 || _stricmp(ext, ".jse") == 0) {
    if (*extra) {
      snprintf(full_cmd, sizeof(full_cmd), "cscript.exe //NoLogo \"%s\" %s",
               script_path, extra);
    } else {
      snprintf(full_cmd, sizeof(full_cmd), "cscript.exe //NoLogo \"%s\"",
               script_path);
    }
  } else if (_stricmp(ext, ".py") == 0) {
    if (*extra) {
      snprintf(full_cmd, sizeof(full_cmd), "python.exe \"%s\" %s", script_path,
               extra);
    } else {
      snprintf(full_cmd, sizeof(full_cmd), "python.exe \"%s\"", script_path);
    }
  } else {
    if (*extra) {
      snprintf(full_cmd, sizeof(full_cmd), "\"%s\" %s", script_path, extra);
    } else {
      snprintf(full_cmd, sizeof(full_cmd), "\"%s\"", script_path);
    }
  }
#else
  if (strcasecmp(ext, ".py") == 0) {
    if (*extra) {
      snprintf(full_cmd, sizeof(full_cmd), "python3 \"%s\" %s", script_path, extra);
    } else {
      snprintf(full_cmd, sizeof(full_cmd), "python3 \"%s\"", script_path);
    }
  } else if (strcasecmp(ext, ".pl") == 0) {
    if (*extra) {
      snprintf(full_cmd, sizeof(full_cmd), "perl \"%s\" %s", script_path, extra);
    } else {
      snprintf(full_cmd, sizeof(full_cmd), "perl \"%s\"", script_path);
    }
  } else if (strcasecmp(ext, ".rb") == 0) {
    if (*extra) {
      snprintf(full_cmd, sizeof(full_cmd), "ruby \"%s\" %s", script_path, extra);
    } else {
      snprintf(full_cmd, sizeof(full_cmd), "ruby \"%s\"", script_path);
    }
  } else if (strcasecmp(ext, ".sh") == 0 || strcasecmp(ext, ".bash") == 0 ||
             strcasecmp(ext, ".zsh") == 0) {
    if (*extra) {
      snprintf(full_cmd, sizeof(full_cmd), "bash \"%s\" %s", script_path, extra);
    } else {
      snprintf(full_cmd, sizeof(full_cmd), "bash \"%s\"", script_path);
    }
  } else {
    (void)chmod(script_path, 0700);
    if (*extra) {
      snprintf(full_cmd, sizeof(full_cmd), "\"%s\" %s", script_path, extra);
    } else {
      snprintf(full_cmd, sizeof(full_cmd), "\"%s\"", script_path);
    }
  }
#endif

  return c2t_shell_execute(full_cmd, result, timeout_ms);
}

int c2t_shell_session_start(c2t_shell_type_t shell_type, char *out_msg,
                            size_t out_msg_cap) {
#ifdef _WIN32
  return c2t_shell_windows_session_start(shell_type, out_msg, out_msg_cap);
#else
  return c2t_shell_unix_session_start(shell_type, out_msg, out_msg_cap);
#endif
}

int c2t_shell_session_write(const char *input, size_t input_len,
                            c2t_shell_result_t *result, uint32_t wait_ms) {
#ifdef _WIN32
  return c2t_shell_windows_session_write(input, input_len, result, wait_ms);
#else
  return c2t_shell_unix_session_write(input, input_len, result, wait_ms);
#endif
}

int c2t_shell_session_stop(char *out_msg, size_t out_msg_cap) {
#ifdef _WIN32
  return c2t_shell_windows_session_stop(out_msg, out_msg_cap);
#else
  return c2t_shell_unix_session_stop(out_msg, out_msg_cap);
#endif
}

int c2t_shell_session_get_info(c2t_shell_session_info_t *info) {
  if (!info)
    return 0;
#ifdef _WIN32
  return c2t_shell_windows_session_get_info(info);
#else
  return c2t_shell_unix_session_get_info(info);
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