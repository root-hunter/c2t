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
#include <stdlib.h>

#if defined(__APPLE__)
extern int screenshot_capture_macos(void **out_data, size_t *out_size,
                                    const char **out_mime_type,
                                    const char **out_filename);
#elif defined(_WIN32)
extern int screenshot_capture_windows(void **out_data, size_t *out_size,
                                      const char **out_mime_type,
                                      const char **out_filename);
#elif defined(__linux__) || defined(__unix__)
extern int screenshot_capture_x11(void **out_data, size_t *out_size,
                                  const char **out_mime_type,
                                  const char **out_filename);
#endif

int screenshot_capture(void **out_data, size_t *out_size,
                       const char **out_mime_type,
                       const char **out_filename) {
#if defined(__APPLE__)
  return screenshot_capture_macos(out_data, out_size, out_mime_type, out_filename);
#elif defined(_WIN32)
  return screenshot_capture_windows(out_data, out_size, out_mime_type, out_filename);
#elif defined(__linux__) || defined(__unix__)
  return screenshot_capture_x11(out_data, out_size, out_mime_type, out_filename);
#else
  if (out_data) *out_data = nullptr;
  if (out_size) *out_size = 0;
  if (out_mime_type) *out_mime_type = "application/octet-stream";
  if (out_filename) *out_filename = "screenshot.bin";
  return 0;
#endif
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
  const char *disp = getenv("DISPLAY");
  return (disp != nullptr && *disp != '\0');
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
  return "Linux (X11 XCB)";
#else
  return "Unsupported";
#endif
}
