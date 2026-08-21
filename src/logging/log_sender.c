/*
 * Copyright (C) 2026 Antonio Ricciardi
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
#include "logging.h"
#include "../runtime/runtime.h"
#include "../telegram/telegram.h"

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
#endif

#define MAX_LOG_READ_BYTES (5U * 1024U * 1024U)

static int worker_started;
static int stopping;
static size_t last_sent_offset;

#ifdef _WIN32
static CRITICAL_SECTION sender_mutex;
static CONDITION_VARIABLE sender_condition;
static HANDLE worker_thread;
static int mutex_initialized;

static void ensure_mutex_init(void)
{
    if (!mutex_initialized) {
        InitializeCriticalSection(&sender_mutex);
        InitializeConditionVariable(&sender_condition);
        mutex_initialized = 1;
    }
}
static void sender_lock(void) { ensure_mutex_init(); EnterCriticalSection(&sender_mutex); }
static void sender_unlock(void) { LeaveCriticalSection(&sender_mutex); }
static void sender_wait(size_t seconds)
{
    DWORD timeout = seconds > (0xFFFFFFFFU / 1000U) ? INFINITE : (DWORD)(seconds * 1000U);
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
static void sender_wait(size_t seconds)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += (time_t)seconds;
    (void)pthread_cond_timedwait(&sender_condition, &sender_mutex, &ts);
}
static void sender_signal(void) { (void)pthread_cond_signal(&sender_condition); }
#endif

static char *escape_html(const char *input, size_t input_len, size_t *out_len)
{
    size_t extra = 0;
    for (size_t i = 0; i < input_len; ++i) {
        if (input[i] == '&') extra += 4;
        else if (input[i] == '<') extra += 3;
        else if (input[i] == '>') extra += 3;
    }
    char *escaped = malloc(input_len + extra + 1);
    if (!escaped) return NULL;

    size_t out_idx = 0;
    for (size_t i = 0; i < input_len; ++i) {
        if (input[i] == '&') {
            memcpy(escaped + out_idx, "&amp;", 5);
            out_idx += 5;
        } else if (input[i] == '<') {
            memcpy(escaped + out_idx, "&lt;", 4);
            out_idx += 4;
        } else if (input[i] == '>') {
            memcpy(escaped + out_idx, "&gt;", 4);
            out_idx += 4;
        } else {
            escaped[out_idx++] = input[i];
        }
    }
    escaped[out_idx] = '\0';
    if (out_len) *out_len = out_idx;
    return escaped;
}

static int send_log_payload(int on_demand)
{
    const char *path = c2t_runtime_log_path();
    if (!path) {
        if (on_demand) {
            telegram_send_html("ℹ️ <b>c2t Logs</b>\n<i>Log system is not active or no log file path configured.</i>");
        }
        return 0;
    }

    FILE *stream = fopen(path, "rb");
    if (!stream) {
        if (on_demand) {
            telegram_send_html("ℹ️ <b>c2t Logs</b>\n<i>Log file is currently empty or unavailable.</i>");
        }
        return 0;
    }

    if (fseek(stream, 0, SEEK_END) != 0) {
        fclose(stream);
        return 0;
    }

    long file_size = ftell(stream);
    if (file_size < 0) {
        fclose(stream);
        return 0;
    }

    size_t current_size = (size_t)file_size;
    if (current_size < last_sent_offset) {
        /* File was truncated or reset */
        last_sent_offset = 0;
    }

    if (current_size <= last_sent_offset) {
        fclose(stream);
        if (on_demand) {
            telegram_send_html("ℹ️ <b>c2t Logs</b>\n<i>No new logs since last dispatch.</i>");
        }
        return 1;
    }

    size_t unread_bytes = current_size - last_sent_offset;
    if (unread_bytes > MAX_LOG_READ_BYTES)
        unread_bytes = MAX_LOG_READ_BYTES;

    if (fseek(stream, (long)last_sent_offset, SEEK_SET) != 0) {
        fclose(stream);
        return 0;
    }

    char *buffer = malloc(unread_bytes + 1);
    if (!buffer) {
        c2t_log_error("log_sender", "Unable to allocate memory for log dispatch");
        fclose(stream);
        return 0;
    }

    size_t read_bytes = fread(buffer, 1, unread_bytes, stream);
    fclose(stream);

    if (read_bytes == 0) {
        free(buffer);
        if (on_demand) {
            telegram_send_html("ℹ️ <b>c2t Logs</b>\n<i>No new logs available.</i>");
        }
        return 1;
    }
    buffer[read_bytes] = '\0';

    int sent = 0;

    /* If log chunk is small enough (<= 3000 bytes), format nicely in HTML code block */
    if (read_bytes <= 3000) {
        size_t escaped_len = 0;
        char *escaped = escape_html(buffer, read_bytes, &escaped_len);
        if (escaped) {
            static const char prefix[] = "📋 <b>c2t Execution Logs</b>\n<pre><code class=\"language-log\">";
            static const char suffix[] = "</code></pre>";
            size_t total_len = strlen(prefix) + escaped_len + strlen(suffix) + 1;
            char *msg = malloc(total_len);
            if (msg) {
                snprintf(msg, total_len, "%s%s%s", prefix, escaped, suffix);
                sent = telegram_send_html(msg);
                free(msg);
            }
            free(escaped);
        }
    }

    /* If sending as code block failed or buffer was larger than 3000 bytes, send as .log document file */
    if (!sent) {
        char filename[64];
        time_t now = time(NULL);
        struct tm utc_tm;
#ifdef _WIN32
        gmtime_s(&utc_tm, &now);
#else
        gmtime_r(&now, &utc_tm);
#endif
        snprintf(filename, sizeof(filename), "c2t_%04d%02d%02d_%02d%02d%02d.log",
                 utc_tm.tm_year + 1900, utc_tm.tm_mon + 1, utc_tm.tm_mday,
                 utc_tm.tm_hour, utc_tm.tm_min, utc_tm.tm_sec);

        c2t_log_info("log_sender", "Sending log file to Telegram (%llu bytes, name=%s)",
                     (unsigned long long)read_bytes, filename);

        sent = telegram_send_file(buffer, read_bytes, "text/plain", filename, NULL);
    }

    free(buffer);

    if (sent) {
        last_sent_offset += read_bytes;
        c2t_log_info("log_sender", "Logs successfully delivered to Telegram (%llu bytes)",
                     (unsigned long long)read_bytes);
    } else {
        c2t_log_warning("log_sender", "Failed to send logs to Telegram; will retry");
    }

    return sent;
}

