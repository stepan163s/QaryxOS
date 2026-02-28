#include "input.h"
#include <libinput.h>
#include <libudev.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <linux/input-event-codes.h>

static struct libinput  *g_li = NULL;
static struct udev      *g_udev = NULL;

static int open_restricted(const char *path, int flags, void *ud) {
    (void)ud;
    int fd = open(path, flags | O_CLOEXEC);
    if (fd < 0) perror(path);
    return fd;
}

static void close_restricted(int fd, void *ud) {
    (void)ud;
    close(fd);
}

static const struct libinput_interface g_iface = {
    .open_restricted  = open_restricted,
    .close_restricted = close_restricted,
};

int input_init(void) {
    g_udev = udev_new();
    if (!g_udev) {
        fprintf(stderr, "input: udev_new failed\n");
        return -1;
    }

    g_li = libinput_udev_create_context(&g_iface, NULL, g_udev);
    if (!g_li) {
        fprintf(stderr, "input: libinput_udev_create_context failed\n");
        return -1;
    }

    if (libinput_udev_assign_seat(g_li, "seat0") < 0) {
        fprintf(stderr, "input: libinput_udev_assign_seat failed\n");
        return -1;
    }

    /* Drain initial device-added events */
    libinput_dispatch(g_li);
    struct libinput_event *ev;
    while ((ev = libinput_get_event(g_li))) libinput_event_destroy(ev);

    fprintf(stderr, "input: libinput ready\n");
    return 0;
}

int input_fd(void) {
    return g_li ? libinput_get_fd(g_li) : -1;
}

static const char *keycode_to_name(uint32_t code) {
    switch (code) {
        case KEY_UP:       return "up";
        case KEY_DOWN:     return "down";
        case KEY_LEFT:     return "left";
        case KEY_RIGHT:    return "right";
        case KEY_ENTER:
        case KEY_KPENTER:  return "ok";
        case KEY_ESC:
        case KEY_BACK:     return "back";
        case KEY_H:
        case KEY_HOME:     return "home";
        case KEY_PLAYPAUSE:return "play";
        case KEY_VOLUMEUP: return "vol_up";
        case KEY_VOLUMEDOWN:return "vol_down";
        default:           return NULL;
    }
}

int input_dispatch(const char **queue, int max) {
    if (!g_li) return 0;
    libinput_dispatch(g_li);

    int n = 0;
    struct libinput_event *ev;
    while (n < max && (ev = libinput_get_event(g_li))) {
        if (libinput_event_get_type(ev) == LIBINPUT_EVENT_KEYBOARD_KEY) {
            struct libinput_event_keyboard *ke = libinput_event_get_keyboard_event(ev);
            if (libinput_event_keyboard_get_key_state(ke) == LIBINPUT_KEY_STATE_PRESSED) {
                uint32_t code = libinput_event_keyboard_get_key(ke);
                const char *name = keycode_to_name(code);
                if (name) queue[n++] = name;
            }
        }
        libinput_event_destroy(ev);
    }
    return n;
}

void input_destroy(void) {
    if (g_li)   { libinput_unref(g_li);   g_li   = NULL; }
    if (g_udev) { udev_unref(g_udev);     g_udev = NULL; }
}
