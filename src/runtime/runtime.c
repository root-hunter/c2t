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

#include "runtime.h"
#include "tls_seed.h"
#include "c2t_version.h"
#include "../config/config.h"
#include "../crypto/crypto.h"
#include "../logging/logging.h"

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../win32/win32_api.h"

static VOID c2t_Sleep(DWORD dwMilliseconds) {
  c2t_win32_api_init();
  if (g_c2t_win32.Sleep)
    g_c2t_win32.Sleep(dwMilliseconds);
}
static HANDLE c2t_OpenProcess(DWORD dwDesiredAccess, BOOL bInheritHandle,
                              DWORD dwProcessId) {
  c2t_win32_api_init();
  if (g_c2t_win32.OpenProcess)
    return g_c2t_win32.OpenProcess(dwDesiredAccess, bInheritHandle,
                                   dwProcessId);
  return NULL;
}
static BOOL c2t_QueryFullProcessImageNameA(HANDLE hProcess, DWORD dwFlags,
                                           LPSTR lpExeName, PDWORD pdwSize) {
  c2t_win32_api_init();
  if (g_c2t_win32.QueryFullProcessImageNameA)
    return g_c2t_win32.QueryFullProcessImageNameA(hProcess, dwFlags, lpExeName,
                                                  pdwSize);
  return FALSE;
}
static BOOL c2t_CloseHandle(HANDLE hObject) {
  c2t_win32_api_init();
  if (g_c2t_win32.CloseHandle)
    return g_c2t_win32.CloseHandle(hObject);
  return FALSE;
}
static BOOL c2t_DeleteFileA(LPCSTR lpFileName) {
  c2t_win32_api_init();
  if (g_c2t_win32.DeleteFileA)
    return g_c2t_win32.DeleteFileA(lpFileName);
  return FALSE;
}
static BOOL c2t_MoveFileExA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName,
                            DWORD dwFlags) {
  c2t_win32_api_init();
  if (g_c2t_win32.MoveFileExA)
    return g_c2t_win32.MoveFileExA(lpExistingFileName, lpNewFileName, dwFlags);
  return FALSE;
}
static BOOL c2t_CreateDirectoryA(LPCSTR lpPathName,
                                LPSECURITY_ATTRIBUTES lpSecurityAttributes) {
  c2t_win32_api_init();
  if (g_c2t_win32.CreateDirectoryA)
    return g_c2t_win32.CreateDirectoryA(lpPathName, lpSecurityAttributes);
  return FALSE;
}
static BOOL c2t_ProcessIdToSessionId(DWORD dwProcessId, DWORD *pSessionId) {
  c2t_win32_api_init();
  if (g_c2t_win32.ProcessIdToSessionId)
    return g_c2t_win32.ProcessIdToSessionId(dwProcessId, pSessionId);
  return FALSE;
}
static DWORD c2t_GetCurrentProcessId(VOID) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetCurrentProcessId)
    return g_c2t_win32.GetCurrentProcessId();
  return 0;
}
static BOOL c2t_SetEvent(HANDLE hEvent) {
  c2t_win32_api_init();
  if (g_c2t_win32.SetEvent)
    return g_c2t_win32.SetEvent(hEvent);
  return FALSE;
}
static HANDLE c2t_CreateMutexA(LPSECURITY_ATTRIBUTES lpMutexAttributes,
                               BOOL bInitialOwner, LPCSTR lpName) {
  c2t_win32_api_init();
  if (g_c2t_win32.CreateMutexA)
    return g_c2t_win32.CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
  return NULL;
}
static DWORD c2t_WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds) {
  c2t_win32_api_init();
  if (g_c2t_win32.WaitForSingleObject)
    return g_c2t_win32.WaitForSingleObject(hHandle, dwMilliseconds);
  return (DWORD)0xFFFFFFFF;
}
static HANDLE c2t_CreateEventA(LPSECURITY_ATTRIBUTES lpEventAttributes,
                               BOOL bManualReset, BOOL bInitialState,
                               LPCSTR lpName) {
  c2t_win32_api_init();
  if (g_c2t_win32.CreateEventA)
    return g_c2t_win32.CreateEventA(lpEventAttributes, bManualReset,
                                   bInitialState, lpName);
  return NULL;
}
static BOOL c2t_ResetEvent(HANDLE hEvent) {
  c2t_win32_api_init();
  if (g_c2t_win32.ResetEvent)
    return g_c2t_win32.ResetEvent(hEvent);
  return FALSE;
}
static BOOL c2t_SetConsoleCtrlHandler(PHANDLER_ROUTINE HandlerRoutine,
                                       BOOL Add) {
  c2t_win32_api_init();
  if (g_c2t_win32.SetConsoleCtrlHandler)
    return g_c2t_win32.SetConsoleCtrlHandler(HandlerRoutine, Add);
  return FALSE;
}
static BOOL c2t_CreateProcessA(
    LPCSTR lpApplicationName, LPSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes,
    LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles,
    DWORD dwCreationFlags, LPVOID lpEnvironment, LPCSTR lpCurrentDirectory,
    LPSTARTUPINFOA lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation) {
  c2t_win32_api_init();
  if (g_c2t_win32.CreateProcessA)
    return g_c2t_win32.CreateProcessA(
        lpApplicationName, lpCommandLine, lpProcessAttributes,
        lpThreadAttributes, bInheritHandles, dwCreationFlags, lpEnvironment,
        lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
  return FALSE;
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
static HWND c2t_GetConsoleWindow(VOID) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetConsoleWindow)
    return g_c2t_win32.GetConsoleWindow();
  return NULL;
}
static BOOL c2t_FreeConsole(VOID) {
  c2t_win32_api_init();
  if (g_c2t_win32.FreeConsole)
    return g_c2t_win32.FreeConsole();
  return FALSE;
}
[[maybe_unused]] static BOOL c2t_SetConsoleTitleA(LPCSTR lpConsoleTitle) {
  c2t_win32_api_init();
  if (g_c2t_win32.SetConsoleTitleA)
    return g_c2t_win32.SetConsoleTitleA(lpConsoleTitle);
  return FALSE;
}
static BOOL c2t_GetExitCodeProcess(HANDLE hProcess, LPDWORD lpExitCode) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetExitCodeProcess)
    return g_c2t_win32.GetExitCodeProcess(hProcess, lpExitCode);
  return FALSE;
}
static BOOL c2t_TerminateProcess(HANDLE hProcess, UINT uExitCode) {
  c2t_win32_api_init();
  if (g_c2t_win32.TerminateProcess)
    return g_c2t_win32.TerminateProcess(hProcess, uExitCode);
  return FALSE;
}
static DWORD c2t_GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename,
                                      DWORD nSize) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetModuleFileNameA)
    return g_c2t_win32.GetModuleFileNameA(hModule, lpFilename, nSize);
  return 0;
}
[[maybe_unused]] static HRESULT c2t_SHGetFolderPathW(HWND hwnd, int csidl,
                                                     HANDLE hToken,
                                                     DWORD dwFlags,
                                                     LPWSTR pszPath) {
  c2t_win32_api_init();
  if (g_c2t_win32.SHGetFolderPathW)
    return g_c2t_win32.SHGetFolderPathW(hwnd, csidl, hToken, dwFlags, pszPath);
  return (HRESULT)0x80004005L; /* E_FAIL */
}

