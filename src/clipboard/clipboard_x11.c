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

#include "../config/config.h"
#include "../logging/logging.h"
#include "../runtime/runtime.h"
#include "clipboard.h"
#include "clipboard_output.h"
#include "../runtime/environment.h"

#include <xcb/xcb.h>
#include <xcb/xcbext.h>

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <time.h>

#define XFIXES_QUERY_VERSION 0
#define XFIXES_SELECT_SELECTION_INPUT 2
#define XFIXES_SELECTION_NOTIFY 0
#define XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER 1

typedef struct {
  uint8_t major_opcode;
  uint8_t minor_opcode;
  uint16_t length;
  uint32_t client_major_version;
  uint32_t client_minor_version;
} xfixes_query_version_request_t;

typedef struct {
  uint8_t major_opcode;
  uint8_t minor_opcode;
  uint16_t length;
  xcb_window_t window;
  xcb_atom_t selection;
  uint32_t event_mask;
} xfixes_select_selection_input_request_t;

typedef struct {
  uint8_t response_type;
  uint8_t subtype;
  uint16_t sequence;
  xcb_window_t window;
  xcb_window_t owner;
  xcb_atom_t selection;
  xcb_timestamp_t timestamp;
  xcb_timestamp_t selection_timestamp;
  uint8_t padding[8];
} xfixes_selection_notify_event_t;

static xcb_extension_t xfixes_extension = {"XFIXES", 0};

typedef struct {
  char *data;
  size_t length;
  size_t capacity;
} clipboard_transfer_t;

typedef struct {
  xcb_atom_t atom;
  const char *mime_type;
} clipboard_target_t;

static size_t copy_property_text(const char *value, size_t length, char *output,
                                 size_t capacity) {
  if (!capacity || !value || !length) {
    if (capacity)
      output[0] = '\0';
    return 0;
  }
  size_t copied = length < capacity - 1 ? length : capacity - 1;
  if (copied < length) {
    while (copied > 0 && ((unsigned char)value[copied] & 0xc0) == 0x80)
      --copied;
  }
  memcpy(output, value, copied);
  output[copied] = '\0';
  return copied;
}

[[nodiscard]] static xcb_get_property_reply_t *
get_property(xcb_connection_t *connection, xcb_window_t window,
             xcb_atom_t property, xcb_atom_t type, uint32_t offset,
             uint32_t length, int delete_property) {
  xcb_get_property_cookie_t cookie =
      xcb_get_property(connection, (uint8_t)delete_property, window, property,
                       type, offset, length);
  return xcb_get_property_reply(connection, cookie, nullptr);
}

static int capture_window(xcb_connection_t *connection,
                          xcb_window_t source_window, xcb_atom_t name_atom,
                          xcb_atom_t wm_name_atom, xcb_atom_t class_atom,
                          xcb_atom_t pid_atom, c2t_clipboard_source_t *source) {
  xcb_get_property_reply_t *name =
      get_property(connection, source_window, name_atom,
                   XCB_GET_PROPERTY_TYPE_ANY, 0, 2048, 0);
  if (!name || name->format != 8 || xcb_get_property_value_length(name) == 0) {
    free(name);
    name = get_property(connection, source_window, wm_name_atom,
                        XCB_GET_PROPERTY_TYPE_ANY, 0, 2048, 0);
  }
  if (name && name->format == 8) {
    copy_property_text(xcb_get_property_value(name),
                       (size_t)xcb_get_property_value_length(name),
                       source->title, sizeof(source->title));
  }
  free(name);

  xcb_get_property_reply_t *window_class =
      get_property(connection, source_window, class_atom,
                   XCB_GET_PROPERTY_TYPE_ANY, 0, 2048, 0);
  if (window_class && window_class->format == 8) {
    const char *value = xcb_get_property_value(window_class);
    size_t length = (size_t)xcb_get_property_value_length(window_class);
    if (value && length > 0) {
      size_t first_length = 0;
      while (first_length < length && value[first_length])
        ++first_length;
      const char *selected = value;
      size_t selected_length = first_length;
      if (first_length < length && first_length + 1 < length) {
        selected = value + first_length + 1;
        selected_length = length - first_length - 1;
        while (selected_length > 0 && selected[selected_length - 1] == '\0')
          --selected_length;
      }
      copy_property_text(selected, selected_length, source->application,
                         sizeof(source->application));
    }
  }
  free(window_class);

  xcb_get_property_reply_t *pid = get_property(
      connection, source_window, pid_atom, XCB_ATOM_CARDINAL, 0, 4, 0);
  if (pid && pid->format == 32 &&
      xcb_get_property_value_length(pid) >= (int)sizeof(uint32_t)) {
    const void *pid_val = xcb_get_property_value(pid);
    if (pid_val)
      memcpy(&source->process_id, pid_val, sizeof(source->process_id));
  }
  free(pid);
  return source->application[0] || source->title[0] || source->process_id;
}

