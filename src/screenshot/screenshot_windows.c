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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int screenshot_capture_windows(void **out_data, size_t *out_size,
                               const char **out_mime_type,
                               const char **out_filename) {
  if (!out_data || !out_size || !out_mime_type || !out_filename) {
    return 0;
  }
  *out_data = NULL;
  *out_size = 0;

  int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
  int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
  int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

  if (width <= 0 || height <= 0) {
    /* Fallback to primary monitor */
    width = GetSystemMetrics(SM_CXSCREEN);
    height = GetSystemMetrics(SM_CYSCREEN);
    x = 0;
    y = 0;
  }

  if (width <= 0 || height <= 0) {
    c2t_log_warning("screenshot", "Invalid Windows screen dimensions (%dx%d)", width, height);
    return 0;
  }

  HDC screen = GetDC(NULL);
  if (!screen) {
    c2t_log_warning("screenshot", "GetDC failed (error %lu)", GetLastError());
    return 0;
  }

  HDC memory = CreateCompatibleDC(screen);
  HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
  if (!memory || !bitmap) {
    c2t_log_warning("screenshot", "GDI DC or bitmap creation failed");
    if (bitmap) DeleteObject(bitmap);
    if (memory) DeleteDC(memory);
    ReleaseDC(NULL, screen);
    return 0;
  }

  HGDIOBJ prev = SelectObject(memory, bitmap);
  if (!BitBlt(memory, 0, 0, width, height, screen, x, y, SRCCOPY | CAPTUREBLT)) {
    c2t_log_warning("screenshot", "BitBlt failed (error %lu)", GetLastError());
    SelectObject(memory, prev);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(NULL, screen);
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
    SelectObject(memory, prev);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(NULL, screen);
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
  if (!GetDIBits(memory, bitmap, 0, (UINT)height, pixels, &bmi, DIB_RGB_COLORS)) {
    c2t_log_warning("screenshot", "GetDIBits failed (error %lu)", GetLastError());
    free(bmp_buf);
    SelectObject(memory, prev);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(NULL, screen);
    return 0;
  }

  SelectObject(memory, prev);
  DeleteObject(bitmap);
  DeleteDC(memory);
  ReleaseDC(NULL, screen);

  *out_data = bmp_buf;
  *out_size = total_size;
  *out_mime_type = "image/bmp";
  *out_filename = "screenshot.bmp";
  c2t_log_info("screenshot", "Captured %dx%d Windows desktop screenshot (%zu bytes BMP)", width, height, total_size);
  return 1;
}

#endif
