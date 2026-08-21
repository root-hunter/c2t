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

#include "telegram_listener.h"
#include "../config/config.h"
#include "../clipboard/clipboard_output.h"
#include "../logging/logging.h"
#include "../logging/log_sender.h"
#include "telegram.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <pthread.h>
#include <unistd.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define POLL_TIMEOUT_SECONDS 15

static int listener_started;
static volatile int stopping;

#ifdef _WIN32
static HANDLE listener_thread;
#else
static pthread_t listener_thread;
#endif

#ifndef _WIN32
#include <strings.h>
#endif

static int match_command(const char *text, const char *cmd)
{
    if (!text || !cmd) return 0;
    while (isspace((unsigned char)*text)) text++;
    if (*text == '/') text++;
    if (*cmd == '/') cmd++;

    size_t cmd_len = strlen(cmd);
#ifdef _WIN32
    if (_strnicmp(text, cmd, cmd_len) != 0) return 0;
#else
    if (strncasecmp(text, cmd, cmd_len) != 0) return 0;
#endif

    char next = text[cmd_len];
    return (next == '\0' || next == '@' || isspace((unsigned char)next));
}

static void handle_command(const char *text, const char *chat_id, const char *username)
{
    (void)username;
    const c2t_config_t *config = c2t_config_get();
    if (!config->telegram_chat_id || !*config->telegram_chat_id) {
        c2t_log_warning("listener", "Telegram chat_id is not configured");
        return;
    }

    const char *cfg_chat = config->telegram_chat_id;
    while (isspace((unsigned char)*cfg_chat)) cfg_chat++;
    while (isspace((unsigned char)*chat_id)) chat_id++;

    if (strcmp(chat_id, cfg_chat) != 0) {
        c2t_log_warning("listener", "Ignored command '%s' from unauthorized chat_id: %s (authorized: %s)",
                        text, chat_id, cfg_chat);
        return;
    }

    c2t_log_info("listener", "Executing Telegram command '%s' from chat %s", text, chat_id);

    if (match_command(text, "pause") || match_command(text, "mute") || match_command(text, "stop_listen") || match_command(text, "disable")) {
        clipboard_set_paused(1);
        c2t_log_info("listener", "Clipboard monitoring paused by Telegram command");
        telegram_send_html("⏸️ <b>Clipboard Monitoring Paused</b>\n<i>c2t will ignore clipboard changes until resumed with /resume or /toggle.</i>");
    } else if (match_command(text, "resume") || match_command(text, "unmute") || match_command(text, "start_listen") || match_command(text, "enable")) {
        clipboard_set_paused(0);
        c2t_log_info("listener", "Clipboard monitoring resumed by Telegram command");
        telegram_send_html("▶️ <b>Clipboard Monitoring Resumed</b>\n<i>c2t is now capturing and forwarding clipboard changes.</i>");
    } else if (match_command(text, "toggle")) {
        int is_now_paused = clipboard_toggle_paused();
        c2t_log_info("listener", "Clipboard monitoring toggled to %s by Telegram command",
                     is_now_paused ? "paused" : "active");
        if (is_now_paused) {
            telegram_send_html("⏸️ <b>Clipboard Monitoring Paused</b>\n<i>c2t will ignore clipboard changes until resumed.</i>");
        } else {
            telegram_send_html("▶️ <b>Clipboard Monitoring Resumed</b>\n<i>c2t is now capturing and forwarding clipboard changes.</i>");
        }
    } else if (match_command(text, "logs") || match_command(text, "log")) {
        c2t_log_info("listener", "Flushing logs on-demand by /logs command");
        c2t_log_sender_dispatch_now();
    } else if (match_command(text, "status") || match_command(text, "ping")) {
        int paused = clipboard_is_paused();
        char status_msg[1024];
        snprintf(status_msg, sizeof(status_msg),
                 "🤖 <b>c2t Daemon Status</b>\n\n"
                 "• <b>Status:</b> 🟢 Active &amp; Running\n"
                 "• <b>Clipboard Monitoring:</b> %s\n"
                 "• <b>Periodic Logs:</b> %s (Interval: %llu s)\n"
                 "• <b>File Uploads:</b> %s\n"
                 "• <b>Window Info:</b> %s\n"
                 "• <b>Delivery Queue:</b> %llu items / %llu bytes",
                 paused ? "⏸️ <b>PAUSED</b> (Muted)" : "🟢 <b>ACTIVE</b> (Capturing)",
                 config->telegram_send_logs ? "Enabled" : "On-demand only (/logs)",
                 (unsigned long long)config->telegram_log_interval_sec,
                 config->telegram_send_files ? "Enabled" : "Disabled",
                 config->telegram_send_window_info ? "Enabled" : "Disabled",
                 (unsigned long long)config->queue_max_items,
                 (unsigned long long)config->queue_max_bytes);
        telegram_send_html(status_msg);
    } else if (match_command(text, "help") || match_command(text, "start")) {
        char help_msg[768];
        snprintf(help_msg, sizeof(help_msg),
                 "💡 <b>c2t Telegram Commands</b>\n\n"
                 "• <code>/pause</code> - Pause clipboard monitoring (mute copies)\n"
                 "• <code>/resume</code> - Resume clipboard monitoring\n"
                 "• <code>/toggle</code> - Toggle pause / resume monitoring\n"
                 "• <code>/logs</code> - Flush and retrieve execution logs\n"
                 "• <code>/status</code> - View daemon status &amp; monitoring state\n"
                 "• <code>/help</code> - Show this help guide");
        telegram_send_html(help_msg);
    }
}