static int capture_source(xcb_connection_t *connection, xcb_window_t root,
                          xcb_window_t selection_owner,
                          xcb_atom_t active_window_atom, xcb_atom_t name_atom,
                          xcb_atom_t wm_name_atom, xcb_atom_t class_atom,
                          xcb_atom_t pid_atom, c2t_clipboard_source_t *source) {
  memset(source, 0, sizeof(*source));
  if (!c2t_config_get()->telegram_send_window_info)
    return 0;

  xcb_window_t active_window = XCB_WINDOW_NONE;
  xcb_get_property_reply_t *active = get_property(
      connection, root, active_window_atom, XCB_ATOM_WINDOW, 0, 4, 0);
  if (active && active->format == 32 &&
      xcb_get_property_value_length(active) >= (int)sizeof(xcb_window_t)) {
    xcb_window_t candidate;
    memcpy(&candidate, xcb_get_property_value(active), sizeof(candidate));
    if (candidate != XCB_WINDOW_NONE)
      active_window = candidate;
  }
  free(active);

  int captured = active_window != XCB_WINDOW_NONE &&
                 capture_window(connection, active_window, name_atom,
                                wm_name_atom, class_atom, pid_atom, source);
  if (!captured && selection_owner != XCB_WINDOW_NONE &&
      selection_owner != active_window) {
    memset(source, 0, sizeof(*source));
    captured = capture_window(connection, selection_owner, name_atom,
                              wm_name_atom, class_atom, pid_atom, source);
  }
  if (captured)
    c2t_log_debug("x11", "Captured source window: app=%s, title=%s, pid=%lu",
                  source->application[0] ? source->application : "unknown",
                  source->title[0] ? source->title : "unknown",
                  (unsigned long)source->process_id);
  else
    c2t_log_debug("x11", "No source window metadata exposed by X11");
  return captured;
}

[[nodiscard]] static xcb_atom_t intern_atom(xcb_connection_t *connection,
                                            const char *name) {
  xcb_intern_atom_cookie_t cookie =
      xcb_intern_atom(connection, 0, (uint16_t)strlen(name), name);
  xcb_intern_atom_reply_t *reply =
      xcb_intern_atom_reply(connection, cookie, nullptr);
  if (!reply)
    return XCB_ATOM_NONE;

  xcb_atom_t atom = reply->atom;
  free(reply);
  return atom;
}

[[nodiscard]] static int xfixes_listen(xcb_connection_t *connection,
                                       xcb_window_t window,
                                       xcb_atom_t selection,
                                       uint8_t *event_type) {
  const xcb_query_extension_reply_t *extension =
      xcb_get_extension_data(connection, &xfixes_extension);
  if (!extension || !extension->present)
    return 0;

  static const xcb_protocol_request_t version_protocol = {
      2, &xfixes_extension, XFIXES_QUERY_VERSION, 0};
  xfixes_query_version_request_t version_request = {.client_major_version = 5,
                                                    .client_minor_version = 0};
  struct iovec version_parts[4];
  version_parts[2].iov_base = &version_request;
  version_parts[2].iov_len = sizeof(version_request);
  version_parts[3].iov_base = nullptr;
  version_parts[3].iov_len = 0;

  unsigned int sequence = xcb_send_request(
      connection, XCB_REQUEST_CHECKED, version_parts + 2, &version_protocol);
  xcb_generic_error_t *error = nullptr;
  void *reply = xcb_wait_for_reply(connection, sequence, &error);
  if (!reply || error) {
    free(reply);
    free(error);
    return 0;
  }
  free(reply);

  static const xcb_protocol_request_t select_protocol = {
      2, &xfixes_extension, XFIXES_SELECT_SELECTION_INPUT, 1};
  xfixes_select_selection_input_request_t select_request = {
      .window = window,
      .selection = selection,
      .event_mask = XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER};
  struct iovec select_parts[4];
  select_parts[2].iov_base = &select_request;
  select_parts[2].iov_len = sizeof(select_request);
  select_parts[3].iov_base = nullptr;
  select_parts[3].iov_len = 0;

  sequence = xcb_send_request(connection, XCB_REQUEST_CHECKED, select_parts + 2,
                              &select_protocol);
  error =
      xcb_request_check(connection, (xcb_void_cookie_t){.sequence = sequence});
  if (error) {
    free(error);
    return 0;
  }

  *event_type = (uint8_t)(extension->first_event + XFIXES_SELECTION_NOTIFY);
  return 1;
}