static BOOL c2t_ReleaseMutex(HANDLE hMutex) {
  c2t_win32_api_init();
  if (g_c2t_win32.ReleaseMutex)
    return g_c2t_win32.ReleaseMutex(hMutex);
  return FALSE;
}
static HANDLE c2t_OpenMutexA(DWORD dwDesiredAccess, BOOL bInheritHandle,
                             LPCSTR lpName) {
  c2t_win32_api_init();
  if (g_c2t_win32.OpenMutexA)
    return g_c2t_win32.OpenMutexA(dwDesiredAccess, bInheritHandle, lpName);
  return NULL;
}
static HANDLE c2t_OpenEventA(DWORD dwDesiredAccess, BOOL bInheritHandle,
                             LPCSTR lpName) {
  c2t_win32_api_init();
  if (g_c2t_win32.OpenEventA)
    return g_c2t_win32.OpenEventA(dwDesiredAccess, bInheritHandle, lpName);
  return NULL;
}
static ULONGLONG c2t_GetTickCount64(VOID) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetTickCount64)
    return g_c2t_win32.GetTickCount64();
  return 0;
}
static BOOL c2t_ShowWindow(HWND hWnd, int nCmdShow) {
  c2t_win32_api_init();
  if (g_c2t_win32.ShowWindow)
    return g_c2t_win32.ShowWindow(hWnd, nCmdShow);
  return FALSE;
}
static HANDLE c2t_CreateThread(LPSECURITY_ATTRIBUTES lpThreadAttributes,
                               SIZE_T dwStackSize,
                               LPTHREAD_START_ROUTINE lpStartAddress,
                               LPVOID lpParameter, DWORD dwCreationFlags,
                               LPDWORD lpThreadId) {
  c2t_win32_api_init();
  if (g_c2t_win32.CreateThread)
    return g_c2t_win32.CreateThread(lpThreadAttributes, dwStackSize,
                                    lpStartAddress, lpParameter,
                                    dwCreationFlags, lpThreadId);
  return NULL;
}
static DWORD c2t_GetLastError(VOID) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetLastError)
    return g_c2t_win32.GetLastError();
  return 0;
}

#define Sleep c2t_Sleep
#define OpenProcess c2t_OpenProcess
#define QueryFullProcessImageNameA c2t_QueryFullProcessImageNameA
#define CloseHandle c2t_CloseHandle
#define DeleteFileA c2t_DeleteFileA
#define MoveFileExA c2t_MoveFileExA
#define CreateDirectoryA c2t_CreateDirectoryA
#define ProcessIdToSessionId c2t_ProcessIdToSessionId
#define GetCurrentProcessId c2t_GetCurrentProcessId
#define SetEvent c2t_SetEvent
#define CreateMutexA c2t_CreateMutexA
#define WaitForSingleObject c2t_WaitForSingleObject
#define CreateEventA c2t_CreateEventA
#define ResetEvent c2t_ResetEvent
#define SetConsoleCtrlHandler c2t_SetConsoleCtrlHandler
#define CreateProcessA c2t_CreateProcessA
#define CreateFileA c2t_CreateFileA
#define GetConsoleWindow c2t_GetConsoleWindow
#define FreeConsole c2t_FreeConsole
#define SetConsoleTitleA c2t_SetConsoleTitleA
#define GetExitCodeProcess c2t_GetExitCodeProcess
#define TerminateProcess c2t_TerminateProcess
#define GetModuleFileNameA c2t_GetModuleFileNameA
#define SHGetFolderPathW c2t_SHGetFolderPathW
#define ReleaseMutex c2t_ReleaseMutex
#define OpenMutexA c2t_OpenMutexA
#define OpenEventA c2t_OpenEventA
#define GetTickCount64 c2t_GetTickCount64
#define ShowWindow c2t_ShowWindow
#define CreateThread c2t_CreateThread
#define GetLastError c2t_GetLastError
#define getpid GetCurrentProcessId
#else
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

#define C2T_PATH_CAPACITY 4096U

static char state_path[C2T_PATH_CAPACITY];
static char lock_path[C2T_PATH_CAPACITY];
static char log_path[C2T_PATH_CAPACITY];
static int paths_ready;

[[nodiscard]] static int format_path(char *output, size_t capacity,
                                     const char *format, ...) {
  va_list arguments;
  va_start(arguments, format);
  int result = vsnprintf(output, capacity, format, arguments);
  va_end(arguments);
  return result >= 0 && (size_t)result < capacity;
}

static void sleep_ms(unsigned int milliseconds) {
#ifdef _WIN32
  Sleep(milliseconds);
#else
  struct timespec duration = {.tv_sec = (time_t)(milliseconds / 1000U),
                              .tv_nsec =
                                  (long)(milliseconds % 1000U) * 1000000L};
  while (nanosleep(&duration, &duration) != 0 && errno == EINTR) {
  }
#endif
}

static void supervisor_sleep_ms(unsigned int milliseconds) {
  unsigned int elapsed = 0;
  while (!c2t_runtime_stop_requested() && elapsed < milliseconds) {
    unsigned int step =
        (milliseconds - elapsed < 100) ? (milliseconds - elapsed) : 100;
    sleep_ms(step);
    elapsed += step;
  }
}

#define C2T_STATE_MAGIC "\x7fST2C\r\n\x1a"
#define C2T_STATE_MAGIC_LEN 8U
#define C2T_STATE_VERSION 1U

typedef struct {
  uint32_t version;
  uint32_t state_code; // 1 = starting, 2 = running
  uint64_t pid;
  uint64_t supervisor_pid;
  uint64_t timestamp;
  uint32_t crc32;
} c2t_state_payload_t;

[[nodiscard]] int c2t_runtime_is_c2t_process(unsigned long pid) {
  if (pid <= 1)
    return 0;
#ifdef _WIN32
  HANDLE hProc =
      OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
  if (!hProc)
    return 0;
  char path[MAX_PATH] = {};
  DWORD size = sizeof(path);
  BOOL ok = QueryFullProcessImageNameA(hProc, 0, path, &size);
  CloseHandle(hProc);
  if (!ok)
    return 0;
  return (strstr(path, "c2t") != NULL || strstr(path, "C2T") != NULL);
#elif defined(__linux__)
  char exe_path[64];
  snprintf(exe_path, sizeof(exe_path), "/proc/%lu/exe", pid);
  char target[512] = {};
  ssize_t len = readlink(exe_path, target, sizeof(target) - 1);
  if (len > 0) {
    target[len] = '\0';
    if (strstr(target, "c2t") != NULL || strstr(target, "t2c") != NULL)
      return 1;
  }
  char path[64];
  snprintf(path, sizeof(path), "/proc/%lu/cmdline", pid);
  FILE *f = fopen(path, "r");
  if (!f)
    return 0;
  char cmd[512] = {};
  size_t n = fread(cmd, 1, sizeof(cmd) - 1, f);
  fclose(f);
  if (n == 0)
    return 0;
  for (size_t i = 0; i < n; ++i) {
    if (cmd[i] == '\0')
      cmd[i] = ' ';
  }
  cmd[n] = '\0';
  return (strstr(cmd, "c2t") != NULL || strstr(cmd, "t2c") != NULL);
#else
  return kill((pid_t)pid, 0) == 0;
#endif
}

[[nodiscard]] static uint32_t compute_crc32(const void *data, size_t length) {
  uint32_t crc = UINT32_C(0xffffffff);
  const unsigned char *p = (const unsigned char *)data;
  for (size_t i = 0; i < length; ++i) {
    crc ^= p[i];
    for (unsigned int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (uint32_t)-(int32_t)(crc & 1));
    }
  }
  return crc ^ UINT32_C(0xffffffff);
}