static void on_telegram_command_received(int64_t update_id,
                                        const char *chat_id,
                                        const char *username,
                                        const char *text,
                                        void *user_data)
{
    (void)update_id;
    (void)user_data;
    if (text && *text) {
        handle_command(text, chat_id ? chat_id : "", username ? username : "");
    }
}

#ifdef _WIN32
static DWORD WINAPI telegram_listener_worker_func(void *context)
#else
static void *telegram_listener_worker_func(void *context)
#endif
{
    (void)context;
    int64_t offset = 0;

    c2t_log_info("listener", "Telegram command listener started (long-polling timeout=%ds)",
                 POLL_TIMEOUT_SECONDS);

    /* Fast initial check: drain and advance offset so all pending / initial updates are processed immediately */
    const c2t_config_t *init_config = c2t_config_get();
    if (init_config->telegram_enabled && init_config->telegram_bot_token && init_config->telegram_chat_id) {
        telegram_poll_updates_callback(
            init_config->telegram_bot_token,
            &offset,
            0,
            on_telegram_command_received,
            NULL
        );
    }

    while (!stopping) {
        const c2t_config_t *config = c2t_config_get();
        if (!config->telegram_enabled || !config->telegram_bot_token || !config->telegram_chat_id) {
#ifndef _WIN32
            sleep(1);
#else
            Sleep(1000);
#endif
            continue;
        }

        telegram_poll_updates_callback(
            config->telegram_bot_token,
            &offset,
            POLL_TIMEOUT_SECONDS,
            on_telegram_command_received,
            NULL
        );
    }

    c2t_log_info("listener", "Telegram command listener stopped");

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

int c2t_telegram_listener_init(void)
{
    const c2t_config_t *config = c2t_config_get();
    if (!config->telegram_enabled || !config->telegram_bot_token || !config->telegram_chat_id) {
        c2t_log_debug("listener", "Telegram listener disabled: Telegram not enabled or unconfigured");
        return 1;
    }

    if (listener_started)
        return 1;

    stopping = 0;

#ifdef _WIN32
    listener_thread = CreateThread(NULL, 0, telegram_listener_worker_func, NULL, 0, NULL);
    listener_started = listener_thread != NULL;
#else
    listener_started = pthread_create(&listener_thread, NULL, telegram_listener_worker_func, NULL) == 0;
#endif

    if (!listener_started) {
        c2t_log_error("listener", "Unable to start Telegram command listener thread");
        return 0;
    }

    return 1;
}

void c2t_telegram_listener_cleanup(void)
{
    if (!listener_started)
        return;

    stopping = 1;

#ifdef _WIN32
    WaitForSingleObject(listener_thread, INFINITE);
    CloseHandle(listener_thread);
    listener_thread = NULL;
#else
    (void)pthread_join(listener_thread, NULL);
#endif

    listener_started = 0;
    stopping = 0;
}
