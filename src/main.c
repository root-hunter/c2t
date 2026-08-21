#include "clipboard/clipboard.h"
#include "config/config.h"
#include "logging/logging.h"
#include "telegram/telegram.h"

#ifndef _WIN32
#include <signal.h>
#endif

int main(int argc, char **argv)
{
    c2t_config_load(argv[0]);
    const char *invalid_option = c2t_config_apply_arguments(argc, argv);
    c2t_log_init();
#ifndef _WIN32
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        c2t_log_warning("main", "Unable to disable fatal SIGPIPE handling");
#endif
    if (invalid_option) {
        c2t_log_error("main", "Unknown option: %s", invalid_option);
        c2t_log_error("main", "Supported options: -v, --verbose, --send-files, "
                      "--send-window-info");
        return 2;
    }

    c2t_log_info("main", "c2t starting (verbose logging enabled)");
    c2t_log_info("config", "File path uploads=%s, max file size=%llu bytes",
                 c2t_config_get()->telegram_send_files ? "enabled" : "disabled",
                 (unsigned long long)c2t_config_get()->telegram_max_file_bytes);
    c2t_log_info("config", "Clipboard source window information=%s",
                 c2t_config_get()->telegram_send_window_info
                     ? "enabled" : "disabled");
    c2t_log_info("config", "Delivery queue: max_items=%llu, max_bytes=%llu",
                 (unsigned long long)c2t_config_get()->queue_max_items,
                 (unsigned long long)c2t_config_get()->queue_max_bytes);
    c2t_log_info("config", "Delivery retry policy: attempts=%llu, delay=%llu ms",
                 (unsigned long long)c2t_config_get()->delivery_attempts,
                 (unsigned long long)c2t_config_get()->retry_delay_ms);
    if (!telegram_init()) {
        c2t_log_error("main", "Telegram initialization failed");
        return 1;
    }
    if (!clipboard_output_init()) {
        c2t_log_error("main", "Clipboard delivery worker initialization failed");
        telegram_cleanup();
        return 1;
    }

    c2t_log_info("main", "Starting clipboard listener");
    int result = clipboard_listen();
    c2t_log_info("main", "Clipboard listener stopped with result %d", result);
    clipboard_output_cleanup();
    telegram_cleanup();
    c2t_log_info("main", "Shutdown complete");
    return result;
}