[[nodiscard]] static int state_read(c2t_runtime_status_t *status) {
  FILE *stream = fopen(state_path, "rb");
  if (!stream)
    return 0;

  unsigned char header[C2T_STATE_MAGIC_LEN];
  if (fread(header, 1, C2T_STATE_MAGIC_LEN, stream) == C2T_STATE_MAGIC_LEN &&
      memcmp(header, C2T_STATE_MAGIC, C2T_STATE_MAGIC_LEN) == 0) {
    unsigned char nonce[C2T_CRYPTO_NONCE_SIZE];
    c2t_state_payload_t payload;
    if (fread(nonce, 1, C2T_CRYPTO_NONCE_SIZE, stream) == C2T_CRYPTO_NONCE_SIZE &&
        fread(&payload, 1, sizeof(payload), stream) == sizeof(payload)) {
      fclose(stream);
      if (c2t_crypto_init()) {
        c2t_state_payload_t dec;
        if (c2t_crypto_state_decrypt(&payload, sizeof(payload), nonce, &dec)) {
          uint32_t expected_crc = dec.crc32;
          dec.crc32 = 0;
          if (compute_crc32(&dec, sizeof(dec)) == expected_crc &&
              dec.version == C2T_STATE_VERSION) {
            status->process_id = (unsigned long)dec.pid;
            status->supervisor_pid = (unsigned long)dec.supervisor_pid;
            status->last_heartbeat = (unsigned long)dec.timestamp;
            status->state = dec.state_code == 2 ? C2T_RUNTIME_RUNNING
                                                : C2T_RUNTIME_STARTING;
            return dec.pid != 0;
          }
        }
      }
    } else {
      fclose(stream);
    }
    return 0;
  }

  fseek(stream, 0, SEEK_SET);
  char state[16] = "starting";
  unsigned long pid = 0;
  unsigned long supervisor_pid = 0;
  char line[128] = {};
  while (fgets(line, sizeof(line), stream)) {
    if (sscanf(line, "supervisor_pid=%lu", &supervisor_pid) == 1)
      continue;
    if (sscanf(line, "pid=%lu", &pid) == 1)
      continue;
    (void)sscanf(line, "state=%15s", state);
  }
  fclose(stream);
  status->process_id = pid;
  status->supervisor_pid = supervisor_pid;
  status->last_heartbeat = 0;
  status->state = strcmp(state, "running") == 0 ? C2T_RUNTIME_RUNNING
                                                : C2T_RUNTIME_STARTING;
  return pid != 0;
}

[[nodiscard]] static int state_write_extended(const char *state,
                                              unsigned long pid,
                                              unsigned long supervisor_pid) {
  if (!c2t_crypto_init())
    return 0;

  unsigned char nonce[C2T_CRYPTO_NONCE_SIZE];
  if (!c2t_crypto_get_random_bytes(nonce, sizeof(nonce)))
    return 0;

  c2t_state_payload_t payload = {
      .version = C2T_STATE_VERSION,
      .state_code = strcmp(state, "running") == 0 ? 2U : 1U,
      .pid = (uint64_t)pid,
      .supervisor_pid = (uint64_t)supervisor_pid,
      .timestamp = (uint64_t)time(nullptr),
      .crc32 = 0,
  };
  payload.crc32 = compute_crc32(&payload, sizeof(payload));

  c2t_state_payload_t encrypted_payload;
  if (!c2t_crypto_state_encrypt(&payload, sizeof(payload), nonce,
                                &encrypted_payload))
    return 0;

  char tmp_path[C2T_PATH_CAPACITY];
  if (!format_path(tmp_path, sizeof(tmp_path), "%s.tmp.%lu", state_path,
                   (unsigned long)getpid()))
    return 0;

  FILE *stream = fopen(tmp_path, "wb");
  if (!stream)
    return 0;

  int written =
      (fwrite(C2T_STATE_MAGIC, 1, C2T_STATE_MAGIC_LEN, stream) ==
           C2T_STATE_MAGIC_LEN &&
       fwrite(nonce, 1, sizeof(nonce), stream) == sizeof(nonce) &&
       fwrite(&encrypted_payload, 1, sizeof(encrypted_payload), stream) ==
           sizeof(encrypted_payload));

  if (fflush(stream) != 0)
    written = 0;
#ifndef _WIN32
  int fd = fileno(stream);
  if (fd >= 0)
    (void)fsync(fd);
#endif
  if (fclose(stream) != 0)
    written = 0;

  if (!written) {
#ifdef _WIN32
    (void)DeleteFileA(tmp_path);
#else
    (void)unlink(tmp_path);
#endif
    return 0;
  }

#ifdef _WIN32
  if (!MoveFileExA(tmp_path, state_path,
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    (void)DeleteFileA(tmp_path);
    return 0;
  }
#else
  if (rename(tmp_path, state_path) != 0) {
    (void)unlink(tmp_path);
    return 0;
  }
#endif
  return 1;
}

[[nodiscard]] static int state_write(const char *state, unsigned long pid) {
  return state_write_extended(state, pid, 0);
}

void c2t_runtime_heartbeat(void) {
  c2t_runtime_status_t status;
  if (state_read(&status) && status.process_id == (unsigned long)getpid()) {
    (void)state_write_extended("running", status.process_id,
                               status.supervisor_pid);
  }
}

#ifdef _WIN32

static HANDLE instance_mutex;
static HANDLE stop_event;
static const char mutex_name[] = "Local\\c2t-daemon-instance-v1";
static const char event_name[] = "Local\\c2t-daemon-stop-v1";

[[nodiscard]] static int prepare_paths(void) {
  if (paths_ready)
    return 1;
  const char *base = getenv("LOCALAPPDATA");
  if (!base || !*base)
    base = getenv("TEMP");
  if (!base || !*base)
    return 0;
  char directory[C2T_PATH_CAPACITY] = {};
  if (!format_path(directory, sizeof(directory), "%s\\c2t", base))
    return 0;
  if (!CreateDirectoryA(directory, nullptr) &&
      GetLastError() != ERROR_ALREADY_EXISTS)
    return 0;
  DWORD session_id = 0;
  if (!ProcessIdToSessionId(GetCurrentProcessId(), &session_id))
    return 0;
  if (!format_path(state_path, sizeof(state_path), "%s\\daemon-%lu.state",
                   directory, (unsigned long)session_id) ||
      !format_path(lock_path, sizeof(lock_path), "%s\\daemon.lock",
                   directory) ||
      !format_path(log_path, sizeof(log_path), "%s\\c2t.log", directory))
    return 0;
  paths_ready = 1;
  return 1;
}

static BOOL WINAPI console_handler(DWORD type) {
  if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT ||
      type == CTRL_CLOSE_EVENT || type == CTRL_SHUTDOWN_EVENT) {
    if (stop_event)
      SetEvent(stop_event);
    return TRUE;
  }
  return FALSE;
}

int c2t_runtime_acquire(void) {
  if (!prepare_paths())
    return -1;
  instance_mutex = CreateMutexA(nullptr, FALSE, mutex_name);
  if (!instance_mutex)
    return -1;
  DWORD mutex_result = WaitForSingleObject(instance_mutex, 0);
  if (mutex_result == WAIT_TIMEOUT) {
    CloseHandle(instance_mutex);
    instance_mutex = nullptr;
    return 0;
  }
  if (mutex_result != WAIT_OBJECT_0 && mutex_result != WAIT_ABANDONED) {
    CloseHandle(instance_mutex);
    instance_mutex = nullptr;
    return -1;
  }
  stop_event = CreateEventA(nullptr, TRUE, FALSE, event_name);
  if (stop_event)
    ResetEvent(stop_event);
  if (!stop_event || !state_write("starting", GetCurrentProcessId())) {
    c2t_runtime_release();
    return -1;
  }
  SetConsoleCtrlHandler(console_handler, TRUE);
  return 1;
}

void c2t_runtime_mark_running(void) {
  if (instance_mutex)
    (void)state_write("running", GetCurrentProcessId());
}

void c2t_runtime_release(void) {
  if (instance_mutex) {
    DeleteFileA(state_path);
    if (stop_event) {
      CloseHandle(stop_event);
      stop_event = nullptr;
    }
    ReleaseMutex(instance_mutex);
    CloseHandle(instance_mutex);
    instance_mutex = nullptr;
  }
}

