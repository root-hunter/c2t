#include "config.h"
#include "embedded_config.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

static const char *configured_value(const char *name, char *embedded,
                                    size_t embedded_capacity)
{
    const char *value = getenv(name);
    if (value)
        return value;
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

void c2t_config_load_environment(void)
{
    config.verbose = configured_flag("C2T_VERBOSE");
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

const char *c2t_config_apply_arguments(int argc, char **argv)
{
    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "-v") == 0 ||
            strcmp(argv[index], "--verbose") == 0) {
            config.verbose = 1;
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

const c2t_config_t *c2t_config_get(void)
{
    return &config;
}
