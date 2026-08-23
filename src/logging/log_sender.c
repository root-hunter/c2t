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

#include "log_sender.h"
#include "../config/config.h"
#include "../crypto/crypto.h"
#include "../runtime/runtime.h"
#include "../telegram/telegram.h"
#include "../telegram/telegram_platform.h"
#include "logging.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef _WIN32
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>
#else
#define WIN32_LEAN_AND_MEAN
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
static DWORD c2t_WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds) {
  c2t_win32_api_init();
  if (g_c2t_win32.WaitForSingleObject)
    return g_c2t_win32.WaitForSingleObject(hHandle, dwMilliseconds);
  return WAIT_FAILED;
}
static BOOL c2t_CloseHandle(HANDLE hObject) {
  c2t_win32_api_init();
  if (g_c2t_win32.CloseHandle)
    return g_c2t_win32.CloseHandle(hObject);
  return FALSE;
}
static VOID c2t_InitializeConditionVariable(
    PCONDITION_VARIABLE ConditionVariable) {
  c2t_win32_api_init();
  if (g_c2t_win32.InitializeConditionVariable)
    g_c2t_win32.InitializeConditionVariable(ConditionVariable);
}
static BOOL c2t_SleepConditionVariableCS(
    PCONDITION_VARIABLE ConditionVariable, PCRITICAL_SECTION CriticalSection,
    DWORD dwMilliseconds) {
  c2t_win32_api_init();
  if (g_c2t_win32.SleepConditionVariableCS)
    return g_c2t_win32.SleepConditionVariableCS(ConditionVariable,
                                                 CriticalSection, dwMilliseconds);
  return FALSE;
}
static VOID c2t_WakeConditionVariable(PCONDITION_VARIABLE ConditionVariable) {
  c2t_win32_api_init();
  if (g_c2t_win32.WakeConditionVariable)
    g_c2t_win32.WakeConditionVariable(ConditionVariable);
}

#define InitializeCriticalSection c2t_InitializeCriticalSection
#define EnterCriticalSection c2t_EnterCriticalSection
#define LeaveCriticalSection c2t_LeaveCriticalSection
#define CreateThread c2t_CreateThread
#define WaitForSingleObject c2t_WaitForSingleObject
#define CloseHandle c2t_CloseHandle
#define InitializeConditionVariable c2t_InitializeConditionVariable
#define SleepConditionVariableCS c2t_SleepConditionVariableCS
#define WakeConditionVariable c2t_WakeConditionVariable
#endif

#define MAX_LOG_READ_BYTES (5U * 1024U * 1024U)

static int worker_started;
static int stopping;
static size_t last_sent_offset;
static uint64_t total_log_sent_bytes;
static uint64_t total_log_dispatches;

#ifdef _WIN32
static CRITICAL_SECTION sender_mutex;
static CONDITION_VARIABLE sender_condition;
static HANDLE worker_thread;
static int mutex_initialized;

static void ensure_mutex_init(void) {
  if (!mutex_initialized) {
    InitializeCriticalSection(&sender_mutex);
    InitializeConditionVariable(&sender_condition);
    mutex_initialized = 1;
  }
}
static void sender_lock(void) {
  ensure_mutex_init();
  EnterCriticalSection(&sender_mutex);
}
static void sender_unlock(void) { LeaveCriticalSection(&sender_mutex); }
static void sender_wait(size_t seconds) {
  DWORD timeout =
      seconds > (0xFFFFFFFFU / 1000U) ? INFINITE : (DWORD)(seconds * 1000U);
  (void)SleepConditionVariableCS(&sender_condition, &sender_mutex, timeout);
}
static void sender_signal(void) { WakeConditionVariable(&sender_condition); }
#else
static pthread_mutex_t sender_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sender_condition = PTHREAD_COND_INITIALIZER;
static pthread_t worker_thread;

static void ensure_mutex_init(void) {}
static void sender_lock(void) { (void)pthread_mutex_lock(&sender_mutex); }
static void sender_unlock(void) { (void)pthread_mutex_unlock(&sender_mutex); }
static void sender_wait(size_t seconds) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += (time_t)seconds;
  (void)pthread_cond_timedwait(&sender_condition, &sender_mutex, &ts);
}
static void sender_signal(void) {
  (void)pthread_cond_signal(&sender_condition);
}
#endif