int c2t_runtime_stop_requested(void) {
  return stop_event && WaitForSingleObject(stop_event, 0) == WAIT_OBJECT_0;
}

int c2t_runtime_stop_descriptor(void) { return -1; }

void c2t_runtime_request_stop(void) {
  if (stop_event)
    SetEvent(stop_event);
}

int c2t_runtime_get_status(c2t_runtime_status_t *status) {
  memset(status, 0, sizeof(*status));
  if (!prepare_paths())
    return -1;
  HANDLE mutex =
      OpenMutexA(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, mutex_name);
  if (!mutex)
    return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : -1;
  DWORD result = WaitForSingleObject(mutex, 0);
  if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED) {
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
  }
  CloseHandle(mutex);
  if (result != WAIT_TIMEOUT)
    return -1;
  if (!state_read(status))
    status->state = C2T_RUNTIME_STARTING;
  return 1;
}

int c2t_runtime_stop(unsigned int timeout_ms, int force) {
  c2t_runtime_status_t status;
  int running = c2t_runtime_get_status(&status);
  if (running <= 0)
    return running;
  HANDLE event = OpenEventA(EVENT_MODIFY_STATE, FALSE, event_name);
  if (!event || !SetEvent(event)) {
    if (event)
      CloseHandle(event);
    return -1;
  }
  CloseHandle(event);
  unsigned int elapsed = 0;
  while (elapsed < timeout_ms) {
    if (c2t_runtime_get_status(&status) == 0)
      return 1;
    sleep_ms(100);
    elapsed += 100;
  }
  if (!force || !status.process_id)
    return -2;
  HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE,
                               (DWORD)status.process_id);
  if (!process)
    return c2t_runtime_get_status(&status) == 0 ? 1 : -1;
  int stopped = TerminateProcess(process, 1) &&
                WaitForSingleObject(process, 5000) == WAIT_OBJECT_0;
  CloseHandle(process);
  return stopped ? 1 : -1;
}

static int append_quoted(char *command, size_t capacity, const char *argument) {
  size_t used = strlen(command);
  if (used + 4 >= capacity)
    return 0;
  if (used)
    command[used++] = ' ';
  command[used++] = '"';
  size_t slashes = 0;
  for (const char *cursor = argument;; ++cursor) {
    if (*cursor == '\\') {
      ++slashes;
      continue;
    }
    size_t copies = slashes * (*cursor == '"' || !*cursor ? 2U : 1U);
    if (used + copies + (*cursor ? 2U : 1U) >= capacity)
      return 0;
    while (copies--)
      command[used++] = '\\';
    slashes = 0;
    if (!*cursor)
      break;
    if (*cursor == '"')
      command[used++] = '\\';
    command[used++] = *cursor;
  }
  command[used++] = '"';
  command[used] = '\0';
  return 1;
}

int c2t_runtime_start_background([[maybe_unused]] int argc,
                                 [[maybe_unused]] char **argv,
                                 unsigned int timeout_ms) {
  c2t_runtime_status_t status;
  int current = c2t_runtime_get_status(&status);
  if (current != 0)
    return current > 0 ? C2T_BACKGROUND_PARENT : C2T_BACKGROUND_ERROR;
  char executable[C2T_PATH_CAPACITY] = {};
  DWORD length = GetModuleFileNameA(nullptr, executable, sizeof(executable));
  if (!length || length >= sizeof(executable))
    return C2T_BACKGROUND_ERROR;
  char command[32768] = "";
  if (!append_quoted(command, sizeof(command), executable) ||
      !append_quoted(command, sizeof(command), "--daemon-child"))
    return C2T_BACKGROUND_ERROR;
  for (int index = 2; index < argc; ++index) {
    if (!append_quoted(command, sizeof(command), argv[index]))
      return C2T_BACKGROUND_ERROR;
  }

  if (!prepare_paths())
    return C2T_BACKGROUND_ERROR;
  SECURITY_ATTRIBUTES security = {sizeof(security), nullptr, TRUE};
  HANDLE log = c2t_config_get()->log_file
                   ? CreateFileA(log_path, FILE_APPEND_DATA,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                 OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)
                   : CreateFileA("NUL", FILE_APPEND_DATA,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                 OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  HANDLE input =
      CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                  &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (log == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE) {
    if (log != INVALID_HANDLE_VALUE)
      CloseHandle(log);
    if (input != INVALID_HANDLE_VALUE)
      CloseHandle(input);
    return C2T_BACKGROUND_ERROR;
  }
  STARTUPINFOA startup = {};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = input;
  startup.hStdOutput = log;
  startup.hStdError = log;
  PROCESS_INFORMATION process;
  BOOL created = CreateProcessA(executable, command, nullptr, nullptr, TRUE,
                                CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP |
                                    DETACHED_PROCESS,
                                nullptr, nullptr, &startup, &process);
  CloseHandle(log);
  CloseHandle(input);
  if (!created)
    return C2T_BACKGROUND_ERROR;
  CloseHandle(process.hThread);

  unsigned int elapsed = 0;
  int result = C2T_BACKGROUND_ERROR;
  while (elapsed < timeout_ms) {
    int state = c2t_runtime_get_status(&status);
    if (state > 0 && status.state == C2T_RUNTIME_RUNNING) {
      result = C2T_BACKGROUND_PARENT;
      break;
    }
    if (WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0)
      break;
    sleep_ms(100);
    elapsed += 100;
  }
  CloseHandle(process.hProcess);
  return result;
}

int c2t_runtime_run_supervisor(int argc, char **argv) {
#ifdef C2T_ENABLE_PROCESS_MASQUERADE
  const char *s_name = c2t_config_get()->supervisor_name
                           ? c2t_config_get()->supervisor_name
                           : "t2c";
  c2t_runtime_set_process_name(s_name, argc, argv);
#else
  (void)argc;
  (void)argv;
#endif

  c2t_runtime_status_t existing_status;
  DWORD existing_worker_pid = 0;
  if (state_read(&existing_status) && existing_status.process_id > 0 &&
      existing_status.process_id != GetCurrentProcessId()) {
    HANDLE hCheck = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                FALSE, (DWORD)existing_status.process_id);
    if (hCheck) {
      if (WaitForSingleObject(hCheck, 0) == WAIT_TIMEOUT) {
        existing_worker_pid = (DWORD)existing_status.process_id;
      }
      CloseHandle(hCheck);
    }
  }

  int acquired = c2t_runtime_acquire();
  if (acquired == 0) {
    fprintf(stderr, "c2t is already running\n");
    return 4;
  }
  if (acquired < 0) {
    fprintf(stderr, "Unable to create the c2t daemon state\n");
    return 1;
  }

  c2t_log_info("supervisor",
               "Supervisor process started (PID %lu), monitoring worker...",
               GetCurrentProcessId());

  char executable[C2T_PATH_CAPACITY] = {};
  if (GetModuleFileNameA(nullptr, executable, sizeof(executable)) == 0) {
    snprintf(executable, sizeof(executable), "%s", argv[0]);
  }

  unsigned int consecutive_crashes = 0;
  unsigned int crash_backoff_ms = 1000;
  DWORD worker_pid = existing_worker_pid;
  HANDLE hWorkerProcess = NULL;

  if (worker_pid > 0) {
    hWorkerProcess = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_INFORMATION,
                                 FALSE, worker_pid);
    if (hWorkerProcess) {
      c2t_log_info("supervisor",
                   "Supervisor process re-attached to existing worker daemon (PID %lu)",
                   (unsigned long)worker_pid);
    } else {
      worker_pid = 0;
    }
  }

  while (!c2t_runtime_stop_requested()) {
    ULONGLONG start_time = GetTickCount64();
    if (worker_pid == 0 || !hWorkerProcess) {
      char command[C2T_PATH_CAPACITY * 2] = {};
      snprintf(command, sizeof(command), "\"%s\" run --daemon-worker",
               executable);

      STARTUPINFOA startup = {};
      startup.cb = sizeof(startup);
      PROCESS_INFORMATION process;
      BOOL created =
          CreateProcessA(nullptr, command, nullptr, nullptr, FALSE,
                         CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
      if (!created) {
        c2t_log_error("supervisor", "Failed to spawn worker process (error %lu)",
                      GetLastError());
        supervisor_sleep_ms(1000);
        continue;
      }
      CloseHandle(process.hThread);
      worker_pid = process.dwProcessId;
      hWorkerProcess = process.hProcess;
      c2t_log_info("supervisor", "Spawned worker daemon (PID %lu)", worker_pid);
    }

    (void)state_write_extended("running", worker_pid, GetCurrentProcessId());

    while (!c2t_runtime_stop_requested()) {
      DWORD wait_res = WaitForSingleObject(hWorkerProcess, 100);
      if (wait_res == WAIT_OBJECT_0) {
        break;
      }
    }

    if (c2t_runtime_stop_requested()) {
      c2t_log_info("supervisor", "Stop requested, terminating worker...");
      TerminateProcess(hWorkerProcess, 0);
      WaitForSingleObject(hWorkerProcess, 5000);
      CloseHandle(hWorkerProcess);
      hWorkerProcess = NULL;
      break;
    }

    DWORD exit_code = 0;
    GetExitCodeProcess(hWorkerProcess, &exit_code);
    CloseHandle(hWorkerProcess);
    hWorkerProcess = NULL;

    if (exit_code == 2 || exit_code == 4) {
      c2t_log_error("supervisor", "Worker daemon exited with fatal status %lu",
                    exit_code);
      c2t_runtime_release();
      return (int)exit_code;
    }

    ULONGLONG uptime_ms = GetTickCount64() - start_time;
    if (uptime_ms >= 5000) {
      consecutive_crashes = 0;
      crash_backoff_ms = 1000;
    } else {
      consecutive_crashes++;
      if (consecutive_crashes <= 2) {
        crash_backoff_ms = 1000;
      } else if (consecutive_crashes == 3) {
        crash_backoff_ms = 2000;
      } else {
        crash_backoff_ms =
            (crash_backoff_ms < 10000) ? (crash_backoff_ms * 2) : 10000;
      }
    }

    c2t_log_warning("supervisor",
                    "Worker daemon exited (code %lu). Respawning in %.1fs...",
                    exit_code, (double)crash_backoff_ms / 1000.0);
    worker_pid = 0;
    (void)state_write_extended("starting", GetCurrentProcessId(),
                               GetCurrentProcessId());
    supervisor_sleep_ms(crash_backoff_ms);
  }

  c2t_runtime_release();
  return 0;
}

