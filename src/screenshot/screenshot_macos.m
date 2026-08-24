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
#include "screenshot.h"
#include "../logging/logging.h"

#include <stdlib.h>
#include <string.h>

int screenshot_capture_macos(void **out_data, size_t *out_size,
                             const char **out_mime_type,
                             const char **out_filename) {
  if (!out_data || !out_size || !out_mime_type || !out_filename) {
    return 0;
  }
  *out_data = NULL;
  *out_size = 0;

  /* CGWindowListCreateImage is compatible with modern macOS (macOS 10.5 through macOS 15+) */
  CGImageRef image = CGWindowListCreateImage(
      CGRectInfinite,
      kCGWindowListOptionOnScreenOnly,
      kCGNullWindowID,
      kCGWindowImageDefault);

  if (!image) {
    c2t_log_warning("screenshot", "CGWindowListCreateImage failed; check Screen Recording permissions in System Settings");
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

#endif
