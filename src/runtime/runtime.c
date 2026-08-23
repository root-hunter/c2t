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
#include "../config/config.h"
#include "../logging/logging.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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

#ifndef C2T_TLS_SEED_DATA
#define C2T_TLS_SEED_DATA                                                      \
  {                                                                            \
    0x9e, 0x37, 0x79, 0xb9, 0x7f, 0x4a, 0x7c, 0x15, 0xf3, 0x9c, 0x6e, 0x2a,      \
        0x4b, 0x8d, 0x10, 0x55                                                 \
  }
#endif

#if defined(_MSC_VER)
#define C2T_TLS_USED
#elif defined(__GNUC__) || defined(__clang__)
#define C2T_TLS_USED __attribute__((used))
#else
#define C2T_TLS_USED
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L &&                \
    !defined(__STDC_NO_THREADS__)
C2T_TLS_USED static _Thread_local const unsigned char c2t_tls_seed_buffer[16] =
    C2T_TLS_SEED_DATA;
#elif defined(_MSC_VER)
C2T_TLS_USED static __declspec(thread) const unsigned char
    c2t_tls_seed_buffer[16] = C2T_TLS_SEED_DATA;
#elif defined(__GNUC__) || defined(__clang__)
C2T_TLS_USED static __thread const unsigned char c2t_tls_seed_buffer[16] =
    C2T_TLS_SEED_DATA;
#else
C2T_TLS_USED static const unsigned char c2t_tls_seed_buffer[16] =
    C2T_TLS_SEED_DATA;
#endif

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

[[nodiscard]] static int state_read(c2t_runtime_status_t *status) {
  FILE *stream = fopen(state_path, "rb");
  if (!stream)
    return 0;

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
  status->state = strcmp(state, "running") == 0 ? C2T_RUNTIME_RUNNING
                                                : C2T_RUNTIME_STARTING;
  return pid != 0;
}

[[nodiscard]] static int state_write_extended(const char *state,
                                              unsigned long pid,
                                              unsigned long supervisor_pid) {
  FILE *stream = fopen(state_path, "wb");
  if (!stream)
    return 0;
  int written =
      supervisor_pid != 0
          ? (fprintf(stream, "pid=%lu\nsupervisor_pid=%lu\nstate=%s\n", pid,
                     supervisor_pid, state) > 0)
          : (fprintf(stream, "pid=%lu\nstate=%s\n", pid, state) > 0);
  if (fflush(stream) != 0)
    written = 0;
  if (fclose(stream) != 0)
    written = 0;
  return written;
}

[[nodiscard]] static int state_write(const char *state, unsigned long pid) {
  return state_write_extended(state, pid, 0);
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

  while (!c2t_runtime_stop_requested()) {
    char command[C2T_PATH_CAPACITY * 2] = {};
    snprintf(command, sizeof(command), "\"%s\" run --daemon-worker",
             executable);

    STARTUPINFOA startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process;
    ULONGLONG start_time = GetTickCount64();
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

    (void)state_write_extended("running", process.dwProcessId,
                               GetCurrentProcessId());
    c2t_log_info("supervisor", "Spawned worker daemon (PID %lu)",
                 process.dwProcessId);

    while (!c2t_runtime_stop_requested()) {
      DWORD wait_res = WaitForSingleObject(process.hProcess, 100);
      if (wait_res == WAIT_OBJECT_0) {
        break;
      }
    }

    if (c2t_runtime_stop_requested()) {
      c2t_log_info("supervisor", "Stop requested, terminating worker...");
      TerminateProcess(process.hProcess, 0);
      WaitForSingleObject(process.hProcess, 5000);
      CloseHandle(process.hProcess);
      break;
    }

    DWORD exit_code = 0;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hProcess);

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
  if (flock(lock_descriptor, LOCK_EX | LOCK_NB) != 0) {
    close(lock_descriptor);
    lock_descriptor = -1;
    return errno == EWOULDBLOCK ? 0 : -1;
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
      sigaction(SIGINT, &action, nullptr) != 0) {
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
  if (saved_errno != EWOULDBLOCK)
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
    if (c2t_runtime_get_status(&status) == 0)
      return 1;
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
    if (chdir("/") != 0 || !redirect_background_io())
      _exit(1);
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
#ifdef C2T_ENABLE_PROCESS_MASQUERADE
  const char *s_name = c2t_config_get()->supervisor_name
                           ? c2t_config_get()->supervisor_name
                           : "t2c";
  const char *d_name =
      c2t_config_get()->daemon_name ? c2t_config_get()->daemon_name : "c2t";
  c2t_runtime_set_process_name(s_name, argc, argv);
#endif
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

  while (!c2t_runtime_stop_requested()) {
    time_t spawn_time = time(nullptr);
    pid_t pid = fork();
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

    (void)state_write_extended("running", (unsigned long)pid,
                               (unsigned long)getpid());
    c2t_log_info("supervisor", "Spawned worker daemon (PID %lu)",
                 (unsigned long)pid);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
      if (c2t_runtime_stop_requested()) {
        kill(pid, SIGTERM);
      }
    }

    if (c2t_runtime_stop_requested()) {
      c2t_log_info("supervisor", "Stop requested, terminating supervisor");
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