static int append_transfer(clipboard_transfer_t *transfer, const void *data,
                           size_t length) {
  if (length > SIZE_MAX - transfer->length)
    return 0;

  size_t required = transfer->length + length;
  if (required > c2t_config_get()->queue_max_bytes)
    return 0;
  if (required > transfer->capacity) {
    size_t capacity = transfer->capacity ? transfer->capacity : 4096;
    while (capacity < required) {
      if (capacity > SIZE_MAX / 2) {
        capacity = required;
        break;
      }
      capacity *= 2;
    }

    char *resized = realloc(transfer->data, capacity);
    if (!resized)
      return 0;
    transfer->data = resized;
    transfer->capacity = capacity;
  }

  memcpy(transfer->data + transfer->length, data, length);
  transfer->length = required;
  return 1;
}

static int read_property(xcb_connection_t *connection, xcb_window_t window,
                         xcb_atom_t property, xcb_atom_t incr, int incremental,
                         clipboard_transfer_t *transfer,
                         xcb_atom_t expected_type, const char *mime_type,
                         const c2t_clipboard_source_t *source) {
  size_t limit = c2t_config_get()->queue_max_bytes;
  uint32_t long_length = limit / 4 + (limit % 4 != 0);
  if (limit / 4 > UINT32_MAX)
    long_length = UINT32_MAX;
  xcb_get_property_reply_t *reply =
      get_property(connection, window, property, XCB_GET_PROPERTY_TYPE_ANY, 0,
                   long_length, 1);
  if (!reply)
    return 0;

  if (reply->type == incr) {
    c2t_log_debug("x11", "Starting incremental clipboard transfer");
    transfer->length = 0;
    free(reply);
    return 1;
  }

  if (reply->bytes_after != 0) {
    c2t_log_error("x11", "Clipboard content exceeds the queue limit");
    transfer->length = 0;
    free(reply);
    return 0;
  }

  if (reply->type != expected_type) {
    c2t_log_warning("x11", "Clipboard returned an unexpected data type");
    transfer->length = 0;
    free(reply);
    return 0;
  }

  int length = xcb_get_property_value_length(reply);

  if (incremental) {
    if (length > 0 && !append_transfer(transfer, xcb_get_property_value(reply),
                                       (size_t)length)) {
      c2t_log_error("x11", "Clipboard content is too large");
      transfer->length = 0;
      incremental = 0;
    } else if (length == 0) {
      c2t_log_debug("x11", "Incremental transfer complete (%llu bytes)",
                    (unsigned long long)transfer->length);
      clipboard_output(transfer->data, transfer->length, mime_type, source);
      transfer->length = 0;
      incremental = 0;
    }
  } else {
    clipboard_output(xcb_get_property_value(reply), (size_t)length, mime_type,
                     source);
  }

  free(reply);
  return incremental;
}

static int atom_is_available(const xcb_atom_t *atoms, size_t count,
                             xcb_atom_t candidate) {
  for (size_t index = 0; index < count; ++index) {
    if (atoms[index] == candidate)
      return 1;
  }
  return 0;
}

