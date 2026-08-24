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

#include <xcb/xcb.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
  int32_t  biWidth;
  int32_t  biHeight;
  uint16_t biPlanes;
  uint16_t biBitCount;
  uint32_t biCompression;
  uint32_t biSizeImage;
  int32_t  biXPelsPerMeter;
  int32_t  biYPelsPerMeter;
  uint32_t biClrUsed;
  uint32_t biClrImportant;
} bmp_info_header_t;
#pragma pack(pop)

static xcb_screen_t *get_screen(xcb_connection_t *conn, int screen_num) {
  const xcb_setup_t *setup = xcb_get_setup(conn);
  xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
  for (int i = 0; i < screen_num && iter.rem > 0; ++i) {
    xcb_screen_next(&iter);
  }
  return iter.data;
}

int screenshot_capture_x11(void **out_data, size_t *out_size,
                           const char **out_mime_type,
                           const char **out_filename) {
  if (!out_data || !out_size || !out_mime_type || !out_filename) {
    return 0;
  }
  *out_data = nullptr;
  *out_size = 0;

  const char *display = getenv("DISPLAY");
  if (!display || !*display) {
    c2t_log_warning("screenshot", "DISPLAY environment variable is not set");
    return 0;
  }

  int screen_num = 0;
  xcb_connection_t *conn = xcb_connect(nullptr, &screen_num);
  if (xcb_connection_has_error(conn)) {
    c2t_log_warning("screenshot", "Failed to connect to X11 display (DISPLAY=%s)", display);
    xcb_disconnect(conn);
    return 0;
  }

  xcb_screen_t *screen = get_screen(conn, screen_num);
  if (!screen) {
    c2t_log_warning("screenshot", "Failed to retrieve primary X11 screen");
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

  /* BMP row stride for 32-bit BGRA */
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

  /* Copy pixel data (in ZPixmap 24/32 depth, already 32-bit BGRA format) */
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
  c2t_log_info("screenshot", "Captured %ux%u X11 desktop screenshot (%zu bytes BMP)", width, height, total_size);
  return 1;
}

#endif
