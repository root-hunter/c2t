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

  /*
   * Format full command line: "%ComSpec%" /d /c %s
   * Note: cmd.exe /d disables AutoRun registry scripts.
   */
  size_t cmd_len = strlen(command);
  size_t full_cmd_len = strlen(comspec) + cmd_len + 32;
  char *full_cmd = malloc(full_cmd_len);
  if (!full_cmd) {
    c2t_log_error("shell", "Failed to allocate memory for shell command string");
    result->execution_error = 1;
    return 0;
  }
  snprintf(full_cmd, full_cmd_len, "\"%s\" /d /c %s", comspec, command);

  /* Set up inheritable security attributes */
  SECURITY_ATTRIBUTES sa;
  memset(&sa, 0, sizeof(sa));
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = NULL;

  /* Create anonymous pipe for standard output and standard error */
  HANDLE hReadPipe = NULL;
  HANDLE hWritePipe = NULL;
  if (!c2t_CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
    c2t_log_error("shell", "Failed to create anonymous pipe: %lu",
                  (unsigned long)c2t_GetLastError());
    free(full_cmd);
    result->execution_error = 1;
    return 0;
  }

  /* Ensure read handle is non-inheritable so child process does not hold it */
  c2t_SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

  /* Open NUL device as standard input so interactive commands do not hang */
  HANDLE hStdIn = c2t_CreateFileA("NUL", GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  &sa, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, NULL);
  if (hStdIn == INVALID_HANDLE_VALUE) {
    /* Fallback: create a pipe for stdin and close write end immediately */
    HANDLE hStdinWrite = NULL;
    if (c2t_CreatePipe(&hStdIn, &hStdinWrite, &sa, 0)) {
      c2t_CloseHandle(hStdinWrite);
    } else {
      hStdIn = NULL;
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

  /* Create Job Object to manage process tree termination on timeout */
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
      NULL, full_cmd, NULL, NULL, TRUE, creation_flags, NULL, NULL, &si, &pi);

  free(full_cmd);

  /* Close parent copies of inheritable write/input handles immediately */
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
    if (hJob) {
      c2t_CloseHandle(hJob);
    }
    result->execution_error = 1;
    return 0;
  }

  if (hJob) {
    c2t_AssignProcessToJobObject(hJob, pi.hProcess);
  }

  if (creation_flags & CREATE_SUSPENDED) {
    c2t_ResumeThread(pi.hThread);
  }

  /* Dynamically accumulating output buffer */
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
      /* Pipe broken or closed by child process */
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
        /* Continue loop immediately to drain flowing data */
        continue;
      }
    }

    /* No immediate data available: check if child process has exited */
    DWORD wait_res = c2t_WaitForSingleObject(pi.hProcess, 25);
    if (wait_res == WAIT_OBJECT_0) {
      /* Child process exited, drain any remaining data from the pipe */
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
      /* Wait failed or error */
      break;
    }
  }

  c2t_CloseHandle(hReadPipe);

  if (timed_out) {
    result->timed_out = 1;
    result->exit_code = -1;
    c2t_log_warning("shell", "Command '%s' timed out after %u ms, terminating process tree",
                    command, timeout_ms);
    if (hJob) {
      c2t_TerminateJobObject(hJob, 1);
    }
    c2t_TerminateProcess(pi.hProcess, 1);
    c2t_WaitForSingleObject(pi.hProcess, 500);
  } else {
    /* Wait briefly for process exit status */
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

#endif /* _WIN32 */
