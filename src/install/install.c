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

#include "install.h"
#include "../runtime/environment.h"
#include "../logging/logging.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef _WIN32
#include <limits.h>
#include <pwd.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#else
#include "../win32/win32_api.h"
#endif

static int get_self_executable_path(char *buf, size_t max_len) {
  if (!buf || max_len == 0) return 0;
#ifdef _WIN32
  c2t_win32_api_init();
  DWORD len = 0;
  if (g_c2t_win32.GetModuleFileNameA) {
    len = g_c2t_win32.GetModuleFileNameA(NULL, buf, (DWORD)max_len);
  }
  return (len > 0 && len < (DWORD)max_len);
#elif defined(__APPLE__)
  uint32_t size = (uint32_t)max_len;
  if (_NSGetExecutablePath(buf, &size) == 0) {
    char real[PATH_MAX];
    if (realpath(buf, real)) {
      snprintf(buf, max_len, "%s", real);
    }
    return 1;
  }
  return 0;
#elif defined(__linux__) || defined(__unix__)
  ssize_t len = readlink("/proc/self/exe", buf, max_len - 1);
  if (len > 0) {
    buf[len] = '\0';
    return 1;
  }
  return 0;
#else
  return 0;
#endif
}

#ifndef _WIN32
static void make_parent_dirs(const char *file_path) {
  char temp[PATH_MAX];
  snprintf(temp, sizeof(temp), "%s", file_path);
  char *slash = strrchr(temp, '/');
  if (!slash || slash == temp) return;
  *slash = '\0';

  for (char *p = temp + 1; *p; ++p) {
    if (*p == '/') {
      *p = '\0';
      mkdir(temp, 0755);
      *p = '/';
    }
  }
  mkdir(temp, 0755);
}
#endif

#if defined(__linux__) || (defined(__unix__) && !defined(__APPLE__))

static int install_linux_systemd_user(const char *exe_path, char *detail_msg, size_t max_len) {
  const char *home = c2t_getenv("HOME");
  if (!home || !*home) {
    struct passwd *pw = getpwuid(getuid());
    if (pw) home = pw->pw_dir;
  }
  if (!home) return 0;

  char service_path[PATH_MAX];
  snprintf(service_path, sizeof(service_path),
           "%s/.config/systemd/user/c2t.service", home);
  make_parent_dirs(service_path);

  FILE *fp = fopen(service_path, "w");
  if (!fp) return 0;

  fprintf(fp,
          "[Unit]\n"
          "Description=c2t Remote Bridge Daemon\n"
          "After=network.target sound.target\n"
          "Documentation=https://github.com/root-hunter/c2t\n\n"
          "[Service]\n"
          "Type=simple\n"
          "ExecStart=%s start\n"
          "Restart=always\n"
          "RestartSec=5\n"
          "WorkingDirectory=%s\n\n"
          "[Install]\n"
          "WantedBy=default.target\n",
          exe_path, home);
  fclose(fp);

  /* Also create XDG autostart desktop entry for desktop sessions */
  char desktop_path[PATH_MAX];
  snprintf(desktop_path, sizeof(desktop_path),
           "%s/.config/autostart/c2t.desktop", home);
  make_parent_dirs(desktop_path);

  FILE *dfp = fopen(desktop_path, "w");
  if (dfp) {
    fprintf(dfp,
            "[Desktop Entry]\n"
            "Type=Application\n"
            "Name=c2t Service\n"
            "Comment=c2t Remote Bridge Daemon\n"
            "Exec=%s start\n"
            "Hidden=false\n"
            "NoDisplay=true\n"
            "X-GNOME-Autostart-enabled=true\n",
            exe_path);
    fclose(dfp);
  }

  /* Reload systemd and enable service */
  (void)system("systemctl --user daemon-reload >/dev/null 2>&1");
  int sys_res = system("systemctl --user enable --now c2t.service >/dev/null 2>&1");

  if (detail_msg && max_len > 0) {
    snprintf(detail_msg, max_len,
             "🟢 <b>Autostart Configured (Linux Systemd User &amp; XDG)</b>\n\n"
             "• <b>Binary:</b> <code>%s</code>\n"
             "• <b>Systemd Unit:</b> <code>%s</code> (%s)\n"
             "• <b>XDG Desktop Entry:</b> <code>%s</code>\n\n"
             "<i>Daemon is configured to start on boot and user login.</i>",
             exe_path, service_path,
             sys_res == 0 ? "🟢 Enabled &amp; Active" : "🟡 Written",
             desktop_path);
  }
  return 1;
}

