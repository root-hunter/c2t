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

#if defined(__linux__) || defined(__unix__)

#include "screenshot.h"
#include "screenshot_png.h"
#include "../logging/logging.h"

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xcb/xcb.h>

static pthread_mutex_t display_lock = PTHREAD_MUTEX_INITIALIZER;
static char selected_display_target[64] = "all";
static int selected_display_index = -1;

typedef struct {
  int id;
  int x;
  int y;
  uint16_t width;
  uint16_t height;
  int is_primary;
  char name[64];
} linux_display_info_t;

#define MAX_LINUX_DISPLAYS 16
static linux_display_info_t cached_displays[MAX_LINUX_DISPLAYS];
static int cached_display_count = 0;

static int is_wayland_session(void) {
  const char *wayland_disp = getenv("WAYLAND_DISPLAY");
  const char *session_type = getenv("XDG_SESSION_TYPE");
  if (wayland_disp && *wayland_disp) return 1;
  if (session_type && strcmp(session_type, "wayland") == 0) return 1;
  return 0;
}

static xcb_screen_t *get_screen(xcb_connection_t *conn, int screen_num) {
  const xcb_setup_t *setup = xcb_get_setup(conn);
  xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
  for (int i = 0; i < screen_num && iter.rem > 0; ++i) {
    xcb_screen_next(&iter);
  }
  return iter.data;
}

static int refresh_displays_x11(void) {
  const char *display = getenv("DISPLAY");
  if (!display || !*display) return 0;

  int screen_num = 0;
  xcb_connection_t *conn = xcb_connect(nullptr, &screen_num);
  if (xcb_connection_has_error(conn)) {
    xcb_disconnect(conn);
    return 0;
  }

  const xcb_setup_t *setup = xcb_get_setup(conn);
  xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
  cached_display_count = 0;

  int idx = 0;
  while (iter.rem > 0 && cached_display_count < MAX_LINUX_DISPLAYS) {
    xcb_screen_t *s = iter.data;
    if (s && s->width_in_pixels > 0 && s->height_in_pixels > 0) {
      cached_displays[cached_display_count].id = idx;
      cached_displays[cached_display_count].x = 0;
      cached_displays[cached_display_count].y = 0;
      cached_displays[cached_display_count].width = s->width_in_pixels;
      cached_displays[cached_display_count].height = s->height_in_pixels;
      cached_displays[cached_display_count].is_primary = (idx == screen_num);
      snprintf(cached_displays[cached_display_count].name,
               sizeof(cached_displays[cached_display_count].name),
               "X11 Screen :%d", idx);
      cached_display_count++;
    }
    idx++;
    xcb_screen_next(&iter);
  }

  xcb_disconnect(conn);
  return cached_display_count;
}

/* Fast helper to read an image file from disk and return its allocated buffer */
static int read_image_file_to_buffer(const char *filepath, void **out_data, size_t *out_size) {
  if (!filepath || !*filepath) return 0;

  FILE *fp = fopen(filepath, "rb");
  if (!fp) return 0;

  fseek(fp, 0, SEEK_END);
  long fsize = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (fsize <= 0) {
    fclose(fp);
    return 0;
  }

  void *buf = malloc((size_t)fsize);
  if (!buf) {
    fclose(fp);
    return 0;
  }

  if (fread(buf, 1, (size_t)fsize, fp) != (size_t)fsize) {
    free(buf);
    fclose(fp);
    return 0;
  }

  fclose(fp);
  *out_data = buf;
  *out_size = (size_t)fsize;
  return 1;
}

/* Check if a PNG image buffer is purely black (empty / unrendered XWayland root) */
static int is_png_empty_or_black(const void *data, size_t size) {
  if (!data || size < 64) return 1;
  /* Very small PNGs (like 6KB for 1920x1080) usually indicate zero data or single-color blank */
  if (size < 10240) {
    /* If the entire file is tiny for full HD, check if pixels are trivial/black */
    return 1;
  }
  return 0;
}

