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

#include "screenshot.h"
#include "../runtime/environment.h"
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
extern int screenshot_capture_macos_display(const char *target,
                                            void **out_data, size_t *out_size,
                                            const char **out_mime_type,
                                            const char **out_filename);
#elif defined(_WIN32)
extern int screenshot_capture_windows_display(const char *target,
                                              void **out_data, size_t *out_size,
                                              const char **out_mime_type,
                                              const char **out_filename);
#elif defined(__linux__) || defined(__unix__)
extern int screenshot_capture_linux_display(const char *target,
                                            void **out_data, size_t *out_size,
                                            const char **out_mime_type,
                                            const char **out_filename);
#endif

int screenshot_capture_display(const char *display_target,
                               void **out_data, size_t *out_size,
                               const char **out_mime_type,
                               const char **out_filename) {
#if defined(__APPLE__)
  return screenshot_capture_macos_display(display_target, out_data, out_size, out_mime_type, out_filename);
#elif defined(_WIN32)
  return screenshot_capture_windows_display(display_target, out_data, out_size, out_mime_type, out_filename);
#elif defined(__linux__) || defined(__unix__)
  return screenshot_capture_linux_display(display_target, out_data, out_size, out_mime_type, out_filename);
#else
  (void)display_target;
  if (out_data) *out_data = nullptr;
  if (out_size) *out_size = 0;
  if (out_mime_type) *out_mime_type = "application/octet-stream";
  if (out_filename) *out_filename = "screenshot.bin";
  return 0;
#endif
}

int screenshot_capture(void **out_data, size_t *out_size,
                       const char **out_mime_type,
                       const char **out_filename) {
  char cur_target[64] = "all";
  screenshot_get_selected_display(cur_target, sizeof(cur_target));
  return screenshot_capture_display(cur_target, out_data, out_size, out_mime_type, out_filename);
}

void screenshot_free_data(void *data) {
  if (data) {
    free(data);
  }
}

int screenshot_is_available(void) {
#if defined(__APPLE__)
  return 1;
#elif defined(_WIN32)
  return 1;
#elif defined(__linux__) || defined(__unix__)
  const char *disp = c2t_getenv("DISPLAY");
  const char *wayland = c2t_getenv("WAYLAND_DISPLAY");
  return (disp != nullptr && *disp != '\0') || (wayland != nullptr && *wayland != '\0');
#else
  return 0;
#endif
}

const char *screenshot_get_backend_name(void) {
#if defined(__APPLE__)
  return "macOS (CoreGraphics)";
#elif defined(_WIN32)
  return "Windows (GDI)";
#elif defined(__linux__) || defined(__unix__)
  const char *wayland = c2t_getenv("WAYLAND_DISPLAY");
  if (wayland && *wayland) return "Linux (Wayland)";
  return "Linux (X11 XCB)";
#else
  return "Unsupported";
#endif
}