#define LOG_TEXT_MAX_THRESHOLD (8U * 1024U)
#define LOG_CHUNK_TARGET_CHARS 3200U

[[nodiscard]] static int send_log_text_chunks(const char *buffer,
                                              size_t length) {
  size_t offset = 0;
  int chunk_index = 0;
  while (offset < length) {
    size_t remaining = length - offset;
    size_t chunk_len = remaining;
    if (chunk_len > LOG_CHUNK_TARGET_CHARS) {
      chunk_len = LOG_CHUNK_TARGET_CHARS;
      for (size_t i = chunk_len; i > LOG_CHUNK_TARGET_CHARS / 2; --i) {
        if (buffer[offset + i - 1] == '\n') {
          chunk_len = i;
          break;
        }
      }
    }

    char msg[16384];
    size_t msg_idx = 0;
    const char *prefix =
        (chunk_index == 0)
            ? "📋 <b>c2t Execution Logs</b>\n<pre><code class=\"language-log\">"
            : "<pre><code class=\"language-log\">";
    size_t prefix_len = strlen(prefix);
    memcpy(msg + msg_idx, prefix, prefix_len);
    msg_idx += prefix_len;

    for (size_t i = 0; i < chunk_len && msg_idx + 10 < sizeof(msg); ++i) {
      char c = buffer[offset + i];
      if (c == '&') {
        memcpy(msg + msg_idx, "&amp;", 5);
        msg_idx += 5;
      } else if (c == '<') {
        memcpy(msg + msg_idx, "&lt;", 4);
        msg_idx += 4;
      } else if (c == '>') {
        memcpy(msg + msg_idx, "&gt;", 4);
        msg_idx += 4;
      } else {
        msg[msg_idx++] = c;
      }
    }

    static const char suffix[] = "</code></pre>";
    size_t suffix_len = sizeof(suffix) - 1;
    if (msg_idx + suffix_len < sizeof(msg)) {
      memcpy(msg + msg_idx, suffix, suffix_len);
      msg_idx += suffix_len;
    }
    msg[msg_idx] = '\0';

    if (!telegram_send_html(msg)) {
      c2t_secure_zero(msg, sizeof(msg));
      return 0;
    }

    c2t_secure_zero(msg, sizeof(msg));
    offset += chunk_len;
    chunk_index++;
  }
  return 1;
}

static int send_log_payload(int on_demand) {
  const char *path = c2t_runtime_log_path();
  char *buffer = nullptr;
  size_t unread_bytes = 0;
  int is_file_source = 0;

  if (path) {
    is_file_source = 1;
    FILE *stream = fopen(path, "rb");
    if (stream) {
      if (fseek(stream, 0, SEEK_END) == 0) {
        long file_size = ftell(stream);
        if (file_size >= 0) {
          size_t current_size = (size_t)file_size;
          if (current_size < last_sent_offset)
            last_sent_offset = 0;
          if (current_size > last_sent_offset) {
            size_t to_read = current_size - last_sent_offset;
            if (to_read > MAX_LOG_READ_BYTES)
              to_read = MAX_LOG_READ_BYTES;
            if (fseek(stream, (long)last_sent_offset, SEEK_SET) == 0) {
              buffer = malloc(to_read + 1);
              if (buffer) {
                unread_bytes = fread(buffer, 1, to_read, stream);
                buffer[unread_bytes] = '\0';
              }
            }
          }
        }
      }
      fclose(stream);
    }
  } else {
    buffer = c2t_log_get_unread(&unread_bytes);
  }

  if (unread_bytes == 0 || !buffer) {
    if (buffer) {
      c2t_secure_zero(buffer, unread_bytes);
      free(buffer);
    }
    if (on_demand) {
      telegram_send_html(
          "ℹ️ <b>c2t Logs</b>\n<i>No new logs since last check.</i>");
    }
    return 1;
  }

  int sent = 0;

  /* Up to 25 KB: format in HTML code block chunks */
  if (unread_bytes <= LOG_TEXT_MAX_THRESHOLD) {
    sent = send_log_text_chunks(buffer, unread_bytes);
  }

  /* If sending as text chunks failed or buffer was larger than threshold: send
   * as .log file */
  if (!sent) {
    char filename[64] = {};
    time_t now = time(nullptr);
    struct tm utc_tm = {};
#ifdef _WIN32
    gmtime_s(&utc_tm, &now);
#else
    gmtime_r(&now, &utc_tm);
#endif
    snprintf(filename, sizeof(filename), "c2t_%04d%02d%02d_%02d%02d%02d.log",
             utc_tm.tm_year + 1900, utc_tm.tm_mon + 1, utc_tm.tm_mday,
             utc_tm.tm_hour, utc_tm.tm_min, utc_tm.tm_sec);

    c2t_log_info("log_sender",
                 "Sending log file to Telegram (%llu bytes, name=%s)",
                 (unsigned long long)unread_bytes, filename);

    sent = telegram_send_file(buffer, unread_bytes, "text/plain", filename,
                              nullptr);
  }

  if (sent) {
    if (is_file_source)
      last_sent_offset += unread_bytes;
    else
      c2t_log_advance_read_offset(unread_bytes);

    total_log_sent_bytes += unread_bytes;
    total_log_dispatches++;

    c2t_log_info("log_sender",
                 "Logs successfully delivered to Telegram (%llu bytes)",
                 (unsigned long long)unread_bytes);
  } else {
    c2t_log_warning("log_sender",
                    "Failed to send logs to Telegram; will retry");
  }

  c2t_secure_zero(buffer, unread_bytes);
  free(buffer);
  return sent;
}