static void ensure_desktop_session_env(void) {
  uid_t uid = getuid();
  char runtime_dir[128];
  snprintf(runtime_dir, sizeof(runtime_dir), "/run/user/%u", (unsigned int)uid);

  if (!getenv("XDG_RUNTIME_DIR") && access(runtime_dir, F_OK) == 0) {
    setenv("XDG_RUNTIME_DIR", runtime_dir, 0);
  }

  if (!getenv("DBUS_SESSION_BUS_ADDRESS")) {
    char bus_path[160];
    snprintf(bus_path, sizeof(bus_path), "unix:path=%s/bus", runtime_dir);
    if (access(runtime_dir, F_OK) == 0) {
      setenv("DBUS_SESSION_BUS_ADDRESS", bus_path, 0);
    }
  }

  if (!getenv("WAYLAND_DISPLAY")) {
    char wayland_sock[160];
    snprintf(wayland_sock, sizeof(wayland_sock), "%s/wayland-0", runtime_dir);
    if (access(wayland_sock, F_OK) == 0) {
      setenv("WAYLAND_DISPLAY", "wayland-0", 0);
    }
  }

  if (!getenv("DISPLAY")) {
    if (access("/tmp/.X11-unix/X0", F_OK) == 0) {
      setenv("DISPLAY", ":0", 0);
    } else if (access("/tmp/.X11-unix/X1", F_OK) == 0) {
      setenv("DISPLAY", ":1", 0);
    }
  }
}

/* Attempt XDG Desktop Portal Screenshot via Python/DBus script with unique handle token */
static int capture_via_xdg_portal(void **out_data, size_t *out_size) {
  ensure_desktop_session_env();

  static const char script[] =
      "import dbus, urllib.parse, sys, time\n"
      "from dbus.mainloop.glib import DBusGMainLoop\n"
      "from gi.repository import GLib\n"
      "DBusGMainLoop(set_as_default=True)\n"
      "try:\n"
      "    bus = dbus.SessionBus()\n"
      "    loop = GLib.MainLoop()\n"
      "    def on_r(res, d):\n"
      "        if res == 0 and 'uri' in d:\n"
      "            u = str(d['uri'])\n"
      "            p = urllib.parse.unquote(u[7:]) if u.startswith('file://') else u\n"
      "            print(p)\n"
      "            sys.stdout.flush()\n"
      "        loop.quit()\n"
      "    portal = bus.get_object('org.freedesktop.portal.Desktop', '/org/freedesktop/portal/desktop')\n"
      "    iface = dbus.Interface(portal, 'org.freedesktop.portal.Screenshot')\n"
      "    req = iface.Screenshot('', {'interactive': dbus.Boolean(False), 'handle_token': dbus.String(f'c2t_{int(time.time()*1000)}')})\n"
      "    bus.add_signal_receiver(on_r, signal_name='Response', dbus_interface='org.freedesktop.portal.Request', path=req)\n"
      "    GLib.timeout_add_seconds(4, loop.quit)\n"
      "    loop.run()\n"
      "except Exception:\n"
      "    sys.exit(1)\n";

  char tmp_script[] = "/tmp/c2t_portal_XXXXXX.py";
  int fd = mkstemps(tmp_script, 3);
  if (fd < 0) return 0;
  if (write(fd, script, sizeof(script) - 1) < 0) {
    close(fd);
    unlink(tmp_script);
    return 0;
  }
  close(fd);

  char cmd[512];
  snprintf(cmd, sizeof(cmd), "python3 %s 2>/dev/null", tmp_script);
  FILE *pipe = popen(cmd, "r");
  if (!pipe) {
    unlink(tmp_script);
    return 0;
  }

  char captured_path[1024] = {};
  if (fgets(captured_path, sizeof(captured_path), pipe) == nullptr) {
    pclose(pipe);
    unlink(tmp_script);
    return 0;
  }
  pclose(pipe);
  unlink(tmp_script);

  size_t len = strlen(captured_path);
  while (len > 0 && (captured_path[len - 1] == '\r' || captured_path[len - 1] == '\n')) {
    captured_path[--len] = '\0';
  }

  if (len == 0) return 0;

  if (read_image_file_to_buffer(captured_path, out_data, out_size)) {
    /* Delete the file generated by portal to avoid cluttering ~/Pictures */
    unlink(captured_path);
    c2t_log_info("screenshot", "Captured Linux Wayland desktop screenshot via XDG Portal (%zu bytes PNG)", *out_size);
    return 1;
  }
  return 0;
}

