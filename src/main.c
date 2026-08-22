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

#include "clipboard/clipboard.h"
#include "config/config.h"
#include "logging/logging.h"
#include "logging/log_sender.h"
#include "runtime/runtime.h"
#include "telegram/telegram.h"
#include "telegram/telegram_listener.h"
#include "c2t_version.h"

#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include <signal.h>
#endif

#define C2T_START_TIMEOUT_MS 10000U
#define C2T_STOP_TIMEOUT_MS 15000U

typedef enum {
    COMMAND_RUN,
    COMMAND_START,
    COMMAND_PAIR,
    COMMAND_STATUS,
    COMMAND_STOP,
    COMMAND_RESTART,
    COMMAND_HELP,
    COMMAND_VERSION,
    COMMAND_DAEMON_CHILD
} command_t;

static void print_usage(FILE *stream)
{
    fprintf(stream,
        "Usage: c2t <command> [options]\n"
        "       c2t [options]             Run in the foreground\n\n"
        "Commands:\n"
        "  start       Start c2t in the background\n"
        "  run         Run c2t in the foreground\n"
        "  pair        Pair Telegram bot interactively via deep link\n"
        "  status      Show whether c2t is running\n"
        "  stop        Stop c2t gracefully\n"
        "  restart     Stop and start c2t\n"
        "  help        Show this help\n"
        "  version     Show the version\n\n"
        "Options for start, run and restart:\n"
        "  -v, --verbose          Enable verbose logging\n"
        "  -l, --log-file         Save logs to disk\n"
        "  --send-files           Send copied files\n"
        "  --send-window-info     Include clipboard source metadata\n"
        "  --send-logs            Periodically send system log files to Telegram\n"
        "  --log-interval <sec>   Interval in seconds to send log files (5-86400)\n\n"
        "Stop options:\n"
        "  --force                Force termination after the timeout\n");
}

[[nodiscard]] static command_t parse_command(int argc, char **argv, int *option_offset)
{
    *option_offset = 1;
    if (argc < 2)
        return COMMAND_RUN;
    if (strcmp(argv[1], "--help") == 0)
        return COMMAND_HELP;
    if (strcmp(argv[1], "--version") == 0)
        return COMMAND_VERSION;
    if (strcmp(argv[1], "--daemon-child") == 0) {
        *option_offset = 2;
        return COMMAND_DAEMON_CHILD;
    }
    if (argv[1][0] == '-')
        return COMMAND_RUN;
    *option_offset = 2;
    if (strcmp(argv[1], "run") == 0)
        return COMMAND_RUN;
    if (strcmp(argv[1], "start") == 0)
        return COMMAND_START;
    if (strcmp(argv[1], "pair") == 0)
        return COMMAND_PAIR;
    if (strcmp(argv[1], "status") == 0)
        return COMMAND_STATUS;
    if (strcmp(argv[1], "stop") == 0)
        return COMMAND_STOP;
    if (strcmp(argv[1], "restart") == 0)
        return COMMAND_RESTART;
    if (strcmp(argv[1], "help") == 0)
        return COMMAND_HELP;
    if (strcmp(argv[1], "version") == 0)
        return COMMAND_VERSION;
    *option_offset = 1;
    return COMMAND_RUN;
}

[[nodiscard]] static const char *apply_service_options(int argc, char **argv,
                                                      int option_offset)
{
    return c2t_config_apply_arguments(
        argc - option_offset + 1, argv + option_offset - 1);
}

[[nodiscard]] static int show_status(int quiet)
{
    c2t_runtime_status_t status;
    int result = c2t_runtime_get_status(&status);
    if (result < 0) {
        if (!quiet)
            fprintf(stderr, "Unable to read c2t daemon state\n");
        return 1;
    }
    if (result == 0) {
        if (!quiet)
            puts("c2t is stopped");
        return 3;
    }
    if (!quiet) {
        printf("c2t is %s",
               status.state == C2T_RUNTIME_RUNNING ? "running" : "starting");
        if (status.process_id)
            printf(" (PID %lu)", status.process_id);
        putchar('\n');
        const char *path = c2t_runtime_log_path();
        if (path)
            printf("Log: %s\n", path);
    }
    return 0;
}

[[nodiscard]] static int stop_service(int force, int quiet)
{
    int result = c2t_runtime_stop(C2T_STOP_TIMEOUT_MS, force);
    if (result == 0) {
        if (!quiet)
            puts("c2t is already stopped");
        return 0;
    }
    if (result > 0) {
        if (!quiet)
            puts("c2t stopped");
        return 0;
    }
    if (result == -2)
        fprintf(stderr, "c2t did not stop within 15 seconds; retry with "
                        "'c2t stop --force'\n");
    else
        fprintf(stderr, "Unable to stop c2t\n");
    return 1;
}

