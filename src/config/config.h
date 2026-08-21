#ifndef C2T_CONFIG_H
#define C2T_CONFIG_H

#include <stddef.h>

typedef struct {
    int verbose;
    int telegram_enabled;
    int telegram_deduplicate;
    int telegram_send_files;
    int telegram_send_window_info;
    size_t telegram_max_file_bytes;
    size_t queue_max_bytes;
    size_t queue_max_items;
    size_t delivery_attempts;
    size_t retry_delay_ms;
    const char *telegram_bot_token;
    const char *telegram_chat_id;
} c2t_config_t;

void c2t_config_load_environment(void);
const char *c2t_config_apply_arguments(int argc, char **argv);
const c2t_config_t *c2t_config_get(void);

#endif