static xcb_atom_t select_target(xcb_connection_t *connection,
                                xcb_window_t window, xcb_atom_t property,
                                const clipboard_target_t *supported,
                                size_t supported_count,
                                const char **mime_type) {
  xcb_get_property_reply_t *reply = get_property(
      connection, window, property, XCB_ATOM_ATOM, 0, UINT32_MAX, 0);
  if (!reply || reply->format != 32) {
    free(reply);
    return XCB_ATOM_NONE;
  }

  const xcb_atom_t *atoms = xcb_get_property_value(reply);
  size_t count =
      atoms ? (size_t)xcb_get_property_value_length(reply) / sizeof(xcb_atom_t)
            : 0;
  xcb_atom_t selected = XCB_ATOM_NONE;
  if (atoms && count > 0) {
    for (size_t index = 0; index < supported_count; ++index) {
      if (atom_is_available(atoms, count, supported[index].atom)) {
        selected = supported[index].atom;
        *mime_type = supported[index].mime_type;
        break;
      }
    }
  }
  free(reply);
  return selected;
}

static int clipboard_listen_once(void) {
  const char *session_type = c2t_getenv("XDG_SESSION_TYPE");
  if (c2t_config_get()->telegram_send_window_info &&
      ((session_type && strcmp(session_type, "wayland") == 0) ||
       c2t_getenv("WAYLAND_DISPLAY"))) {
    c2t_log_warning("x11",
                    "Wayland session detected: source window information is "
                    "available only for windows exposed through X11/XWayland");
  }
  c2t_log_debug("x11", "Connecting to the X11 display");
  int screen_number = 0;
  xcb_connection_t *connection = xcb_connect(nullptr, &screen_number);
  if (xcb_connection_has_error(connection)) {
    c2t_log_error("x11", "Unable to open the X11 display");
    xcb_disconnect(connection);
    return 2;
  }

  const xcb_setup_t *setup = xcb_get_setup(connection);
  xcb_screen_iterator_t screens = xcb_setup_roots_iterator(setup);
  while (screen_number-- > 0 && screens.rem > 0)
    xcb_screen_next(&screens);
  if (!screens.data) {
    c2t_log_error("x11", "The X11 display has no usable screen");
    xcb_disconnect(connection);
    return 1;
  }

  xcb_window_t window = xcb_generate_id(connection);
  uint32_t mask = XCB_CW_EVENT_MASK;
  uint32_t values[] = {XCB_EVENT_MASK_PROPERTY_CHANGE};
  xcb_create_window(
      connection, XCB_COPY_FROM_PARENT, window, screens.data->root, 0, 0, 1, 1,
      0, XCB_WINDOW_CLASS_INPUT_ONLY, screens.data->root_visual, mask, values);

  xcb_atom_t clipboard = intern_atom(connection, "CLIPBOARD");
  xcb_atom_t targets = intern_atom(connection, "TARGETS");
  xcb_atom_t incr = intern_atom(connection, "INCR");
  xcb_atom_t property = intern_atom(connection, "C2T_SELECTION");
  xcb_atom_t active_window_atom = intern_atom(connection, "_NET_ACTIVE_WINDOW");
  xcb_atom_t name_atom = intern_atom(connection, "_NET_WM_NAME");
  xcb_atom_t wm_name_atom = intern_atom(connection, "WM_NAME");
  xcb_atom_t class_atom = intern_atom(connection, "WM_CLASS");
  xcb_atom_t pid_atom = intern_atom(connection, "_NET_WM_PID");

  if (clipboard == XCB_ATOM_NONE || targets == XCB_ATOM_NONE ||
      incr == XCB_ATOM_NONE || property == XCB_ATOM_NONE) {
    c2t_log_error("x11", "Unable to intern standard X11 selection atoms");
    xcb_disconnect(connection);
    return 1;
  }

  clipboard_target_t supported[] = {
      {intern_atom(connection, "UTF8_STRING"), "text/plain;charset=utf-8"},
      {intern_atom(connection, "text/plain;charset=utf-8"),
       "text/plain;charset=utf-8"},
      {intern_atom(connection, "text/plain"), "text/plain"},
      {intern_atom(connection, "STRING"), "text/plain"},
      {intern_atom(connection, "TEXT"), "text/plain"},
      {intern_atom(connection, "image/png"), "image/png"},
      {intern_atom(connection, "image/jpeg"), "image/jpeg"},
      {intern_atom(connection, "image/bmp"), "image/bmp"},
      {intern_atom(connection, "image/webp"), "image/webp"}};

  uint8_t xfixes_event_type;
  if (!xfixes_listen(connection, window, clipboard, &xfixes_event_type)) {
    c2t_log_error("x11", "The XFixes extension is unavailable");
    xcb_disconnect(connection);
    return 1;
  }
  xcb_flush(connection);
  c2t_log_info("x11", "Listening for clipboard ownership changes");

  int incremental = 0;
  xcb_atom_t requested_target = XCB_ATOM_NONE;
  const char *mime_type = nullptr;
  clipboard_transfer_t transfer = {};
  c2t_clipboard_source_t source = {};
  int has_source = 0;
  xcb_generic_event_t *event;

  while (!c2t_runtime_stop_requested()) {
    c2t_runtime_heartbeat();
    event = xcb_poll_for_event(connection);
    if (!event) {
      if (xcb_connection_has_error(connection))
        break;
      struct pollfd fds[2];
      int nfds = 1;
      fds[0].fd = xcb_get_file_descriptor(connection);
      fds[0].events = POLLIN;
      fds[0].revents = 0;

      int stop_fd = c2t_runtime_stop_descriptor();
      if (stop_fd >= 0) {
        fds[1].fd = stop_fd;
        fds[1].events = POLLIN;
        fds[1].revents = 0;
        nfds = 2;
      }

      int polled = poll(fds, (nfds_t)nfds, 1000);
      if (polled < 0 && errno != EINTR)
        break;
      continue;
    }
    uint8_t event_type = event->response_type & 0x7f;

    if (event_type == xfixes_event_type) {
      xfixes_selection_notify_event_t *selection = (void *)event;
      if (selection->owner != XCB_WINDOW_NONE) {
        c2t_log_debug("x11", "Clipboard owner changed; requesting targets");
        incremental = 0;
        transfer.length = 0;
        requested_target = targets;
        mime_type = nullptr;
        has_source =
            capture_source(connection, screens.data->root, selection->owner,
                           active_window_atom, name_atom, wm_name_atom,
                           class_atom, pid_atom, &source);

        xcb_convert_selection(connection, window, clipboard, targets, property,
                              selection->timestamp);
        xcb_flush(connection);
      }
    } else if (event_type == XCB_SELECTION_NOTIFY) {
      xcb_selection_notify_event_t *selection = (void *)event;
      if (selection->property != XCB_ATOM_NONE && requested_target == targets) {
        requested_target =
            select_target(connection, window, property, supported,
                          sizeof(supported) / sizeof(supported[0]), &mime_type);
        if (requested_target != XCB_ATOM_NONE) {
          c2t_log_debug("x11", "Selected clipboard target %s", mime_type);
          xcb_convert_selection(connection, window, clipboard, requested_target,
                                property, selection->time);
          xcb_flush(connection);
        } else {
          c2t_log_warning("x11", "Clipboard has no supported MIME type");
        }
      } else if (selection->property != XCB_ATOM_NONE && mime_type) {
        incremental = read_property(connection, window, property, incr, 0,
                                    &transfer, requested_target, mime_type,
                                    has_source ? &source : nullptr);
      }
    } else if (event_type == XCB_PROPERTY_NOTIFY && incremental) {
      xcb_property_notify_event_t *property_event = (void *)event;
      if (property_event->atom == property &&
          property_event->state == XCB_PROPERTY_NEW_VALUE) {
        incremental = read_property(connection, window, property, incr, 1,
                                    &transfer, requested_target, mime_type,
                                    has_source ? &source : nullptr);
      }
    }

    free(event);
  }

  int stopped = c2t_runtime_stop_requested();
  if (!stopped)
    c2t_log_error("x11", "The X11 connection was interrupted");
  free(transfer.data);
  xcb_disconnect(connection);
  return stopped ? 0 : 2;
}

int clipboard_listen(void) {
  unsigned int retry = 0;
  while (!c2t_runtime_stop_requested()) {
    int result = clipboard_listen_once();
    if (result != 2)
      return result;

    unsigned int delay = retry < 5 ? 1U << retry : 30U;
    if (retry < 5)
      ++retry;
    c2t_log_warning("x11", "Reconnecting to X11 in %u seconds", delay);
    for (unsigned int elapsed = 0;
         elapsed < delay && !c2t_runtime_stop_requested(); ++elapsed) {
      c2t_runtime_heartbeat();
      struct timespec duration = {.tv_sec = 1, .tv_nsec = 0};
      while (nanosleep(&duration, &duration) != 0 && errno == EINTR) {
      }
    }
  }
  return 0;
}