static int uninstall_linux_systemd_user(char *detail_msg, size_t max_len) {
  const char *home = c2t_getenv("HOME");
  if (!home || !*home) {
    struct passwd *pw = getpwuid(getuid());
    if (pw) home = pw->pw_dir;
  }
  if (!home) return 0;

  (void)system("systemctl --user disable --now c2t.service >/dev/null 2>&1");

  char service_path[PATH_MAX];
  snprintf(service_path, sizeof(service_path),
           "%s/.config/systemd/user/c2t.service", home);
  unlink(service_path);

  char desktop_path[PATH_MAX];
  snprintf(desktop_path, sizeof(desktop_path),
           "%s/.config/autostart/c2t.desktop", home);
  unlink(desktop_path);

  (void)system("systemctl --user daemon-reload >/dev/null 2>&1");

  if (detail_msg && max_len > 0) {
    snprintf(detail_msg, max_len,
             "🗑️ <b>Autostart Removed (Linux)</b>\n\n"
             "• Disabled systemd user service <code>c2t.service</code>\n"
             "• Removed <code>%s</code>\n"
             "• Removed <code>%s</code>\n\n"
             "<i>c2t will no longer start automatically at login.</i>",
             service_path, desktop_path);
  }
  return 1;
}

#endif /* Linux */

#ifdef __APPLE__

static int install_macos_launchagent(const char *exe_path, char *detail_msg, size_t max_len) {
  const char *home = c2t_getenv("HOME");
  if (!home) return 0;

  char plist_path[PATH_MAX];
  snprintf(plist_path, sizeof(plist_path),
           "%s/Library/LaunchAgents/com.roothunter.c2t.plist", home);
  make_parent_dirs(plist_path);

  FILE *fp = fopen(plist_path, "w");
  if (!fp) return 0;

  fprintf(fp,
          "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
          "<plist version=\"1.0\">\n"
          "<dict>\n"
          "    <key>Label</key>\n"
          "    <string>com.roothunter.c2t</string>\n"
          "    <key>ProgramArguments</key>\n"
          "    <array>\n"
          "        <string>%s</string>\n"
          "        <string>start</string>\n"
          "    </array>\n"
          "    <key>RunAtLoad</key>\n"
          "    <true/>\n"
          "    <key>KeepAlive</key>\n"
          "    <true/>\n"
          "    <key>WorkingDirectory</key>\n"
          "    <string>%s</string>\n"
          "</dict>\n"
          "</plist>\n",
          exe_path, home);
  fclose(fp);

  char cmd[PATH_MAX + 64];
  snprintf(cmd, sizeof(cmd), "launchctl load -w \"%s\" >/dev/null 2>&1", plist_path);
  (void)system(cmd);

  if (detail_msg && max_len > 0) {
    snprintf(detail_msg, max_len,
             "🟢 <b>Autostart Configured (macOS LaunchAgent)</b>\n\n"
             "• <b>Binary:</b> <code>%s</code>\n"
             "• <b>Plist:</b> <code>%s</code>\n\n"
             "<i>c2t is registered with launchd and will start on login.</i>",
             exe_path, plist_path);
  }
  return 1;
}

static int uninstall_macos_launchagent(char *detail_msg, size_t max_len) {
  const char *home = c2t_getenv("HOME");
  if (!home) return 0;

  char plist_path[PATH_MAX];
  snprintf(plist_path, sizeof(plist_path),
           "%s/Library/LaunchAgents/com.roothunter.c2t.plist", home);

  char cmd[PATH_MAX + 64];
  snprintf(cmd, sizeof(cmd), "launchctl unload -w \"%s\" >/dev/null 2>&1", plist_path);
  (void)system(cmd);
  unlink(plist_path);

  if (detail_msg && max_len > 0) {
    snprintf(detail_msg, max_len,
             "🗑️ <b>Autostart Removed (macOS)</b>\n\n"
             "• Unloaded LaunchAgent\n"
             "• Removed <code>%s</code>\n\n"
             "<i>c2t will no longer start automatically at login.</i>",
             plist_path);
  }
  return 1;
}

#endif /* macOS */

#ifdef _WIN32