static int capture_via_portal_or_tool(void **out_data, size_t *out_size,
                                      const char **out_mime_type,
                                      const char **out_filename) {
  /* 1. Try XDG Portal first for modern Wayland / GNOME 41+ / KDE environments */
  if (capture_via_xdg_portal(out_data, out_size)) {
    *out_mime_type = "image/png";
    *out_filename = "screenshot.png";
    return 1;
  }

  /* 2. Try standard native command utilities */
  static const char *const capture_cmds[] = {
      "grim \"%s\" 2>/dev/null",
      "maim \"%s\" 2>/dev/null",
      "spectacle -b -n -o \"%s\" 2>/dev/null",
      "gnome-screenshot -f \"%s\" 2>/dev/null",
      "xfce4-screenshooter -f -s \"%s\" 2>/dev/null",
      "scrot \"%s\" 2>/dev/null",
      "import -window root \"%s\" 2>/dev/null",
      nullptr,
  };

  for (size_t i = 0; capture_cmds[i] != nullptr; ++i) {
    char tmp_path[] = "/tmp/c2t_shot_XXXXXX.png";
    int fd = mkstemps(tmp_path, 4);
    if (fd < 0) continue;
    close(fd);
    unlink(tmp_path);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), capture_cmds[i], tmp_path);
    int ret = system(cmd);
    (void)ret;

    void *buf = nullptr;
    size_t fsize = 0;
    if (!read_image_file_to_buffer(tmp_path, &buf, &fsize)) {
      unlink(tmp_path);
      continue;
    }
    unlink(tmp_path);

    /* If scrot or another tool returned a blank 6KB black image on Wayland, skip to next tool */
    if (is_wayland_session() && is_png_empty_or_black(buf, fsize) && i == 5) {
      free(buf);
      continue;
    }

    *out_data = buf;
    *out_size = fsize;
    *out_mime_type = "image/png";
    *out_filename = "screenshot.png";
    c2t_log_info("screenshot", "Captured Linux desktop screenshot via command index %zu (%zu bytes PNG)", i, fsize);
    return 1;
  }
  return 0;
}

int screenshot_capture_x11(void **out_data, size_t *out_size,
                           const char **out_mime_type,
                           const char **out_filename) {
  return screenshot_capture_display("all", out_data, out_size, out_mime_type, out_filename);
}

int screenshot_capture_linux_display(const char *target,
                                    void **out_data, size_t *out_size,
                                    const char **out_mime_type,
                                    const char **out_filename) {
  if (!out_data || !out_size || !out_mime_type || !out_filename) {
    return 0;
  }
  *out_data = nullptr;
  *out_size = 0;

  /* On Wayland sessions, prioritize Wayland tools and portals */
  if (is_wayland_session()) {
    if (capture_via_portal_or_tool(out_data, out_size, out_mime_type, out_filename)) {
      return 1;
    }
  }

  const char *display = getenv("DISPLAY");
  if (display && *display) {
    int target_idx = -1;
    if (target && *target && strcmp(target, "all") != 0 && strcmp(target, "*") != 0) {
      target_idx = atoi(target);
    }

    int screen_num = (target_idx >= 0) ? target_idx : 0;
    xcb_connection_t *conn = xcb_connect(nullptr, &screen_num);
    if (!xcb_connection_has_error(conn)) {
      xcb_screen_t *screen = get_screen(conn, screen_num);
      if (!screen) {
        screen = get_screen(conn, 0);
      }
      if (screen && screen->width_in_pixels > 0 && screen->height_in_pixels > 0) {
        uint16_t width = screen->width_in_pixels;
        uint16_t height = screen->height_in_pixels;

        xcb_get_image_cookie_t cookie = xcb_get_image(
            conn, XCB_IMAGE_FORMAT_Z_PIXMAP, screen->root, 0, 0, width, height, ~0);
        xcb_get_image_reply_t *reply = xcb_get_image_reply(conn, cookie, nullptr);
        if (reply) {
          int reply_len = xcb_get_image_data_length(reply);
          uint8_t *pixel_data = xcb_get_image_data(reply);
          if (pixel_data && reply_len > 0) {
            void *png_buf = nullptr;
            size_t png_size = 0;
            if (screenshot_encode_png_rgba((uint32_t)width, (uint32_t)height, pixel_data, 1, &png_buf, &png_size)) {
              free(reply);
              xcb_disconnect(conn);
              *out_data = png_buf;
              *out_size = png_size;
              *out_mime_type = "image/png";
              *out_filename = "screenshot.png";
              c2t_log_info("screenshot", "Captured %ux%u Linux X11 desktop screenshot (%zu bytes PNG)", width, height, png_size);
              return 1;
            }
          }
          free(reply);
        }
      }
      xcb_disconnect(conn);
    }
  }

  /* Fallback to portal/tool capture if XCB failed */
  if (capture_via_portal_or_tool(out_data, out_size, out_mime_type, out_filename)) {
    return 1;
  }

  c2t_log_warning("screenshot", "All Linux capture methods (Wayland tools/portals and X11/XCB) failed");
  return 0;
}

