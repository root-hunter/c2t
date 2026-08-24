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

#include "logging.h"
#include "../config/config.h"
#include "../runtime/runtime.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <io.h>
#include <windows.h>
#include "../win32/win32_api.h"

static void c2t_InitializeCriticalSection(
    LPCRITICAL_SECTION lpCriticalSection) {
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
static void c2t_GetSystemTime(LPSYSTEMTIME lpSystemTime) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetSystemTime)
    g_c2t_win32.GetSystemTime(lpSystemTime);
}

#define InitializeCriticalSection c2t_InitializeCriticalSection
#define EnterCriticalSection c2t_EnterCriticalSection
#define LeaveCriticalSection c2t_LeaveCriticalSection
#define GetSystemTime c2t_GetSystemTime
#else
#include <pthread.h>
#include <unistd.h>
#if defined(__GLIBC__) && !defined(_WIN32)
#include <malloc.h>
#endif
#endif

static int verbose;
static FILE *log_file_stream;

static char *memory_log_buf;
static size_t ring_head;
static size_t ring_unread;

#ifdef _WIN32
static CRITICAL_SECTION log_mutex;
static int log_mutex_initialized;
static void log_lock(void) {
  if (!log_mutex_initialized) {
    InitializeCriticalSection(&log_mutex);
    log_mutex_initialized = 1;
  }
  EnterCriticalSection(&log_mutex);
}
static void log_unlock(void) { LeaveCriticalSection(&log_mutex); }
#else
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static void log_lock(void) { (void)pthread_mutex_lock(&log_mutex); }
static void log_unlock(void) { (void)pthread_mutex_unlock(&log_mutex); }
#endif

static void write_log(const char *level, const char *component,
                      const char *format, va_list arguments) {
  char timestamp[32] = "0000-00-00T00:00:00.000Z";
#ifdef _WIN32
  SYSTEMTIME utc;
  GetSystemTime(&utc);
  snprintf(timestamp, sizeof(timestamp), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
           (unsigned int)utc.wYear, (unsigned int)utc.wMonth,
           (unsigned int)utc.wDay, (unsigned int)utc.wHour,
           (unsigned int)utc.wMinute, (unsigned int)utc.wSecond,
           (unsigned int)utc.wMilliseconds);
#else
  struct timespec now;
  if (timespec_get(&now, TIME_UTC) == TIME_UTC) {
    struct tm utc;
    if (gmtime_r(&now.tv_sec, &utc) &&
        strftime(timestamp, 20, "%Y-%m-%dT%H:%M:%S", &utc) == 19)
      snprintf(timestamp + 19, sizeof(timestamp) - 19, ".%03uZ",
               (unsigned int)(now.tv_nsec / 1000000));
  }
#endif

  char message[2048] = {};
  int result = vsnprintf(message, sizeof(message), format, arguments);
  if (result < 0)
    memcpy(message, "Unable to format log message",
           sizeof("Unable to format log message"));
  fprintf(stderr, "%s %-5s [%s] %s\n", timestamp, level, component, message);
  fflush(stderr);

  if (log_file_stream) {
    fprintf(log_file_stream, "%s %-5s [%s] %s\n", timestamp, level, component,
            message);
    fflush(log_file_stream);
  }

  /* Record in memory circular ring buffer for zero-fragmentation retrieval */
  char line_buf[2500] = {};
  int line_len = snprintf(line_buf, sizeof(line_buf), "%s %-5s [%s] %s\n",
                          timestamp, level, component, message);
  if (line_len > 0) {
    log_lock();
    if (!memory_log_buf) {
      memory_log_buf = malloc(C2T_LOG_MEMORY_CAPACITY);
      ring_head = 0;
      ring_unread = 0;
    }
    if (memory_log_buf) {
      size_t to_write = (size_t)line_len;
      const char *src = line_buf;
      if (to_write > C2T_LOG_MEMORY_CAPACITY) {
        src += (to_write - C2T_LOG_MEMORY_CAPACITY);
        to_write = C2T_LOG_MEMORY_CAPACITY;
      }
      size_t first_chunk = (C2T_LOG_MEMORY_CAPACITY - ring_head < to_write)
                               ? (C2T_LOG_MEMORY_CAPACITY - ring_head)
                               : to_write;
      memcpy(memory_log_buf + ring_head, src, first_chunk);
      if (to_write > first_chunk) {
        memcpy(memory_log_buf, src + first_chunk, to_write - first_chunk);
      }
      ring_head = (ring_head + to_write) % C2T_LOG_MEMORY_CAPACITY;

      ring_unread += to_write;
      if (ring_unread > C2T_LOG_MEMORY_CAPACITY)
        ring_unread = C2T_LOG_MEMORY_CAPACITY;
    }
    log_unlock();
  }
}

