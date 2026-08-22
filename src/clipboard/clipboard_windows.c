/*
 * Copyright (C) 2026 Antonio Ricciardi
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

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600

#include "clipboard.h"
#include "clipboard_output.h"
#include "../config/config.h"
#include "../logging/logging.h"
#include "../runtime/runtime.h"

#include <windows.h>
#include <shellapi.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    DWORD files_offset;
    POINT drop_point;
    BOOL non_client;
    BOOL wide;
} c2t_dropfiles_t;

static void wide_to_utf8(const wchar_t *wide, char *output, size_t capacity)
{
    output[0] = '\0';
    int required = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0,
                                       nullptr, nullptr);
    char *converted = required > 0 ? malloc((size_t)required) : nullptr;
    if (!converted || !WideCharToMultiByte(CP_UTF8, 0, wide, -1, converted,
                                           required, nullptr, nullptr)) {
        free(converted);
        return;
    }

    size_t length = (size_t)required - 1;
    size_t copied = length < capacity - 1 ? length : capacity - 1;
    if (copied < length) {
        while (copied > 0 &&
               ((unsigned char)converted[copied] & 0xc0) == 0x80)
            --copied;
    }
    memcpy(output, converted, copied);
    output[copied] = '\0';
    free(converted);
}

[[nodiscard]] static int capture_source(c2t_clipboard_source_t *source)
{
    memset(source, 0, sizeof(*source));
    if (!c2t_config_get()->telegram_send_window_info)
        return 0;

    HWND source_window = GetForegroundWindow();
    if (!source_window)
        return 0;

    wchar_t title[512] = {};
    int title_length = GetWindowTextW(source_window, title,
                                      (int)(sizeof(title) / sizeof(title[0])));
    if (title_length > 0)
        wide_to_utf8(title, source->title, sizeof(source->title));

    DWORD process_id = 0;
    GetWindowThreadProcessId(source_window, &process_id);
    source->process_id = (uint32_t)process_id;
    HANDLE process = process_id
        ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id)
        : nullptr;
    if (process) {
        wchar_t path[32768] = {};
        DWORD path_length = (DWORD)(sizeof(path) / sizeof(path[0]));
        if (QueryFullProcessImageNameW(process, 0, path, &path_length) &&
            path_length < (DWORD)(sizeof(path) / sizeof(path[0]))) {
            path[path_length] = L'\0';
            const wchar_t *name = path;
            for (const wchar_t *cursor = path; *cursor; ++cursor) {
                if (*cursor == L'\\' || *cursor == L'/')
                    name = cursor + 1;
            }
            wide_to_utf8(name, source->application,
                         sizeof(source->application));
        }
        CloseHandle(process);
    }
    return source->application[0] || source->title[0] || source->process_id;
}

static void write_u16_le(unsigned char *output, uint16_t value)
{
    output[0] = (unsigned char)(value & 0xff);
    output[1] = (unsigned char)((value >> 8) & 0xff);
}

static void write_u32_le(unsigned char *output, uint32_t value)
{
    output[0] = (unsigned char)(value & 0xff);
    output[1] = (unsigned char)((value >> 8) & 0xff);
    output[2] = (unsigned char)((value >> 16) & 0xff);
    output[3] = (unsigned char)((value >> 24) & 0xff);
}

[[nodiscard]] static int output_bitmap(UINT format,
                                      const c2t_clipboard_source_t *source)
{
    c2t_log_debug("windows", "Reading bitmap clipboard format %u", format);
    HANDLE handle = GetClipboardData(format);
    const BITMAPINFOHEADER *info = handle ? GlobalLock(handle) : nullptr;
    SIZE_T dib_size = handle ? GlobalSize(handle) : 0;
    if (!info || dib_size < sizeof(BITMAPINFOHEADER) ||
        info->biSize < sizeof(BITMAPINFOHEADER) || info->biSize > dib_size ||
        dib_size > UINT32_MAX - 14) {
        if (info)
            GlobalUnlock(handle);
        return 0;
    }

    size_t color_bytes = 0;
    if (info->biSize == sizeof(BITMAPINFOHEADER) &&
        info->biCompression == BI_BITFIELDS)
        color_bytes += 3 * sizeof(DWORD);
    if ((size_t)info->biSize + color_bytes > (size_t)dib_size) {
        GlobalUnlock(handle);
        return 0;
    }
    if (info->biClrUsed) {
        if ((size_t)info->biClrUsed >
            ((size_t)dib_size - (size_t)info->biSize - color_bytes) /
            sizeof(RGBQUAD)) {
            GlobalUnlock(handle);
            return 0;
        }
        color_bytes += (size_t)info->biClrUsed * sizeof(RGBQUAD);
    } else if (info->biBitCount <= 8) {
        color_bytes += ((size_t)1 << info->biBitCount) * sizeof(RGBQUAD);
    }

    size_t pixel_offset = 14 + (size_t)info->biSize + color_bytes;
    size_t file_size = 14 + (size_t)dib_size;
    if (pixel_offset > file_size || file_size > UINT32_MAX ||
        file_size > c2t_config_get()->queue_max_bytes) {
        c2t_log_warning("windows", "Bitmap exceeds the delivery queue limit");
        GlobalUnlock(handle);
        return 0;
    }

    unsigned char *bitmap = malloc(file_size);
    if (!bitmap) {
        GlobalUnlock(handle);
        return 0;
    }
    bitmap[0] = 'B';
    bitmap[1] = 'M';
    write_u32_le(bitmap + 2, (uint32_t)file_size);
    write_u16_le(bitmap + 6, 0);
    write_u16_le(bitmap + 8, 0);
    write_u32_le(bitmap + 10, (uint32_t)pixel_offset);
    memcpy(bitmap + 14, info, (size_t)dib_size);
    GlobalUnlock(handle);

    clipboard_output(bitmap, file_size, "image/bmp", source);
    free(bitmap);
    return 1;
}

static void output_text(const c2t_clipboard_source_t *source)
{
    c2t_log_debug("windows", "Reading Unicode text from clipboard");
    HANDLE handle = GetClipboardData(CF_UNICODETEXT);

    const wchar_t *wide_text = handle ? GlobalLock(handle) : nullptr;
    SIZE_T byte_size = handle ? GlobalSize(handle) : 0;
    if (!wide_text)
        return;
    if (byte_size < sizeof(wchar_t)) {
        GlobalUnlock(handle);
        return;
    }

    size_t capacity = (size_t)byte_size / sizeof(wchar_t);
    size_t wide_length = 0;
    while (wide_length < capacity && wide_text[wide_length])
        ++wide_length;
    if (wide_length == capacity || wide_length > INT32_MAX) {
        c2t_log_warning("windows", "Ignoring unterminated clipboard text");
        GlobalUnlock(handle);
        return;
    }
    int utf8_size = WideCharToMultiByte(
        CP_UTF8, 0, wide_text, (int)wide_length, nullptr, 0, nullptr, nullptr);
    if (utf8_size < 0 ||
        (size_t)utf8_size > c2t_config_get()->queue_max_bytes) {
        c2t_log_warning("windows", "Text exceeds the delivery queue limit");
        GlobalUnlock(handle);
        return;
    }
    char *utf8 = utf8_size > 0 ? malloc((size_t)utf8_size) : nullptr;
    if (utf8 && WideCharToMultiByte(
            CP_UTF8, 0, wide_text, (int)wide_length, utf8, utf8_size,
            nullptr, nullptr)) {
        clipboard_output(utf8, (size_t)utf8_size,
                         "text/plain;charset=utf-8", source);
    }

    free(utf8);
    GlobalUnlock(handle);
}

[[nodiscard]] static int output_wide_file(const wchar_t *wide, int wide_length,
                                         const c2t_clipboard_source_t *source)
{
    int utf8_length = WideCharToMultiByte(CP_UTF8, 0, wide, wide_length,
                                          nullptr, 0, nullptr, nullptr);
    if (utf8_length < 0 ||
        (size_t)utf8_length > c2t_config_get()->queue_max_bytes) {
        c2t_log_warning("windows", "File path exceeds the delivery queue limit");
        return 0;
    }
    char *utf8 = utf8_length > 0 ? malloc((size_t)utf8_length) : nullptr;
    if (!utf8 || !WideCharToMultiByte(CP_UTF8, 0, wide, wide_length, utf8,
                                      utf8_length, nullptr, nullptr)) {
        free(utf8);
        return 0;
    }
    clipboard_output(utf8, (size_t)utf8_length, "text/uri-list", source);
    free(utf8);
    return 1;
}

[[nodiscard]] static int output_files(const c2t_clipboard_source_t *source)
{
    HANDLE handle = GetClipboardData(CF_HDROP);
    const c2t_dropfiles_t *drop = handle ? GlobalLock(handle) : nullptr;
    SIZE_T total_size = handle ? GlobalSize(handle) : 0;
    if (!drop || total_size < sizeof(*drop) ||
        drop->files_offset >= total_size) {
        if (drop)
            GlobalUnlock(handle);
        return 0;
    }

    const unsigned char *cursor = (const unsigned char *)drop + drop->files_offset;
    SIZE_T remaining = total_size - drop->files_offset;
    int handled = 0;
    if (drop->wide) {
        while (remaining >= sizeof(wchar_t)) {
            const wchar_t *wide = (const wchar_t *)cursor;
            size_t wide_capacity = remaining / sizeof(wchar_t);
            size_t length = 0;
            while (length < wide_capacity && wide[length])
                ++length;
            if (length == 0 || length == wide_capacity || length > INT32_MAX)
                break;
            handled |= output_wide_file(wide, (int)length, source);
            size_t consumed = (length + 1) * sizeof(wchar_t);
            cursor += consumed;
            remaining -= consumed;
        }
    } else {
        while (remaining > 0) {
            const unsigned char *terminator = memchr(cursor, '\0', remaining);
            if (!terminator)
                break;
            size_t length = (size_t)(terminator - cursor);
            if (length <= INT32_MAX) {
                int wide_length = MultiByteToWideChar(
                    CP_ACP, 0, (const char *)cursor, (int)length, nullptr, 0);
                wchar_t *wide = wide_length > 0
                    ? malloc((size_t)wide_length * sizeof(*wide)) : nullptr;
                if (wide && MultiByteToWideChar(
                        CP_ACP, 0, (const char *)cursor, (int)length, wide,
                        wide_length))
                    handled |= output_wide_file(wide, wide_length, source);
                free(wide);
            }
            size_t consumed = length + 1;
            cursor += consumed;
            remaining -= consumed;
        }
    }
    GlobalUnlock(handle);
    return handled;
}

static void output_clipboard(void)
{
    c2t_clipboard_source_t source = {};
    const c2t_clipboard_source_t *source_pointer =
        capture_source(&source) ? &source : nullptr;
    int opened = 0;
    for (unsigned int attempt = 0; attempt < 5 && !opened; ++attempt) {
        opened = OpenClipboard(nullptr);
        if (!opened && attempt < 4)
            Sleep(10U << attempt);
    }
    if (!opened) {
        c2t_log_warning("windows", "Unable to open clipboard (error %lu)",
                        (unsigned long)GetLastError());
        return;
    }

    int handled = 0;
    if (c2t_config_get()->telegram_send_files &&
        IsClipboardFormatAvailable(CF_HDROP))
        handled = output_files(source_pointer);
#ifdef CF_DIBV5
    if (!handled && IsClipboardFormatAvailable(CF_DIBV5))
        handled = output_bitmap(CF_DIBV5, source_pointer);
#endif
    if (!handled && IsClipboardFormatAvailable(CF_DIB))
        handled = output_bitmap(CF_DIB, source_pointer);
    if (!handled && IsClipboardFormatAvailable(CF_UNICODETEXT))
        output_text(source_pointer);

    CloseClipboard();
}

static LRESULT CALLBACK window_callback(HWND window, UINT message,
                                        WPARAM wparam, LPARAM lparam)
{
    if (message == WM_CLIPBOARDUPDATE) {
        c2t_log_debug("windows", "Clipboard update event received");
        output_clipboard();
        return 0;
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

static int clipboard_listen_once(void)
{
    c2t_log_debug("windows", "Creating clipboard listener window");
    static const wchar_t class_name[] = L"c2c_clipboard_listener";
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW window_class = {
        .lpfnWndProc = window_callback,
        .hInstance = instance,
        .lpszClassName = class_name
    };

    if (!RegisterClassW(&window_class)) {
        c2t_log_error("windows",
                      "Unable to register clipboard listener (error %lu)",
                      (unsigned long)GetLastError());
        return 1;
    }

    HWND window = CreateWindowExW(
        0, class_name, L"", 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, instance, nullptr);
    if (!window) {
        c2t_log_error("windows",
                      "Unable to create clipboard listener (error %lu)",
                      (unsigned long)GetLastError());
        UnregisterClassW(class_name, instance);
        return 1;
    }

    if (!AddClipboardFormatListener(window)) {
        c2t_log_error("windows",
                      "Unable to listen to clipboard (error %lu)",
                      (unsigned long)GetLastError());
        DestroyWindow(window);
        UnregisterClassW(class_name, instance);
        return 1;
    }

    c2t_log_info("windows", "Listening for clipboard updates");

    MSG message = {};
    int result = 0;
    while (!c2t_runtime_stop_requested()) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT)
                goto message_loop_finished;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(100);
    }

message_loop_finished:
    RemoveClipboardFormatListener(window);
    DestroyWindow(window);
    UnregisterClassW(class_name, instance);
    return result == 0 ? 0 : 2;
}

int clipboard_listen(void)
{
    unsigned int retry = 0;
    while (!c2t_runtime_stop_requested()) {
        int result = clipboard_listen_once();
        if (result != 2)
            return result;
        unsigned int delay = retry < 5 ? 1U << retry : 30U;
        if (retry < 5)
            ++retry;
        c2t_log_warning("windows", "Restarting clipboard listener in %u seconds",
                        delay);
        for (unsigned int elapsed = 0;
             elapsed < delay * 1000U && !c2t_runtime_stop_requested();
             elapsed += 100U)
            Sleep(100);
    }
    return 0;
}
