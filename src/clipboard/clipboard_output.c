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

#include "clipboard_output.h"
#include "../config/config.h"
#include "../crypto/arena.h"
#include "../crypto/crypto.h"
#include "../files/files.h"
#include "../logging/logging.h"
#include "../telegram/telegram.h"
#include "../telegram/telegram_platform.h"

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
#define DeleteCriticalSection c2t_DeleteCriticalSection
#define GetTickCount64 c2t_GetTickCount64
#define Sleep c2t_Sleep
#define CreateThread c2t_CreateThread
#define WaitForSingleObject c2t_WaitForSingleObject
#define CloseHandle c2t_CloseHandle
#define InitializeConditionVariable c2t_InitializeConditionVariable
#define SleepConditionVariableCS c2t_SleepConditionVariableCS
#define WakeConditionVariable c2t_WakeConditionVariable
#endif

#define DUPLICATE_WINDOW_MS 500U
#define C2T_MIME_CAPACITY 128U

typedef struct clipboard_event {
  struct clipboard_event *next;
  size_t length;
  size_t allocation_size;
  char mime_type[C2T_MIME_CAPACITY];
  c2t_clipboard_source_t source;
  int has_source;
  unsigned char nonce[C2T_CRYPTO_NONCE_SIZE];
  unsigned char encrypted_data[];
} clipboard_event_t;

static clipboard_event_t *queue_head;
static clipboard_event_t *queue_tail;
static size_t queue_bytes;
static size_t queue_items;
static size_t maximum_queue_bytes;
static size_t maximum_queue_items;
static size_t delivery_attempts;
static size_t retry_delay_ms;
static int stopping;
static int worker_started;
static atomic_int clipboard_paused;
static uint64_t total_clipboard_bytes;
static uint64_t total_clipboard_events;
static c2t_arena_t clipboard_arena;

#ifdef _WIN32
static CRITICAL_SECTION queue_mutex;
static CONDITION_VARIABLE queue_condition;
static HANDLE worker_thread;

static void queue_lock(void) { EnterCriticalSection(&queue_mutex); }
static void queue_unlock(void) { LeaveCriticalSection(&queue_mutex); }
static void queue_wait(void) {
  (void)SleepConditionVariableCS(&queue_condition, &queue_mutex, INFINITE);
}
static void queue_signal(void) { WakeConditionVariable(&queue_condition); }
#else
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_condition = PTHREAD_COND_INITIALIZER;
static pthread_t worker_thread;

static void queue_lock(void) { (void)pthread_mutex_lock(&queue_mutex); }
static void queue_unlock(void) { (void)pthread_mutex_unlock(&queue_mutex); }
static void queue_wait(void) {
  (void)pthread_cond_wait(&queue_condition, &queue_mutex);
}
static void queue_signal(void) { (void)pthread_cond_signal(&queue_condition); }
#endif

static uint64_t content_hash(const void *data, size_t length,
                             const char *mime_type,
                             const c2t_clipboard_source_t *source) {
  const unsigned char *bytes = data;
  uint64_t hash = UINT64_C(14695981039346656037);

  size_t words = length / 8;
  for (size_t w = 0; w < words; ++w) {
    uint64_t word_val;
    memcpy(&word_val, bytes + w * 8, sizeof(word_val));
    hash ^= word_val;
    hash *= UINT64_C(1099511628211);
  }
  for (size_t rem = words * 8; rem < length; ++rem) {
    hash ^= bytes[rem];
    hash *= UINT64_C(1099511628211);
  }

  while (*mime_type) {
    hash ^= (unsigned char)*mime_type++;
    hash *= UINT64_C(1099511628211);
  }
  if (source) {
    const char *fields[] = {source->application, source->title};
    const size_t capacities[] = {sizeof(source->application),
                                 sizeof(source->title)};
    for (size_t field = 0; field < 2; ++field) {
      for (size_t index = 0; index < capacities[field] && fields[field][index];
           ++index) {
        hash ^= (unsigned char)fields[field][index];
        hash *= UINT64_C(1099511628211);
      }
      hash ^= 0xff;
      hash *= UINT64_C(1099511628211);
    }
    uint32_t process_id = source->process_id;
    for (size_t index = 0; index < sizeof(process_id); ++index) {
      hash ^= (unsigned char)(process_id >> (index * 8));
      hash *= UINT64_C(1099511628211);
    }
  }
  return hash;
}