static void send_log_chunk(void)
{
    send_log_payload(0);
}

int c2t_log_sender_dispatch_now(void)
{
    ensure_mutex_init();
    sender_lock();
    int result = send_log_payload(1);
    sender_unlock();
    return result;
}

#ifdef _WIN32
static DWORD WINAPI log_sender_worker_func(void *context)
#else
static void *log_sender_worker_func(void *context)
#endif
{
    (void)context;
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

        send_log_chunk();

        if (is_stopping)
            break;
    }

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

int c2t_log_sender_init(void)
{
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

    c2t_log_info("log_sender", "Initializing periodic Telegram log sender (interval=%llu s)",
                 (unsigned long long)config->telegram_log_interval_sec);

#ifdef _WIN32
    worker_thread = CreateThread(NULL, 0, log_sender_worker_func, NULL, 0, NULL);
    worker_started = worker_thread != NULL;
#else
    worker_started = pthread_create(&worker_thread, NULL, log_sender_worker_func, NULL) == 0;
#endif

    if (!worker_started)
        c2t_log_error("log_sender", "Unable to start periodic log sender worker thread");

    return worker_started;
}

void c2t_log_sender_cleanup(void)
{
    if (!worker_started)
        return;

    sender_lock();
    stopping = 1;
    sender_signal();
    sender_unlock();

#ifdef _WIN32
    WaitForSingleObject(worker_thread, INFINITE);
    CloseHandle(worker_thread);
    worker_thread = NULL;
#else
    (void)pthread_join(worker_thread, NULL);
#endif

    worker_started = 0;
    stopping = 0;
    c2t_log_info("log_sender", "Periodic Telegram log sender stopped");
}