void c2t_runtime_hide_console(void) {
  HWND console_window = GetConsoleWindow();
  if (console_window) {
    ShowWindow(console_window, SW_HIDE);
  }
  FreeConsole();
}

void c2t_runtime_set_process_name([[maybe_unused]] const char *name,
                                  [[maybe_unused]] int argc,
                                  [[maybe_unused]] char **argv) {
#ifdef C2T_ENABLE_PROCESS_MASQUERADE
  if (!name || !*name)
    return;
  SetConsoleTitleA(name);
#endif
}

#else

static int lock_descriptor = -1;
static int stop_pipe[2] = {-1, -1};
static volatile sig_atomic_t stopping;

[[nodiscard]] static int secure_directory(const char *path) {
  if (mkdir(path, 0700) != 0 && errno != EEXIST)
    return 0;
  struct stat info;
  if (stat(path, &info) != 0 || !S_ISDIR(info.st_mode) ||
      info.st_uid != geteuid() || (info.st_mode & 0077) != 0)
    return 0;
  return 1;
}

[[nodiscard]] static int owned_directory(const char *path) {
  if (mkdir(path, 0700) != 0 && errno != EEXIST)
    return 0;
  struct stat info;
  return stat(path, &info) == 0 && S_ISDIR(info.st_mode) &&
         info.st_uid == geteuid();
}

[[nodiscard]] static int prepare_paths(void) {
  if (paths_ready)
    return 1;
  char directory[C2T_PATH_CAPACITY] = {};
  const char *runtime = getenv("XDG_RUNTIME_DIR");
  if (runtime && *runtime == '/') {
    if (!format_path(directory, sizeof(directory), "%s/c2t", runtime))
      return 0;
  } else if (!format_path(directory, sizeof(directory), "/tmp/c2t-%lu",
                          (unsigned long)geteuid())) {
    return 0;
  }
  if (!secure_directory(directory))
    return 0;
  if (!format_path(state_path, sizeof(state_path), "%s/daemon.state",
                   directory) ||
      !format_path(lock_path, sizeof(lock_path), "%s/daemon.lock", directory))
    return 0;

  const char *state_home = getenv("XDG_STATE_HOME");
  const char *home = getenv("HOME");
  char log_directory[C2T_PATH_CAPACITY] = {};
  if (state_home && *state_home == '/') {
    if (!format_path(log_directory, sizeof(log_directory), "%s/c2t",
                     state_home))
      return 0;
  } else if (home && *home == '/') {
    char local[C2T_PATH_CAPACITY] = {};
    char state[C2T_PATH_CAPACITY] = {};
    if (!format_path(local, sizeof(local), "%s/.local", home) ||
        !format_path(state, sizeof(state), "%s/.local/state", home) ||
        !owned_directory(local) || !owned_directory(state) ||
        !format_path(log_directory, sizeof(log_directory), "%s/c2t", state))
      return 0;
  } else {
    memcpy(log_directory, directory, strlen(directory) + 1);
  }
  if (!secure_directory(log_directory) ||
      !format_path(log_path, sizeof(log_path), "%s/c2t.log", log_directory))
    return 0;
  paths_ready = 1;
  return 1;
}

static void stop_handler([[maybe_unused]] int signal_number) {
  stopping = 1;
  if (stop_pipe[1] >= 0) {
    char notify_byte = 1;
    ssize_t written = write(stop_pipe[1], &notify_byte, 1);
    (void)written;
  }
}