int screenshot_get_display_list(char *buffer, size_t max_len) {
  if (!buffer || max_len == 0) return 0;

  pthread_mutex_lock(&display_lock);
  refresh_displays_x11();

  const char *backend = is_wayland_session() ? "Wayland" : "X11";

  if (cached_display_count == 0) {
    snprintf(buffer, max_len,
             "🖥️ <b>Connected Displays (%s):</b>\n\n"
             "• <b>[all]</b> <i>Virtual Desktop / Primary Display</i> — 🟢 <b>ACTIVE</b>\n\n"
             "🎯 <b>Current Target:</b> <code>%s</code>\n"
             "💡 <i>Select display with <code>/screenshot_select &lt;id|all&gt;</code></i>",
             backend, selected_display_target);
    pthread_mutex_unlock(&display_lock);
    return 1;
  }

  size_t offset = (size_t)snprintf(
      buffer, max_len, "🖥️ <b>Detected Displays — %s (%d):</b>\n\n",
      backend, cached_display_count);

  for (int i = 0; i < cached_display_count && offset + 128 < max_len; ++i) {
    int active = (selected_display_index == i) || (selected_display_index == -1);
    offset += (size_t)snprintf(
        buffer + offset, max_len - offset,
        "• <b>[%d]</b> <code>%s</code> (%ux%u)%s\n"
        "  Status: %s\n",
        cached_displays[i].id,
        cached_displays[i].name,
        cached_displays[i].width,
        cached_displays[i].height,
        cached_displays[i].is_primary ? " 🌟 <i>Primary</i>" : "",
        active ? "🟢 <b>ACTIVE</b>" : "⚪ <i>IDLE</i>");
  }

  if (offset + 128 < max_len) {
    snprintf(buffer + offset, max_len - offset,
             "\n🎯 <b>Current Target:</b> <code>%s</code>\n"
             "💡 <i>Select display with <code>/screenshot_select &lt;id|all&gt;</code></i>",
             selected_display_target);
  }

  pthread_mutex_unlock(&display_lock);
  return 1;
}

int screenshot_select_display(const char *target) {
  pthread_mutex_lock(&display_lock);
  if (!target || !*target || strcmp(target, "all") == 0 || strcmp(target, "*") == 0) {
    selected_display_index = -1;
    snprintf(selected_display_target, sizeof(selected_display_target), "all");
  } else {
    int is_num = 1;
    for (const char *p = target; *p; ++p) {
      if (!isdigit((unsigned char)*p)) {
        is_num = 0;
        break;
      }
    }
    if (is_num) {
      selected_display_index = atoi(target);
      snprintf(selected_display_target, sizeof(selected_display_target), "%d", selected_display_index);
    } else {
      selected_display_index = -1;
      snprintf(selected_display_target, sizeof(selected_display_target), "%s", target);
    }
  }
  pthread_mutex_unlock(&display_lock);
  c2t_log_info("screenshot", "Selected display target '%s'", selected_display_target);
  return 1;
}

void screenshot_get_selected_display(char *buffer, size_t max_len) {
  if (!buffer || max_len == 0) return;
  pthread_mutex_lock(&display_lock);
  snprintf(buffer, max_len, "%s", selected_display_target);
  pthread_mutex_unlock(&display_lock);
}

int screenshot_get_display_count(void) {
  pthread_mutex_lock(&display_lock);
  int c = cached_display_count > 0 ? cached_display_count : 1;
  pthread_mutex_unlock(&display_lock);
  return c;
}

#endif