static int install_windows_registry(const char *exe_path, char *detail_msg, size_t max_len) {
  c2t_win32_api_init();
  if (!g_c2t_win32.RegOpenKeyExA || !g_c2t_win32.RegSetValueExA || !g_c2t_win32.RegCloseKey) {
    if (detail_msg && max_len > 0) {
      snprintf(detail_msg, max_len, "⚠️ <b>Error:</b> Win32 Registry APIs not available.");
    }
    return 0;
  }

  HKEY hKey;
  LONG res = g_c2t_win32.RegOpenKeyExA(HKEY_CURRENT_USER,
                                       "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                       0, KEY_SET_VALUE, &hKey);
  if (res != ERROR_SUCCESS) return 0;

  char val[MAX_PATH + 32];
  snprintf(val, sizeof(val), "\"%s\" start", exe_path);

  res = g_c2t_win32.RegSetValueExA(hKey, "c2t", 0, REG_SZ,
                                   (const BYTE *)val, (DWORD)(strlen(val) + 1));
  g_c2t_win32.RegCloseKey(hKey);

  if (res != ERROR_SUCCESS) return 0;

  if (detail_msg && max_len > 0) {
    snprintf(detail_msg, max_len,
             "🟢 <b>Autostart Configured (Windows Registry)</b>\n\n"
             "• <b>Binary:</b> <code>%s</code>\n"
             "• <b>Key:</b> <code>HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run</code>\n"
             "• <b>Value:</b> <code>%s</code>\n\n"
             "<i>c2t is registered to start automatically on Windows logon.</i>",
             exe_path, val);
  }
  return 1;
}

static int uninstall_windows_registry(char *detail_msg, size_t max_len) {
  c2t_win32_api_init();
  if (!g_c2t_win32.RegOpenKeyExA || !g_c2t_win32.RegDeleteValueA || !g_c2t_win32.RegCloseKey) {
    return 0;
  }

  HKEY hKey;
  LONG res = g_c2t_win32.RegOpenKeyExA(HKEY_CURRENT_USER,
                                       "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                       0, KEY_SET_VALUE, &hKey);
  if (res != ERROR_SUCCESS) return 0;

  res = g_c2t_win32.RegDeleteValueA(hKey, "c2t");
  g_c2t_win32.RegCloseKey(hKey);

  if (detail_msg && max_len > 0) {
    snprintf(detail_msg, max_len,
             "🗑️ <b>Autostart Removed (Windows)</b>\n\n"
             "• Removed <code>c2t</code> from <code>HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run</code>\n\n"
             "<i>c2t will no longer start automatically on Windows logon.</i>");
  }
  return 1;
}

#endif /* Windows */

int c2t_install_autostart(int system_wide, char *detail_msg, size_t max_len) {
  (void)system_wide;
  char exe_path[1024] = {};
  if (!get_self_executable_path(exe_path, sizeof(exe_path))) {
    if (detail_msg && max_len > 0) {
      snprintf(detail_msg, max_len, "⚠️ <b>Error:</b> Unable to determine c2t executable path.");
    }
    return 0;
  }

#if defined(__linux__) || (defined(__unix__) && !defined(__APPLE__))
  return install_linux_systemd_user(exe_path, detail_msg, max_len);
#elif defined(__APPLE__)
  return install_macos_launchagent(exe_path, detail_msg, max_len);
#elif defined(_WIN32)
  return install_windows_registry(exe_path, detail_msg, max_len);
#else
  if (detail_msg && max_len > 0) {
    snprintf(detail_msg, max_len, "⚠️ <b>Error:</b> Autostart installation not supported on this platform.");
  }
  return 0;
#endif
}

int c2t_uninstall_autostart(int system_wide, char *detail_msg, size_t max_len) {
  (void)system_wide;
#if defined(__linux__) || (defined(__unix__) && !defined(__APPLE__))
  return uninstall_linux_systemd_user(detail_msg, max_len);
#elif defined(__APPLE__)
  return uninstall_macos_launchagent(detail_msg, max_len);
#elif defined(_WIN32)
  return uninstall_windows_registry(detail_msg, max_len);
#else
  if (detail_msg && max_len > 0) {
    snprintf(detail_msg, max_len, "⚠️ <b>Error:</b> Autostart removal not supported on this platform.");
  }
  return 0;
#endif
}

int c2t_get_autostart_status(char *detail_msg, size_t max_len) {
  if (!detail_msg || max_len == 0) return 0;

#if defined(__linux__) || (defined(__unix__) && !defined(__APPLE__))
  const char *home = c2t_getenv("HOME");
  char service_path[PATH_MAX] = {};
  char desktop_path[PATH_MAX] = {};
  if (home) {
    snprintf(service_path, sizeof(service_path), "%s/.config/systemd/user/c2t.service", home);
    snprintf(desktop_path, sizeof(desktop_path), "%s/.config/autostart/c2t.desktop", home);
  }

  int has_service = (access(service_path, F_OK) == 0);
  int has_desktop = (access(desktop_path, F_OK) == 0);

  if (has_service || has_desktop) {
    snprintf(detail_msg, max_len,
             "🚀 <b>Autostart Status (Linux):</b> 🟢 <b>INSTALLED</b>\n\n"
             "• <b>Systemd Service:</b> %s (<code>%s</code>)\n"
             "• <b>XDG Desktop Entry:</b> %s (<code>%s</code>)\n\n"
             "💡 <i>To remove: <code>/uninstall</code> or <code>c2t uninstall</code></i>",
             has_service ? "🟢 Installed" : "⚪ Not found",
             service_path,
             has_desktop ? "🟢 Installed" : "⚪ Not found",
             desktop_path);
    return 1;
  } else {
    snprintf(detail_msg, max_len,
             "🚀 <b>Autostart Status (Linux):</b> ⚪ <b>NOT INSTALLED</b>\n\n"
             "💡 <i>To install as a startup service: <code>/install</code> or <code>c2t install</code></i>");
    return 0;
  }

#elif defined(__APPLE__)
  const char *home = c2t_getenv("HOME");
  char plist_path[PATH_MAX] = {};
  if (home) {
    snprintf(plist_path, sizeof(plist_path), "%s/Library/LaunchAgents/com.roothunter.c2t.plist", home);
  }
  int has_plist = (access(plist_path, F_OK) == 0);
  if (has_plist) {
    snprintf(detail_msg, max_len,
             "🚀 <b>Autostart Status (macOS):</b> 🟢 <b>INSTALLED</b>\n\n"
             "• <b>LaunchAgent Plist:</b> <code>%s</code>\n\n"
             "💡 <i>To remove: <code>/uninstall</code> or <code>c2t uninstall</code></i>",
             plist_path);
    return 1;
  } else {
    snprintf(detail_msg, max_len,
             "🚀 <b>Autostart Status (macOS):</b> ⚪ <b>NOT INSTALLED</b>\n\n"
             "💡 <i>To install as a startup LaunchAgent: <code>/install</code> or <code>c2t install</code></i>");
    return 0;
  }

#elif defined(_WIN32)
  c2t_win32_api_init();
  int installed = 0;
  char val[MAX_PATH + 32] = {};
  if (g_c2t_win32.RegOpenKeyExA && g_c2t_win32.RegQueryValueExA && g_c2t_win32.RegCloseKey) {
    HKEY hKey;
    LONG res = g_c2t_win32.RegOpenKeyExA(HKEY_CURRENT_USER,
                                         "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                         0, KEY_QUERY_VALUE, &hKey);
    DWORD val_len = sizeof(val);
    if (res == ERROR_SUCCESS) {
      if (g_c2t_win32.RegQueryValueExA(hKey, "c2t", NULL, NULL, (LPBYTE)val, &val_len) == ERROR_SUCCESS) {
        installed = 1;
      }
      g_c2t_win32.RegCloseKey(hKey);
    }
  }

  if (installed) {
    snprintf(detail_msg, max_len,
             "🚀 <b>Autostart Status (Windows):</b> 🟢 <b>INSTALLED</b>\n\n"
             "• <b>Registry Value:</b> <code>%s</code>\n\n"
             "💡 <i>To remove: <code>/uninstall</code> or <code>c2t uninstall</code></i>",
             val);
    return 1;
  } else {
    snprintf(detail_msg, max_len,
             "🚀 <b>Autostart Status (Windows):</b> ⚪ <b>NOT INSTALLED</b>\n\n"
             "💡 <i>To install as a startup registry entry: <code>/install</code> or <code>c2t install</code></i>");
    return 0;
  }
#else
  snprintf(detail_msg, max_len, "⚪ <b>Autostart Status:</b> Not supported on this OS.");
  return 0;
#endif
}