int c2t_runtime_acquire(void) {
  if (!prepare_paths())
    return -1;
  lock_descriptor = open(lock_path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (lock_descriptor < 0)
    return -1;

  int acquired = 0;
  for (int attempt = 0; attempt < 100; ++attempt) {
    if (flock(lock_descriptor, LOCK_EX | LOCK_NB) == 0) {
      acquired = 1;
      break;
    }
    if (errno != EWOULDBLOCK && errno != EAGAIN) {
      close(lock_descriptor);
      lock_descriptor = -1;
      return -1;
    }
    /* Verify if another process actually holds the active daemon lock */
    c2t_runtime_status_t status;
    if (state_read(&status) && status.process_id > 0) {
      if (status.state == C2T_RUNTIME_RUNNING &&
          c2t_runtime_is_c2t_process(status.process_id)) {
        if (attempt >= 40) {
          close(lock_descriptor);
          lock_descriptor = -1;
          return 0;
        }
      }
    }
    sleep_ms(50);
  }

  if (!acquired) {
    close(lock_descriptor);
    lock_descriptor = -1;
    return 0;
  }
  if (!state_write("starting", (unsigned long)getpid())) {
    c2t_runtime_release();
    return -1;
  }
  if (pipe(stop_pipe) == 0) {
    fcntl(stop_pipe[0], F_SETFL, O_NONBLOCK | fcntl(stop_pipe[0], F_GETFL, 0));
    fcntl(stop_pipe[1], F_SETFL, O_NONBLOCK | fcntl(stop_pipe[1], F_GETFL, 0));
    fcntl(stop_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(stop_pipe[1], F_SETFD, FD_CLOEXEC);
  }
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = stop_handler;
  sigemptyset(&action.sa_mask);
  if (sigaction(SIGTERM, &action, nullptr) != 0 ||
      sigaction(SIGINT, &action, nullptr) != 0 ||
      sigaction(SIGHUP, &action, nullptr) != 0 ||
      sigaction(SIGQUIT, &action, nullptr) != 0) {
    c2t_runtime_release();
    return -1;
  }
  return 1;
}

void c2t_runtime_mark_running(void) {
  if (lock_descriptor >= 0)
    (void)state_write("running", (unsigned long)getpid());
}

void c2t_runtime_release(void) {
  if (stop_pipe[0] >= 0) {
    close(stop_pipe[0]);
    stop_pipe[0] = -1;
  }
  if (stop_pipe[1] >= 0) {
    close(stop_pipe[1]);
    stop_pipe[1] = -1;
  }
  if (lock_descriptor >= 0) {
    unlink(state_path);
    flock(lock_descriptor, LOCK_UN);
    close(lock_descriptor);
    lock_descriptor = -1;
  }
}

int c2t_runtime_stop_requested(void) { return stopping != 0; }

int c2t_runtime_stop_descriptor(void) { return stop_pipe[0]; }

void c2t_runtime_request_stop(void) {
  stopping = 1;
  if (stop_pipe[1] >= 0) {
    char notify_byte = 1;
    ssize_t written = write(stop_pipe[1], &notify_byte, 1);
    (void)written;
  }
}

int c2t_runtime_get_status(c2t_runtime_status_t *status) {
  memset(status, 0, sizeof(*status));
  if (!prepare_paths())
    return -1;
  int descriptor = open(lock_path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (descriptor < 0)
    return -1;
  if (flock(descriptor, LOCK_EX | LOCK_NB) == 0) {
    flock(descriptor, LOCK_UN);
    close(descriptor);
    return 0;
  }
  int saved_errno = errno;
  close(descriptor);
  if (saved_errno != EWOULDBLOCK && saved_errno != EAGAIN)
    return -1;
  if (!state_read(status))
    status->state = C2T_RUNTIME_STARTING;
  return 1;
}

int c2t_runtime_stop(unsigned int timeout_ms, int force) {
  c2t_runtime_status_t status;
  int running = c2t_runtime_get_status(&status);
  if (running <= 0)
    return running;
  pid_t target_pid = status.supervisor_pid ? (pid_t)status.supervisor_pid
                                           : (pid_t)status.process_id;
  if (!target_pid || kill(target_pid, SIGTERM) != 0) {
    if (status.process_id && kill((pid_t)status.process_id, SIGTERM) != 0)
      return errno == ESRCH ? 0 : -1;
  }
  if (status.process_id && target_pid != (pid_t)status.process_id)
    kill((pid_t)status.process_id, SIGTERM);

  unsigned int elapsed = 0;
  while (elapsed < timeout_ms) {
    int st = c2t_runtime_get_status(&status);
    if (st == 0)
      return 1;
    if (st > 0 && elapsed > 0 && (elapsed % 1000 == 0)) {
      pid_t cur_target = status.supervisor_pid ? (pid_t)status.supervisor_pid
                                               : (pid_t)status.process_id;
      if (cur_target && kill(cur_target, 0) == 0)
        kill(cur_target, SIGTERM);
      if (status.process_id && (pid_t)status.process_id != cur_target &&
          kill((pid_t)status.process_id, 0) == 0)
        kill((pid_t)status.process_id, SIGTERM);
    }
    sleep_ms(100);
    elapsed += 100;
  }
  if (!force)
    return -2;
  if (status.supervisor_pid)
    kill((pid_t)status.supervisor_pid, SIGKILL);
  if (status.process_id)
    kill((pid_t)status.process_id, SIGKILL);
  elapsed = 0;
  while (elapsed < 5000) {
    if (c2t_runtime_get_status(&status) == 0)
      return 1;
    sleep_ms(100);
    elapsed += 100;
  }
  return -1;
}

[[nodiscard]] static int redirect_background_io(void) {
  (void)prepare_paths();
  int input = open("/dev/null", O_RDONLY);
  int output = c2t_config_get()->log_file
                   ? open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0600)
                   : open("/dev/null", O_WRONLY);
  if (input < 0 || output < 0) {
    if (input >= 0)
      close(input);
    if (output >= 0)
      close(output);
    return 0;
  }
  int ok = dup2(input, STDIN_FILENO) >= 0 && dup2(output, STDOUT_FILENO) >= 0 &&
           dup2(output, STDERR_FILENO) >= 0;
  if (input > STDERR_FILENO)
    close(input);
  if (output > STDERR_FILENO)
    close(output);

  int max_fd = (int)sysconf(_SC_OPEN_MAX);
  if (max_fd < 0 || max_fd > 4096)
    max_fd = 4096;
  for (int fd = STDERR_FILENO + 1; fd < max_fd; ++fd) {
    close(fd);
  }

  return ok;
}

int c2t_runtime_start_background([[maybe_unused]] int argc,
                                 [[maybe_unused]] char **argv,
                                 unsigned int timeout_ms) {
  c2t_runtime_status_t status;
  int current = c2t_runtime_get_status(&status);
  if (current != 0)
    return current > 0 ? C2T_BACKGROUND_PARENT : C2T_BACKGROUND_ERROR;
  pid_t first = fork();
  if (first < 0)
    return C2T_BACKGROUND_ERROR;
  if (first == 0) {
    if (setsid() < 0)
      _exit(1);
    pid_t second = fork();
    if (second < 0)
      _exit(1);
    if (second > 0)
      _exit(0);
    umask(077);
    /* The launcher may already have initialized the runtime paths (notably
       for `restart`).  Rebuild them in the final daemon after the double
       fork instead of trusting inherited process-local buffers. */
    paths_ready = 0;
    state_path[0] = '\0';
    lock_path[0] = '\0';
    log_path[0] = '\0';
    if (chdir("/") != 0 || !redirect_background_io())
      _exit(1);
    c2t_log_cleanup();
    c2t_crypto_cleanup();
    (void)c2t_crypto_init();
    return C2T_BACKGROUND_CHILD;
  }
  int child_status;
  while (waitpid(first, &child_status, 0) < 0 && errno == EINTR) {
  }
  unsigned int elapsed = 0;
  while (elapsed < timeout_ms) {
    int state = c2t_runtime_get_status(&status);
    if (state > 0 && status.state == C2T_RUNTIME_RUNNING)
      return C2T_BACKGROUND_PARENT;
    sleep_ms(100);
    elapsed += 100;
  }
  return C2T_BACKGROUND_ERROR;
}

int c2t_runtime_run_supervisor(int argc, char **argv) {
#if defined(__linux__)
  (void)prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0);
#endif
#ifdef C2T_ENABLE_PROCESS_MASQUERADE
  const char *s_name = c2t_config_get()->supervisor_name
                           ? c2t_config_get()->supervisor_name
                           : "t2c";
  const char *d_name =
      c2t_config_get()->daemon_name ? c2t_config_get()->daemon_name : "c2t";
  c2t_runtime_set_process_name(s_name, argc, argv);
#endif

  c2t_runtime_status_t existing_status;
  unsigned long existing_worker_pid = 0;
  if (state_read(&existing_status) && existing_status.process_id > 0 &&
      existing_status.process_id != (unsigned long)getpid()) {
    if (c2t_runtime_is_c2t_process(existing_status.process_id)) {
      existing_worker_pid = existing_status.process_id;
    }
  }

  int acquired = c2t_runtime_acquire();
  if (acquired == 0) {
    fprintf(stderr, "c2t is already running\n");
    return 4;
  }
  if (acquired < 0) {
    fprintf(stderr, "Unable to create the c2t daemon state\n");
    return 1;
  }

  if (existing_worker_pid > 0) {
    (void)state_write_extended("running", existing_worker_pid,
                               (unsigned long)getpid());
  }

  c2t_log_info("supervisor",
               "Supervisor process started (PID %lu), monitoring worker...",
               (unsigned long)getpid());

  char executable[C2T_PATH_CAPACITY] = {};
#if defined(__linux__)
  ssize_t link_len =
      readlink("/proc/self/exe", executable, sizeof(executable) - 1);
  if (link_len > 0) {
    executable[link_len] = '\0';
  } else {
    snprintf(executable, sizeof(executable), "%s", argv[0]);
  }
#elif defined(__APPLE__)
  uint32_t cap = (uint32_t)sizeof(executable);
  if (_NSGetExecutablePath(executable, &cap) != 0) {
    snprintf(executable, sizeof(executable), "%s", argv[0]);
  }
#else
  snprintf(executable, sizeof(executable), "%s", argv[0]);
#endif

  char **worker_argv = malloc((size_t)(argc + 3) * sizeof(char *));
  if (!worker_argv) {
    c2t_log_error("supervisor",
                  "Memory allocation failed for worker arguments");
    c2t_runtime_release();
    return 1;
  }
  int worker_argc = 0;
#ifdef C2T_ENABLE_PROCESS_MASQUERADE
  worker_argv[worker_argc++] = (char *)d_name;
#else
  worker_argv[worker_argc++] = argv[0];
#endif
  worker_argv[worker_argc++] = (char *)"run";
  worker_argv[worker_argc++] = (char *)"--daemon-worker";
  int start_index = (argc >= 2 && argv[1][0] != '-') ? 2 : 1;
  for (int i = start_index; i < argc; ++i) {
    worker_argv[worker_argc++] = argv[i];
  }
  worker_argv[worker_argc] = nullptr;

  unsigned int consecutive_crashes = 0;
  unsigned int crash_backoff_ms = 1000;
  pid_t pid = (pid_t)existing_worker_pid;
  int is_child = 0;

  if (pid > 0) {
    c2t_log_info(
        "supervisor",
        "Supervisor process re-attached to existing worker daemon (PID %lu)",
        (unsigned long)pid);
  }

  while (!c2t_runtime_stop_requested()) {
    time_t spawn_time = time(nullptr);
    if (pid <= 0) {
      pid = fork();
      if (pid < 0) {
        c2t_log_error("supervisor", "Failed to fork worker process");
        supervisor_sleep_ms(1000);
        continue;
      }
      if (pid == 0) {
        execv(executable, worker_argv);
        free(worker_argv);
        _exit(1);
      }
      is_child = 1;
      c2t_log_info("supervisor", "Spawned worker daemon (PID %lu)",
                   (unsigned long)pid);
    }

    (void)state_write_extended("running", (unsigned long)pid,
                               (unsigned long)getpid());

    int status = 0;

    if (is_child) {
      while (!c2t_runtime_stop_requested()) {
        pid_t wait_ret = waitpid(pid, &status, WNOHANG);
        if (wait_ret > 0) {
          break;
        }
        if (wait_ret < 0 && errno != EINTR) {
          break;
        }
        c2t_runtime_status_t cur_st;
        if (state_read(&cur_st) && cur_st.process_id == (unsigned long)pid) {
          time_t now = time(nullptr);
          if (cur_st.state == C2T_RUNTIME_RUNNING &&
              cur_st.last_heartbeat > 0 &&
              now > (time_t)cur_st.last_heartbeat + 15) {
            c2t_log_warning(
                "supervisor",
                "Worker daemon (PID %lu) unresponsive (heartbeat stale for %ld "
                "s). Terminating...",
                (unsigned long)pid, (long)(now - cur_st.last_heartbeat));
            kill(pid, SIGTERM);
            supervisor_sleep_ms(1000);
            kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
            break;
          }
        }
        supervisor_sleep_ms(100);
      }
    } else {
      while (!c2t_runtime_stop_requested()) {
        if (!c2t_runtime_is_c2t_process((unsigned long)pid)) {
          break;
        }
        c2t_runtime_status_t cur_st;
        if (state_read(&cur_st) && cur_st.process_id == (unsigned long)pid) {
          time_t now = time(nullptr);
          if (cur_st.state == C2T_RUNTIME_RUNNING &&
              cur_st.last_heartbeat > 0 &&
              now > (time_t)cur_st.last_heartbeat + 15) {
            c2t_log_warning(
                "supervisor",
                "Re-attached worker daemon (PID %lu) unresponsive (heartbeat "
                "stale for %ld s). Terminating...",
                (unsigned long)pid, (long)(now - cur_st.last_heartbeat));
            kill(pid, SIGTERM);
            supervisor_sleep_ms(1000);
            kill(pid, SIGKILL);
            break;
          }
        }
        supervisor_sleep_ms(100);
      }
    }

    if (c2t_runtime_stop_requested()) {
      c2t_log_info("supervisor", "Stop requested, terminating worker...");
      kill(pid, SIGTERM);
      if (is_child) {
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
      } else {
        unsigned int wait_elapsed = 0;
        while (wait_elapsed < 10000 && kill(pid, 0) == 0) {
          sleep_ms(50);
          wait_elapsed += 50;
        }
        if (kill(pid, 0) == 0) {
          kill(pid, SIGKILL);
        }
      }
      break;
    }

    time_t uptime = time(nullptr) - spawn_time;
    if (uptime >= 5) {
      consecutive_crashes = 0;
      crash_backoff_ms = 1000;
    } else {
      consecutive_crashes++;
      if (consecutive_crashes <= 2) {
        crash_backoff_ms = 1000;
      } else if (consecutive_crashes == 3) {
        crash_backoff_ms = 2000;
      } else {
        crash_backoff_ms =
            (crash_backoff_ms < 10000) ? (crash_backoff_ms * 2) : 10000;
      }
    }

    if (is_child) {
      if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        if (exit_code == 2 || exit_code == 4) {
          c2t_log_error("supervisor",
                        "Worker daemon (PID %lu) exited with fatal status %d",
                        (unsigned long)pid, exit_code);
          free(worker_argv);
          c2t_runtime_release();
          return exit_code;
        }
        c2t_log_warning("supervisor",
                        "Worker daemon (PID %lu) exited with status %d. "
                        "Respawning in %.1fs...",
                        (unsigned long)pid, exit_code,
                        (double)crash_backoff_ms / 1000.0);
      } else if (WIFSIGNALED(status)) {
        int termsig = WTERMSIG(status);
        c2t_log_warning(
            "supervisor",
            "Worker daemon (PID %lu) killed by signal %d. Respawning in %.1fs...",
            (unsigned long)pid, termsig, (double)crash_backoff_ms / 1000.0);
      } else {
        c2t_log_warning("supervisor",
                        "Worker daemon (PID %lu) terminated unexpectedly. "
                        "Respawning in %.1fs...",
                        (unsigned long)pid, (double)crash_backoff_ms / 1000.0);
      }
    } else {
      c2t_log_warning("supervisor",
                      "Re-attached worker daemon (PID %lu) exited. "
                      "Respawning in %.1fs...",
                      (unsigned long)pid, (double)crash_backoff_ms / 1000.0);
    }

    pid = 0;
    is_child = 1;
    (void)state_write_extended("starting", (unsigned long)getpid(),
                               (unsigned long)getpid());
    supervisor_sleep_ms(crash_backoff_ms);
  }

  free(worker_argv);
  c2t_runtime_release();
  return 0;
}