[[nodiscard]] static int run_service(void)
{
    int acquired = c2t_runtime_acquire();
    if (acquired == 0) {
        fprintf(stderr, "c2t is already running\n");
        return 4;
    }
    if (acquired < 0) {
        fprintf(stderr, "Unable to create the c2t daemon state\n");
        return 1;
    }

#ifndef _WIN32
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        c2t_log_warning("main", "Unable to disable fatal SIGPIPE handling");
#endif
    c2t_log_info("main", "c2t starting (verbose logging enabled)");
    c2t_log_info("config", "File path uploads=%s, max file size=%llu bytes",
                 c2t_config_get()->telegram_send_files ? "enabled" : "disabled",
                 (unsigned long long)c2t_config_get()->telegram_max_file_bytes);
    c2t_log_info("config", "Clipboard source window information=%s",
                 c2t_config_get()->telegram_send_window_info
                     ? "enabled" : "disabled");
    c2t_log_info("config", "Periodic Telegram log sending=%s, interval=%llu s",
                 c2t_config_get()->telegram_send_logs ? "enabled" : "disabled",
                 (unsigned long long)c2t_config_get()->telegram_log_interval_sec);
    c2t_log_info("config", "Delivery queue: max_items=%llu, max_bytes=%llu",
                 (unsigned long long)c2t_config_get()->queue_max_items,
                 (unsigned long long)c2t_config_get()->queue_max_bytes);
    c2t_log_info("config", "Delivery retry policy: attempts=%llu, delay=%llu ms",
                 (unsigned long long)c2t_config_get()->delivery_attempts,
                 (unsigned long long)c2t_config_get()->retry_delay_ms);
    if (!telegram_init()) {
        c2t_log_error("main", "Telegram initialization failed");
        c2t_runtime_release();
        return 1;
    }
    if (!clipboard_output_init()) {
        c2t_log_error("main", "Clipboard delivery worker initialization failed");
        telegram_cleanup();
        c2t_runtime_release();
        return 1;
    }
    if (!c2t_log_sender_init()) {
        c2t_log_error("main", "Log sender initialization failed");
        clipboard_output_cleanup();
        telegram_cleanup();
        c2t_runtime_release();
        return 1;
    }
    if (!c2t_telegram_listener_init()) {
        c2t_log_error("main", "Telegram command listener initialization failed");
        c2t_log_sender_cleanup();
        clipboard_output_cleanup();
        telegram_cleanup();
        c2t_runtime_release();
        return 1;
    }

    c2t_runtime_mark_running();
    c2t_log_info("main", "Starting clipboard listener");
    int result = clipboard_listen();
    c2t_log_info("main", "Clipboard listener stopped with result %d", result);
    c2t_telegram_listener_cleanup();
    c2t_log_sender_cleanup();
    clipboard_output_cleanup();
    telegram_cleanup();
    c2t_log_info("main", "Shutdown complete");
    c2t_log_cleanup();
    c2t_runtime_release();
    return result;
}

int main(int argc, char **argv)
{
    int option_offset;
    command_t command = parse_command(argc, argv, &option_offset);
    if (command == COMMAND_HELP) {
        print_usage(stdout);
        return 0;
    }
    if (command == COMMAND_VERSION) {
        printf("c2t %s\n", C2T_VERSION);
        return 0;
    }
    if (command == COMMAND_STATUS) {
        if (argc != 2) {
            fprintf(stderr, "status does not accept options\n");
            return 2;
        }
        return show_status(0);
    }
    if (command == COMMAND_STOP) {
        int force = argc == 3 && strcmp(argv[2], "--force") == 0;
        if (argc > 2 && !force) {
            fprintf(stderr, "Unknown stop option: %s\n", argv[2]);
            return 2;
        }
        return stop_service(force, 0);
    }

    c2t_config_load(argv[0]);
    const char *invalid_option = apply_service_options(argc, argv, option_offset);
    c2t_log_init();
    if (invalid_option) {
        c2t_log_error("main", "Unknown command or option: %s", invalid_option);
        print_usage(stderr);
        return 2;
    }

    if (command == COMMAND_PAIR) {
        const char *token = c2t_config_get()->telegram_bot_token;
        if ((!token || !*token) && argc >= 3 && argv[2][0] != '-') {
            token = argv[2];
        }
        if (!token || !*token) {
            fprintf(stderr, "Error: TELEGRAM_BOT_TOKEN is required for pairing.\n");
            fprintf(stderr, "Usage: c2t pair [BOT_TOKEN]\n");
            return 1;
        }
        char paired_chat_id[128] = {};
        if (telegram_pair(token, nullptr, paired_chat_id, sizeof(paired_chat_id), 60)) {
            printf("\n[SUCCESS] Pairing complete!\n");
            printf("[CONFIG]  TELEGRAM_BOT_TOKEN=%s\n", token);
            printf("[CONFIG]  TELEGRAM_CHAT_ID=%s\n\n", paired_chat_id);
            return 0;
        }
        return 1;
    }

    if (command == COMMAND_RESTART) {
        if (stop_service(0, 1) != 0)
            return 1;
        command = COMMAND_START;
    }
    if (command == COMMAND_START) {
        c2t_runtime_status_t status;
        int was_running = c2t_runtime_get_status(&status);
        if (was_running > 0) {
            printf("c2t is already %s (PID %lu)\n",
                   status.state == C2T_RUNTIME_RUNNING ? "running" : "starting",
                   status.process_id);
            return 0;
        }
        int background = c2t_runtime_start_background(
            argc, argv, C2T_START_TIMEOUT_MS);
        if (background == C2T_BACKGROUND_PARENT) {
            (void)c2t_runtime_get_status(&status);
            printf("c2t started (PID %lu)\n", status.process_id);
            const char *path = c2t_runtime_log_path();
            if (path)
                printf("Log: %s\n", path);
            return 0;
        }
        if (background == C2T_BACKGROUND_ERROR) {
            fprintf(stderr, "Unable to start c2t; check %s\n",
                    c2t_runtime_log_path() ? c2t_runtime_log_path() : "the log");
            return 1;
        }
    }

    return run_service();
}
