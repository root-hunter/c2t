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

#include "clipboard_output.h"
#include "../config/config.h"
#include "../crypto/crypto.h"
#include "../files/files.h"
#include "../logging/logging.h"
#include "../telegram/telegram.h"
#include "../telegram/telegram_platform.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <errno.h>
#include <pthread.h>
#include <time.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
static volatile int clipboard_paused;

#ifdef _WIN32
static CRITICAL_SECTION queue_mutex;
static CONDITION_VARIABLE queue_condition;
static HANDLE worker_thread;

static void queue_lock(void) { EnterCriticalSection(&queue_mutex); }
static void queue_unlock(void) { LeaveCriticalSection(&queue_mutex); }
static void queue_wait(void)
{
    (void)SleepConditionVariableCS(&queue_condition, &queue_mutex, INFINITE);
}
static void queue_signal(void) { WakeConditionVariable(&queue_condition); }
#else
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_condition = PTHREAD_COND_INITIALIZER;
static pthread_t worker_thread;

static void queue_lock(void) { (void)pthread_mutex_lock(&queue_mutex); }
static void queue_unlock(void) { (void)pthread_mutex_unlock(&queue_mutex); }
static void queue_wait(void)
{
    (void)pthread_cond_wait(&queue_condition, &queue_mutex);
}
static void queue_signal(void) { (void)pthread_cond_signal(&queue_condition); }
#endif

static uint64_t content_hash(const void *data, size_t length,
                             const char *mime_type,
                             const c2t_clipboard_source_t *source)
{
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
            for (size_t index = 0;
                 index < capacities[field] && fields[field][index]; ++index) {
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

static uint64_t current_time_ms(int *available)
{
#ifdef _WIN32
    *available = 1;
    return GetTickCount64();
#else
    struct timespec now;
    *available = clock_gettime(CLOCK_MONOTONIC, &now) == 0;
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
#endif
}

static void retry_delay(size_t attempt)
{
    size_t delay = retry_delay_ms > 60000 / attempt
        ? 60000 : retry_delay_ms * attempt;
#ifdef _WIN32
    Sleep((DWORD)delay);
#else
    struct timespec duration = {
        .tv_sec = (time_t)(delay / 1000),
        .tv_nsec = (long)(delay % 1000) * 1000000L
    };
    while (nanosleep(&duration, &duration) != 0 && errno == EINTR) {
    }
#endif
}

static int deliver_encrypted_payload(const clipboard_event_t *event,
                                     const c2t_clipboard_source_t *source)
{
    if (event->length > 0 && event->length < 1024) {
        unsigned char path_buf[1024];
        if (c2t_crypto_decrypt(event->encrypted_data, event->length, event->nonce, path_buf)) {
            int file_result = c2t_file_try_clipboard_path(path_buf, event->length, event->mime_type, source);
            c2t_secure_zero(path_buf, sizeof(path_buf));
            if (file_result != C2T_FILE_NOT_HANDLED) {
                return file_result == C2T_FILE_SENT;
            }
        }
    }

    return telegram_send_encrypted_data(event->encrypted_data, event->length,
                                        event->nonce, event->mime_type, source);
}

static void deliver_event(const clipboard_event_t *event)
{
    const c2t_clipboard_source_t *source =
        event->has_source ? &event->source : nullptr;

    if (strncmp(event->mime_type, "text/", 5) == 0) {
        if (event->length > 0) {
            unsigned char chunk_buf[1024];
            c2t_secure_lock(chunk_buf, sizeof(chunk_buf));
            size_t written = 0;
            while (written < event->length) {
                size_t chunk = event->length - written < sizeof(chunk_buf)
                    ? event->length - written : sizeof(chunk_buf);
                if (!c2t_crypto_decrypt_offset(event->encrypted_data + written, written,
                                              chunk, event->nonce, chunk_buf)) {
                    break;
                }
                if (fwrite(chunk_buf, 1, chunk, stdout) != chunk)
                    c2t_log_warning("clipboard", "Unable to write content chunk to stdout");
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
            return;
        }
        if (attempt < delivery_attempts) {
            c2t_log_warning("clipboard",
                            "Delivery attempt %llu/%llu failed; retrying",
                            (unsigned long long)attempt,
                            (unsigned long long)delivery_attempts);
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

        queue_lock();
        queue_bytes -= event->allocation_size;
        --queue_items;
        queue_unlock();
        c2t_secure_zero(event, event->allocation_size);
        free(event);
    }
    telegram_http_thread_cleanup();
#ifdef _WIN32
    return 0;
#else
    return nullptr;
#endif
}

int clipboard_output_init(void)
{
    if (worker_started)
        return 1;
    if (!c2t_crypto_init()) {
        c2t_log_error("clipboard", "Unable to initialize crypto session key");
        return 0;
    }
    maximum_queue_bytes = c2t_config_get()->queue_max_bytes;
    maximum_queue_items = c2t_config_get()->queue_max_items;
    delivery_attempts = c2t_config_get()->delivery_attempts;
    retry_delay_ms = c2t_config_get()->retry_delay_ms;
    stopping = 0;
    clipboard_paused = 0;
#ifdef _WIN32
    InitializeCriticalSection(&queue_mutex);
    InitializeConditionVariable(&queue_condition);
    worker_thread = CreateThread(nullptr, 0, delivery_worker, nullptr, 0, nullptr);
    worker_started = worker_thread != nullptr;
#else
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 128 * 1024);
    worker_started = pthread_create(&worker_thread, &attr, delivery_worker,
                                    nullptr) == 0;
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

int clipboard_is_paused(void)
{
    return clipboard_paused;
}

void clipboard_set_paused(int paused)
{
    clipboard_paused = paused ? 1 : 0;
}

int clipboard_toggle_paused(void)
{
    clipboard_paused = !clipboard_paused;
    return clipboard_paused;
}

void clipboard_output(const void *data, size_t length, const char *mime_type,
                      const c2t_clipboard_source_t *source)
{
    if (clipboard_paused) {
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
    int queue_full = stopping || queue_items >= maximum_queue_items ||
        allocation_size > maximum_queue_bytes -
            (queue_bytes < maximum_queue_bytes ? queue_bytes
                                                 : maximum_queue_bytes);
    if (queue_full) {
        queue_unlock();
        c2t_log_warning("clipboard",
                        "Delivery queue full; clipboard event dropped "
                        "(size=%llu bytes)", (unsigned long long)length);
        return;
    }

    clipboard_event_t *event = malloc(allocation_size);
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
        free(event);
        c2t_log_error("clipboard", "Failed to generate random nonce for RAM encryption");
        return;
    }
    if (length > 0 && !c2t_crypto_encrypt(data, length, event->nonce, event->encrypted_data)) {
        queue_unlock();
        c2t_secure_zero(event, allocation_size);
        free(event);
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

void clipboard_output_cleanup(void)
{
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
        free(current);
        current = next;
    }
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
}