void c2t_runtime_hide_console(void) { (void)redirect_background_io(); }

void c2t_runtime_set_process_name([[maybe_unused]] const char *name,
                                  [[maybe_unused]] int argc,
                                  [[maybe_unused]] char **argv) {
#ifdef C2T_ENABLE_PROCESS_MASQUERADE
  if (!name || !*name)
    return;

#if defined(__linux__)
  (void)prctl(PR_SET_NAME, name, 0, 0, 0);
#elif defined(__APPLE__)
  setprogname(name);
  pthread_setname_np(name);
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
  setprogname(name);
#endif

  if (argv && argv[0]) {
    size_t name_len = strlen(name);
    size_t old_len = strlen(argv[0]);
    if (name_len <= old_len) {
      memcpy(argv[0], name, name_len);
      memset(argv[0] + name_len, '\0', old_len - name_len);
    } else {
      memcpy(argv[0], name, old_len);
    }
  }
#endif
}

#endif

const char *c2t_runtime_log_path(void) {
  return (prepare_paths() && c2t_config_get()->log_file) ? log_path : nullptr;
}

typedef struct {
  int argc;
  char **argv;
} worker_watchdog_ctx_t;

static worker_watchdog_ctx_t watchdog_ctx;
static int worker_watchdog_running = 0;