static size_t copy_unread_locked(char *destination, size_t capacity) {
  size_t copied = ring_unread;
  if (copied >= capacity)
    copied = capacity - 1U;

  if (memory_log_buf && copied > 0) {
    size_t start =
        (ring_head + C2T_LOG_MEMORY_CAPACITY - ring_unread) %
        C2T_LOG_MEMORY_CAPACITY;
    size_t first_chunk = (C2T_LOG_MEMORY_CAPACITY - start < copied)
                             ? (C2T_LOG_MEMORY_CAPACITY - start)
                             : copied;
    memcpy(destination, memory_log_buf + start, first_chunk);
    if (copied > first_chunk)
      memcpy(destination + first_chunk, memory_log_buf, copied - first_chunk);
  }
  destination[copied] = '\0';
  return copied;
}

char *c2t_log_get_unread(size_t *out_length) {
  log_lock();
  if (!memory_log_buf || ring_unread == 0) {
    log_unlock();
    if (out_length)
      *out_length = 0;
    return nullptr;
  }

  size_t unread = ring_unread;
  char *copy = malloc(unread + 1U);
  if (copy)
    (void)copy_unread_locked(copy, unread + 1U);
  log_unlock();
  if (out_length)
    *out_length = copy ? unread : 0;
  return copy;
}

size_t c2t_log_copy_unread(char *destination, size_t capacity) {
  if (!destination || capacity == 0)
    return 0;

  log_lock();
  size_t copied = copy_unread_locked(destination, capacity);
  log_unlock();
  return copied;
}

void c2t_log_advance_read_offset(size_t bytes_consumed) {
  log_lock();
  if (bytes_consumed >= ring_unread)
    ring_unread = 0;
  else
    ring_unread -= bytes_consumed;
  log_unlock();
}

void c2t_log_cleanup(void) {
  log_lock();
  if (log_file_stream) {
    fclose(log_file_stream);
    log_file_stream = nullptr;
  }
  free(memory_log_buf);
  memory_log_buf = nullptr;
  ring_head = 0;
  ring_unread = 0;
  log_unlock();
#if defined(__GLIBC__) && !defined(_WIN32)
  malloc_trim(0);
#endif
}

void c2t_log_init(void) {
  const c2t_config_t *config = c2t_config_get();
  verbose = config->verbose;
  const char *path = c2t_runtime_log_path();
  if (config->log_file && path && !log_file_stream) {
#ifdef _WIN32
    if (_isatty(_fileno(stderr)))
      log_file_stream = fopen(path, "ab");
#else
    if (isatty(2))
      log_file_stream = fopen(path, "ab");
#endif
  }
}

int c2t_log_is_verbose(void) { return verbose; }

void c2t_log_error(const char *component, const char *format, ...) {
  va_list arguments;
  va_start(arguments, format);
  write_log("ERROR", component, format, arguments);
  va_end(arguments);
}

void c2t_log_warning(const char *component, const char *format, ...) {
  va_list arguments;
  va_start(arguments, format);
  write_log("WARN", component, format, arguments);
  va_end(arguments);
}

void c2t_log_info(const char *component, const char *format, ...) {
  if (!verbose)
    return;
  va_list arguments;
  va_start(arguments, format);
  write_log("INFO", component, format, arguments);
  va_end(arguments);
}

void c2t_log_debug(const char *component, const char *format, ...) {
  if (!verbose)
    return;
  va_list arguments;
  va_start(arguments, format);
  write_log("DEBUG", component, format, arguments);
  va_end(arguments);
}