static uint64_t current_time_ms(int *available) {
#ifdef _WIN32
  *available = 1;
  return GetTickCount64();
#else
  struct timespec now;
  *available = clock_gettime(CLOCK_MONOTONIC, &now) == 0;
  return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
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

static int deliver_encrypted_payload(const clipboard_event_t *event,
                                     const c2t_clipboard_source_t *source) {
  if (event->length > 0 && event->length < 1024) {
    unsigned char path_buf[1024];
    if (c2t_crypto_decrypt(event->encrypted_data, event->length, event->nonce,
                           path_buf)) {
      int file_result = c2t_file_try_clipboard_path(path_buf, event->length,
                                                    event->mime_type, source);
      c2t_secure_zero(path_buf, sizeof(path_buf));
      if (file_result != C2T_FILE_NOT_HANDLED) {
        return file_result == C2T_FILE_SENT;
      }
    }
  }

  return telegram_send_encrypted_data(event->encrypted_data, event->length,
                                      event->nonce, event->mime_type, source);
}

static void deliver_event(const clipboard_event_t *event) {
  const c2t_clipboard_source_t *source =
      event->has_source ? &event->source : nullptr;

  if (strncmp(event->mime_type, "text/", 5) == 0) {
    if (event->length > 0) {
      unsigned char chunk_buf[1024];
      c2t_secure_lock(chunk_buf, sizeof(chunk_buf));
      size_t written = 0;
      while (written < event->length) {
        size_t chunk = event->length - written < sizeof(chunk_buf)
                           ? event->length - written
                           : sizeof(chunk_buf);
        if (!c2t_crypto_decrypt_offset(event->encrypted_data + written, written,
                                       chunk, event->nonce, chunk_buf)) {
          break;
        }
        if (fwrite(chunk_buf, 1, chunk, stdout) != chunk)
          c2t_log_warning("clipboard",
                          "Unable to write content chunk to stdout");
        c2t_secure_zero(chunk_buf, sizeof(chunk_buf));
        written += chunk;
      }
      c2t_secure_unlock(chunk_buf, sizeof(chunk_buf));
    }
    if (fputc('\n', stdout) == EOF || fflush(stdout) == EOF)
      c2t_log_warning("clipboard", "Unable to flush stdout");
  } else {
    c2t_log_debug("clipboard", "Binary content is not written to stdout");
  }

  for (size_t attempt = 1; attempt <= delivery_attempts; ++attempt) {
    if (deliver_encrypted_payload(event, source)) {
      queue_lock();
      total_clipboard_bytes += event->length;
      total_clipboard_events++;
      queue_unlock();
      return;
    }
    if (attempt < delivery_attempts) {
      c2t_log_warning(
          "clipboard", "Delivery attempt %llu/%llu failed; retrying",
          (unsigned long long)attempt, (unsigned long long)delivery_attempts);
      retry_delay(attempt);
    }
  }
  c2t_log_error("clipboard", "Clipboard delivery failed after %llu attempts",
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
    while (!queue_head && !stopping)
      queue_wait();
    if (!queue_head && stopping) {
      queue_unlock();
      break;
    }
    clipboard_event_t *event = queue_head;
    queue_head = event->next;
    if (!queue_head)
      queue_tail = nullptr;
    queue_unlock();

    deliver_event(event);

    size_t allocation_size = event->allocation_size;
    int arena_owned = c2t_arena_contains(&clipboard_arena, event);
    queue_lock();
    queue_bytes -= allocation_size;
    --queue_items;
    if (arena_owned && queue_items == 0) {
      c2t_arena_reset(&clipboard_arena);
    } else {
      c2t_secure_zero(event, allocation_size);
    }
    queue_unlock();

    if (!arena_owned)
      free(event);
  }
  telegram_http_thread_cleanup();
#ifdef _WIN32
  return 0;
#else
  return nullptr;
#endif
}

int clipboard_output_init(void) {
  if (worker_started)
    return 1;
  if (!c2t_crypto_init()) {
    c2t_log_error("clipboard", "Unable to initialize crypto session key");
    return 0;
  }
  (void)c2t_arena_init(&clipboard_arena, 256U * 1024U);
  maximum_queue_bytes = c2t_config_get()->queue_max_bytes;
  maximum_queue_items = c2t_config_get()->queue_max_items;
  delivery_attempts = c2t_config_get()->delivery_attempts;
  retry_delay_ms = c2t_config_get()->retry_delay_ms;
  stopping = 0;
  clipboard_set_paused(0);
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
  if (!worker_started)
    c2t_log_error("clipboard", "Unable to start delivery worker");
#ifdef _WIN32
  if (!worker_started)
    DeleteCriticalSection(&queue_mutex);
#endif
  return worker_started;
}

static uint64_t last_duplicate_hash;
static size_t last_duplicate_length;
static uint64_t last_duplicate_time_ms;
static int has_duplicate_previous;

int clipboard_is_paused(void) {
  return atomic_load_explicit(&clipboard_paused, memory_order_relaxed);
}

void clipboard_set_paused(int paused) {
  atomic_store_explicit(&clipboard_paused, paused ? 1 : 0,
                        memory_order_relaxed);
}

int clipboard_toggle_paused(void) {
  return atomic_fetch_xor_explicit(&clipboard_paused, 1,
                                   memory_order_relaxed) ^
         1;
}

void clipboard_output_flush(void) {
  queue_lock();
  queue_signal();
  queue_unlock();
}

void clipboard_get_status_info(char *buffer, size_t max_len) {
  if (!buffer || max_len == 0)
    return;

  const c2t_config_t *config = c2t_config_get();
  int paused = clipboard_is_paused();

  queue_lock();
  size_t cur_items = queue_items;
  size_t cur_bytes = queue_bytes;
  uint64_t tot_bytes = total_clipboard_bytes;
  uint64_t tot_events = total_clipboard_events;
  queue_unlock();

  char size_str[32] = {};
  if (cur_bytes < 1024) {
    snprintf(size_str, sizeof(size_str), "%llu B",
             (unsigned long long)cur_bytes);
  } else if (cur_bytes < 1024 * 1024) {
    snprintf(size_str, sizeof(size_str), "%.1f KB", (double)cur_bytes / 1024.0);
  } else {
    snprintf(size_str, sizeof(size_str), "%.2f MB",
             (double)cur_bytes / (1024.0 * 1024.0));
  }

  char tot_str[64] = {};
  if (tot_bytes < 1024) {
    snprintf(tot_str, sizeof(tot_str), "%llu B", (unsigned long long)tot_bytes);
  } else if (tot_bytes < 1024 * 1024) {
    snprintf(tot_str, sizeof(tot_str), "%.2f KB (%llu B)",
             (double)tot_bytes / 1024.0, (unsigned long long)tot_bytes);
  } else {
    snprintf(tot_str, sizeof(tot_str), "%.2f MB (%llu bytes)",
             (double)tot_bytes / (1024.0 * 1024.0),
             (unsigned long long)tot_bytes);
  }

  const char *status_str = config->disable_clipboard
                               ? "❌ <b>DISABLED</b> (Configured OFF)"
                               : (paused ? "⏸️ <b>PAUSED</b> (Muted)"
                                         : "🟢 <b>ACTIVE</b> (Monitoring)");

  snprintf(buffer, max_len,
           "📋 <b>Clipboard Monitor Status</b>\n\n"
           "• <b>Status:</b> %s\n"
           "• <b>Total Delivered:</b> %s in %llu events\n"
           "• <b>Delivery Queue:</b> %llu items (%s)\n"
           "• <b>Queue Limits:</b> %llu items / %llu MB\n"
           "• <b>File Handling:</b> %s\n"
           "• <b>Deduplication:</b> %s\n"
           "• <b>Window Metadata:</b> %s\n"
           "• <b>RAM Encryption:</b> 🔒 Active (ChaCha20-Poly1305)\n"
           "• <b>Delivery Retry:</b> %llu attempts (%llu ms delay)",
           status_str, tot_str, (unsigned long long)tot_events,
           (unsigned long long)cur_items, size_str,
           (unsigned long long)config->queue_max_items,
           (unsigned long long)(config->queue_max_bytes / (1024 * 1024)),
           config->telegram_send_files ? "Enabled" : "Disabled",
           config->telegram_deduplicate ? "Enabled" : "Disabled",
           config->telegram_send_window_info ? "Enabled" : "Disabled",
           (unsigned long long)config->delivery_attempts,
           (unsigned long long)config->retry_delay_ms);
}

uint64_t clipboard_get_total_bytes(void) {
  queue_lock();
  uint64_t val = total_clipboard_bytes;
  queue_unlock();
  return val;
}

uint64_t clipboard_get_total_events(void) {
  queue_lock();
  uint64_t val = total_clipboard_events;
  queue_unlock();
  return val;
}

void clipboard_output(const void *data, size_t length, const char *mime_type,
                      const c2t_clipboard_source_t *source) {
  if (clipboard_is_paused()) {
    c2t_log_debug("clipboard", "Ignoring clipboard event: monitoring paused");
    return;
  }
  if ((!data && length != 0) || !mime_type || !*mime_type) {
    c2t_log_warning("clipboard", "Ignoring invalid clipboard payload");
    return;
  }
  if (!worker_started) {
    c2t_log_error("clipboard", "Delivery worker is not running");
    return;
  }
  size_t mime_length = 0;
  while (mime_length < C2T_MIME_CAPACITY && mime_type[mime_length])
    ++mime_length;
  if (mime_length == C2T_MIME_CAPACITY ||
      length > SIZE_MAX - sizeof(clipboard_event_t)) {
    c2t_log_warning("clipboard", "Ignoring oversized clipboard metadata");
    return;
  }

  int has_time;
  uint64_t now_ms = current_time_ms(&has_time);

  uint64_t hash = content_hash(data, length, mime_type, source);

  if (has_duplicate_previous && has_time && hash == last_duplicate_hash &&
      length == last_duplicate_length && now_ms >= last_duplicate_time_ms) {
    if (now_ms - last_duplicate_time_ms < DUPLICATE_WINDOW_MS) {
      c2t_log_debug("clipboard",
                    "Ignoring repeated platform event within %u ms",
                    DUPLICATE_WINDOW_MS);
      return;
    }
  }

  size_t allocation_size = sizeof(clipboard_event_t) + length;
  queue_lock();
  int queue_full =
      stopping || queue_items >= maximum_queue_items ||
      allocation_size > maximum_queue_bytes - (queue_bytes < maximum_queue_bytes
                                                   ? queue_bytes
                                                   : maximum_queue_bytes);
  if (queue_full) {
    queue_unlock();
    c2t_log_warning("clipboard",
                    "Delivery queue full; clipboard event dropped "
                    "(size=%llu bytes)",
                    (unsigned long long)length);
    return;
  }

  clipboard_event_t *event =
      (clipboard_event_t *)c2t_arena_alloc(&clipboard_arena, allocation_size);
  if (!event) {
    if (queue_items == 0) {
      c2t_arena_reset(&clipboard_arena);
      event = (clipboard_event_t *)c2t_arena_alloc(&clipboard_arena,
                                                   allocation_size);
    }
  }
  if (!event) {
    event = malloc(allocation_size);
  }
  if (!event) {
    queue_unlock();
    c2t_log_error("clipboard", "Not enough memory to queue clipboard data");
    return;
  }
  event->next = nullptr;
  event->length = length;
  event->allocation_size = allocation_size;
  memcpy(event->mime_type, mime_type, mime_length + 1);
  event->has_source = source != nullptr;
  if (source)
    event->source = *source;
  else
    memset(&event->source, 0, sizeof(event->source));
  if (!c2t_crypto_get_random_bytes(event->nonce, C2T_CRYPTO_NONCE_SIZE)) {
    queue_unlock();
    c2t_secure_zero(event, allocation_size);
    if (!c2t_arena_contains(&clipboard_arena, event)) {
      free(event);
    }
    c2t_log_error("clipboard",
                  "Failed to generate random nonce for RAM encryption");
    return;
  }
  if (length > 0 &&
      !c2t_crypto_encrypt(data, length, event->nonce, event->encrypted_data)) {
    queue_unlock();
    c2t_secure_zero(event, allocation_size);
    if (!c2t_arena_contains(&clipboard_arena, event)) {
      free(event);
    }
    c2t_log_error("clipboard", "Failed to encrypt clipboard data in RAM");
    return;
  }

  if (queue_tail)
    queue_tail->next = event;
  else
    queue_head = event;
  queue_tail = event;
  queue_bytes += allocation_size;
  ++queue_items;
  queue_signal();
  queue_unlock();

  c2t_log_info("clipboard", "Queued content: type=%s, size=%llu bytes",
               mime_type, (unsigned long long)length);

  last_duplicate_hash = hash;
  last_duplicate_length = length;
  if (has_time)
    last_duplicate_time_ms = now_ms;
  has_duplicate_previous = has_time;
}

void clipboard_output_cleanup(void) {
  if (!worker_started)
    return;
  queue_lock();
  stopping = 1;
  queue_signal();
  queue_unlock();
#ifdef _WIN32
  WaitForSingleObject(worker_thread, INFINITE);
  CloseHandle(worker_thread);
  worker_thread = nullptr;
  DeleteCriticalSection(&queue_mutex);
#else
  (void)pthread_join(worker_thread, nullptr);
#endif
  worker_started = 0;

  queue_lock();
  clipboard_event_t *current = queue_head;
  while (current) {
    clipboard_event_t *next = current->next;
    c2t_secure_zero(current, current->allocation_size);
    if (!c2t_arena_contains(&clipboard_arena, current)) {
      free(current);
    }
    current = next;
  }
  c2t_arena_destroy(&clipboard_arena);
  queue_head = nullptr;
  queue_tail = nullptr;
  queue_bytes = 0;
  queue_items = 0;
  queue_unlock();

  c2t_crypto_cleanup();

  last_duplicate_hash = 0;
  last_duplicate_length = 0;
  last_duplicate_time_ms = 0;
  has_duplicate_previous = 0;
#if defined(__GLIBC__) && !defined(_WIN32)
  malloc_trim(0);
#endif
}