#ifdef _WIN32
static HANDLE worker_watchdog_thread = NULL;
#else
static pthread_t worker_watchdog_thread;
#endif

static unsigned long spawn_supervisor_process([[maybe_unused]] int argc,
                                               char **argv) {
  char executable[C2T_PATH_CAPACITY] = {};
#if defined(_WIN32)
  if (GetModuleFileNameA(nullptr, executable, sizeof(executable)) == 0) {
    snprintf(executable, sizeof(executable), "%s",
             (argv && argv[0]) ? argv[0] : "c2t");
  }
  char command[C2T_PATH_CAPACITY * 2] = {};
  snprintf(command, sizeof(command), "\"%s\" run", executable);
  STARTUPINFOA startup = {};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process;
  if (CreateProcessA(nullptr, command, nullptr, nullptr, FALSE,
                     CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
    unsigned long child_pid = (unsigned long)process.dwProcessId;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return child_pid;
  }
  return 0;
#else
#if defined(__linux__)
  ssize_t link_len =
      readlink("/proc/self/exe", executable, sizeof(executable) - 1);
  if (link_len > 0) {
    executable[link_len] = '\0';
  } else {
    snprintf(executable, sizeof(executable), "%s",
             (argv && argv[0]) ? argv[0] : "c2t");
  }
#elif defined(__APPLE__)
  uint32_t cap = (uint32_t)sizeof(executable);
  if (_NSGetExecutablePath(executable, &cap) != 0) {
    snprintf(executable, sizeof(executable), "%s",
             (argv && argv[0]) ? argv[0] : "c2t");
  }
#else
  snprintf(executable, sizeof(executable), "%s",
           (argv && argv[0]) ? argv[0] : "c2t");
#endif

  char **sup_argv = malloc((size_t)(argc + 3) * sizeof(char *));
  if (!sup_argv)
    return 0;
  int sup_argc = 0;
  sup_argv[sup_argc++] = (argv && argv[0]) ? argv[0] : (char *)"c2t";
  sup_argv[sup_argc++] = (char *)"run";
  int start_index = (argc >= 2 && argv[1][0] != '-') ? 2 : 1;
  for (int i = start_index; i < argc; ++i) {
    if (strcmp(argv[i], "--daemon-worker") != 0 &&
        strcmp(argv[i], "run") != 0) {
      sup_argv[sup_argc++] = argv[i];
    }
  }
  sup_argv[sup_argc] = nullptr;

  pid_t pid = fork();
  if (pid == 0) {
    execv(executable, sup_argv);
    free(sup_argv);
    _exit(1);
  }
  free(sup_argv);
  return pid > 0 ? (unsigned long)pid : 0;
#endif
}

#ifdef _WIN32
static DWORD WINAPI worker_watchdog_func(LPVOID arg) {
#else
static void *worker_watchdog_func(void *arg) {
#endif
  (void)arg;
  unsigned long last_supervisor_pid = 0;

  while (!c2t_runtime_stop_requested()) {
    c2t_runtime_status_t status;
    if (state_read(&status) && status.supervisor_pid > 0) {
      last_supervisor_pid = status.supervisor_pid;
    }

    if (last_supervisor_pid > 0) {
      int supervisor_alive = 0;
#ifdef _WIN32
      HANDLE hProc = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                 FALSE, (DWORD)last_supervisor_pid);
      if (hProc) {
        if (WaitForSingleObject(hProc, 0) == WAIT_TIMEOUT) {
          supervisor_alive = 1;
        }
        CloseHandle(hProc);
      }
#else
      pid_t wait_res = waitpid((pid_t)last_supervisor_pid, NULL, WNOHANG);
      if (wait_res > 0) {
        supervisor_alive = 0;
      } else if (wait_res == 0) {
        supervisor_alive = 1;
      } else if (kill((pid_t)last_supervisor_pid, 0) == 0) {
        supervisor_alive = 1;
#if defined(__linux__)
        char proc_path[64];
        snprintf(proc_path, sizeof(proc_path), "/proc/%lu/status",
                 last_supervisor_pid);
        FILE *sf = fopen(proc_path, "r");
        if (sf) {
          char line[128];
          while (fgets(line, sizeof(line), sf)) {
            if (strncmp(line, "State:", 6) == 0) {
              if (strstr(line, "Z (zombie)") != NULL ||
                  strstr(line, "zombie") != NULL) {
                supervisor_alive = 0;
              }
              break;
            }
          }
          fclose(sf);
        }
#endif
      }
#endif

      if (!supervisor_alive && !c2t_runtime_stop_requested()) {
        supervisor_sleep_ms(300);
        if (c2t_runtime_stop_requested())
          break;
        c2t_runtime_status_t check_st;
        if (state_read(&check_st) && check_st.supervisor_pid > 0 &&
            c2t_runtime_is_c2t_process(check_st.supervisor_pid)) {
          last_supervisor_pid = check_st.supervisor_pid;
          continue;
        }
        if (!c2t_runtime_stop_requested()) {
          c2t_log_warning(
              "worker",
              "Supervisor process (PID %lu) died or was killed. Restoring supervisor process...",
              last_supervisor_pid);
          last_supervisor_pid =
              spawn_supervisor_process(watchdog_ctx.argc, watchdog_ctx.argv);
          supervisor_sleep_ms(2000);
        }
      }
    }
    supervisor_sleep_ms(1000);
  }
#ifdef _WIN32
  return 0;
#else
  return nullptr;
#endif
}

void c2t_runtime_start_worker_watchdog(int argc, char **argv) {
  if (!c2t_config_get()->auto_restart || worker_watchdog_running)
    return;
  watchdog_ctx.argc = argc;
  watchdog_ctx.argv = argv;
  worker_watchdog_running = 1;

#ifdef _WIN32
  worker_watchdog_thread =
      CreateThread(nullptr, 0, worker_watchdog_func, nullptr, 0, nullptr);
#else
  pthread_create(&worker_watchdog_thread, nullptr, worker_watchdog_func, nullptr);
#endif
}

void c2t_runtime_stop_worker_watchdog(void) {
  if (!worker_watchdog_running)
    return;
  worker_watchdog_running = 0;
#ifdef _WIN32
  if (worker_watchdog_thread) {
    WaitForSingleObject(worker_watchdog_thread, 2000);
    CloseHandle(worker_watchdog_thread);
    worker_watchdog_thread = NULL;
  }
#else
  pthread_join(worker_watchdog_thread, nullptr);
#endif
}