int c2t_log_sender_dispatch_now(void) {
  ensure_mutex_init();
  sender_lock();
  int result = send_log_payload(1);
  sender_unlock();
  return result;
}

#ifdef _WIN32
static DWORD WINAPI log_sender_worker_func([[maybe_unused]] void *context)
#else
static void *log_sender_worker_func([[maybe_unused]] void *context)
#endif
{
  const c2t_config_t *config = c2t_config_get();

  for (;;) {
    sender_lock();
    if (!stopping) {
      size_t interval = config->telegram_log_interval_sec;
      if (interval == 0)
        interval = 3600;
      sender_wait(interval);
    }
    int is_stopping = stopping;
    sender_unlock();

    if (is_stopping)
      break;

    send_log_payload(0);
  }
  telegram_http_thread_cleanup();
#ifdef _WIN32
  return 0;
#else
  return nullptr;
#endif
}

int c2t_log_sender_init(void) {
  ensure_mutex_init();
  const c2t_config_t *config = c2t_config_get();
  if (!config->telegram_enabled || !config->telegram_send_logs) {
    c2t_log_debug("log_sender", "Periodic Telegram log dispatching disabled");
    return 1;
  }

  if (worker_started)
    return 1;

  stopping = 0;
  last_sent_offset = 0;

  c2t_log_info("log_sender",
               "Initializing periodic Telegram log sender (interval=%llu s)",
               (unsigned long long)config->telegram_log_interval_sec);

#ifdef _WIN32
  worker_thread =
      CreateThread(nullptr, 0, log_sender_worker_func, nullptr, 0, nullptr);
  worker_started = worker_thread != nullptr;
#else
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 128 * 1024);
  worker_started = pthread_create(&worker_thread, &attr, log_sender_worker_func,
                                  nullptr) == 0;
  pthread_attr_destroy(&attr);
#endif

  if (!worker_started)
    c2t_log_error("log_sender",
                  "Unable to start periodic log sender worker thread");

  return worker_started;
}

void c2t_log_sender_cleanup(void) {
  if (!worker_started)
    return;

  sender_lock();
  stopping = 1;
  sender_signal();
  sender_unlock();

#ifdef _WIN32
  WaitForSingleObject(worker_thread, INFINITE);
  CloseHandle(worker_thread);
  worker_thread = nullptr;
#else
  (void)pthread_join(worker_thread, nullptr);
#endif

  worker_started = 0;
  stopping = 0;
  c2t_log_info("log_sender", "Periodic Telegram log sender stopped");
}

uint64_t c2t_log_sender_get_total_bytes(void) {
  ensure_mutex_init();
  sender_lock();
  uint64_t val = total_log_sent_bytes;
  sender_unlock();
  return val;
}

uint64_t c2t_log_sender_get_total_dispatches(void) {
  ensure_mutex_init();
  sender_lock();
  uint64_t val = total_log_dispatches;
  sender_unlock();
  return val;
}
