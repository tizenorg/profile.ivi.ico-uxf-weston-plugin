/*
 * Copyright © 2012 Intel Corporation
 * Copyright © 2013 TOYOTA MOTOR CORPORATION
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */
/**
 * @brief   Wayland Application for unit test of Weston(Wayland) IVI plugins
 *
 * @date    Feb-08-2013
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <poll.h>
#include <wayland-client.h>
#include <linux/input.h>
#include "ico_window_mgr-client-protocol.h"
#include "ico_input_mgr-client-protocol.h"
#include "test-common.h"

#define MAX_CON_NAME    127

struct display {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shell *shell;
    struct ico_window_mgr *ico_window_mgr;
    struct ico_exinput *ico_exinput;
    struct input *input;
    struct output *output;
    struct surface *surface;
    int    init_color;
    int    init_width;
    int    init_height;
    int    prompt;
    char   connect[MAX_CON_NAME+1];
};

struct input {
    struct display *display;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;
    float x, y;
    uint32_t button_mask;
    struct surface *pointer_focus;
    struct surface *keyboard_focus;
    uint32_t last_key, last_key_state;
};

struct output {
    struct display *display;
    struct wl_output *output;
    int x, y;
    int width, height;
};

struct surface {
    struct display *display;
    struct wl_surface *surface;
    struct wl_shell_surface *shell_surface;
    struct output *output;
    EGLDisplay  dpy;
    EGLConfig   conf;
    EGLContext  ctx;
    EGLSurface  egl_surface;
};

static void clear_surface(struct display *display);

static void
pointer_handle_enter(void *data, struct wl_pointer *pointer,
                     uint32_t serial, struct wl_surface *surface,
                     wl_fixed_t x, wl_fixed_t y)
{
    struct input *input = data;

    input->pointer_focus = wl_surface_get_user_data(surface);
    input->x = wl_fixed_to_double(x);
    input->y = wl_fixed_to_double(y);
    print_log("CLIENT: got pointer enter (%d,%d), surface %p",
              (int)input->x, (int)input->y, surface);
}

static void
pointer_handle_leave(void *data, struct wl_pointer *pointer,
                     uint32_t serial, struct wl_surface *surface)
{
    struct input *input = data;

    input->pointer_focus = NULL;

    print_log("CLIENT: got pointer leave, surface %p", surface);
}

static void
pointer_handle_motion(void *data, struct wl_pointer *pointer,
                      uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
    struct input *input = data;

    input->x = wl_fixed_to_double(x);
    input->y = wl_fixed_to_double(y);

    print_log("CLIENT: got pointer motion (%d,%d)", (int)input->x, (int)input->y);
}

static void
pointer_handle_button(void *data, struct wl_pointer *pointer,
                      uint32_t serial, uint32_t time, uint32_t button, uint32_t state_w)
{
    struct input *input = data;
    uint32_t bit;
    enum wl_pointer_button_state state = state_w;

    bit = 1 << (button - BTN_LEFT);
    if (state == WL_POINTER_BUTTON_STATE_PRESSED)
        input->button_mask |= bit;
    else
        input->button_mask &= ~bit;
    print_log("CLIENT: got pointer button %u %u", button, state_w);
}

static void
pointer_handle_axis(void *data, struct wl_pointer *pointer,
                    uint32_t time, uint32_t axis, wl_fixed_t value)
{
    fprintf(stderr, "CLIENT: got pointer axis %u %d\n", axis, value);
}

static void
keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard,
                       uint32_t format, int fd, uint32_t size)
{
    close(fd);
    print_log("CLIENT: got keyboard keymap");
}

static void
keyboard_handle_enter(void *data, struct wl_keyboard *keyboard,
                      uint32_t serial, struct wl_surface *surface, struct wl_array *keys)
{
    struct input *input = data;

    input->keyboard_focus = wl_surface_get_user_data(surface);
    print_log("CLIENT: got keyboard enter, surface %p", surface);
}

static void
keyboard_handle_leave(void *data, struct wl_keyboard *keyboard,
                      uint32_t serial, struct wl_surface *surface)
{
    struct input *input = data;

    input->keyboard_focus = NULL;
    print_log("CLIENT: got keyboard leave, surface %p", surface);
}

static void
keyboard_handle_key(void *data, struct wl_keyboard *keyboard,
                    uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
    struct input *input = data;

    input->last_key = key;
    input->last_key_state = state;

    print_log("CLIENT: got keyboard key %u %u", key, state);
}

static void
keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard,
                          uint32_t serial, uint32_t mods_depressed,
                          uint32_t mods_latched, uint32_t mods_locked, uint32_t group)
{
    print_log("CLIENT: got keyboard modifier");
}

static const struct wl_pointer_listener pointer_listener = {
    pointer_handle_enter,
    pointer_handle_leave,
    pointer_handle_motion,
    pointer_handle_button,
    pointer_handle_axis,
};

static const struct wl_keyboard_listener keyboard_listener = {
    keyboard_handle_keymap,
    keyboard_handle_enter,
    keyboard_handle_leave,
    keyboard_handle_key,
    keyboard_handle_modifiers,
};

static void
seat_handle_capabilities(void *data, struct wl_seat *seat,
                         enum wl_seat_capability caps)
{
    struct input *input = data;

    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !input->pointer) {
        input->pointer = wl_seat_get_pointer(seat);
        wl_pointer_set_user_data(input->pointer, input);
        wl_pointer_add_listener(input->pointer, &pointer_listener, input);
    }
    else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && input->pointer) {
        wl_pointer_destroy(input->pointer);
        input->pointer = NULL;
    }

    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !input->keyboard) {
        input->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_set_user_data(input->keyboard, input);
        wl_keyboard_add_listener(input->keyboard, &keyboard_listener, input);
    }
    else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && input->keyboard) {
        wl_keyboard_destroy(input->keyboard);
        input->keyboard = NULL;
    }
}

static const struct wl_seat_listener seat_listener = {
    seat_handle_capabilities,
};

static void
output_handle_geometry(void *data, struct wl_output *wl_output, int x, int y,
                       int physical_width, int physical_height, int subpixel,
                       const char *make, const char *model, int32_t transform)
{
    struct output *output = data;

    print_log("CLIENT: Event[handle_geometry] x/y=%d/%d p.w/h=%d/%d trans=%d",
              x, y, physical_width, physical_height, transform);

    output->x = x;
    output->y = y;
}

static void
output_handle_mode(void *data, struct wl_output *wl_output, uint32_t flags,
                   int width, int height, int refresh)
{
    struct output *output = data;

    print_log("CLIENT: Event[handle_mode] %08x x/y=%d/%d flags=%08x refresh=%d",
              (int)wl_output, width, height, flags, refresh);

    if (flags & WL_OUTPUT_MODE_CURRENT) {
        output->width = width;
        output->height = height;
    }
}

static const struct wl_output_listener output_listener = {
    output_handle_geometry,
    output_handle_mode
};

static void
cb_input_capabilities(void *data, struct ico_exinput *ico_exinput,
                      const char *device, int32_t type, const char *swname, int32_t input,
                      const char *codename, int32_t code)
{
    print_log("CLIENT: Event[input_capabilities] device=%s type=%d sw=%s input=%d "
              "code=%s[%d]", device, type, swname, input, codename, code);
}

static void
cb_input_code(void *data, struct ico_exinput *ico_exinput,
              const char *device, int32_t input, const char *codename, int32_t code)
{
    print_log("CLIENT: Event[input_code] device=%s input=%d code=%s[%d]",
              device, input, codename, code);
}

static void
cb_input_input(void *data, struct ico_exinput *ico_exinput, uint32_t time,
               const char *device, int32_t input, int32_t code, int32_t state)
{
    print_log("CLIENT: Event[input_input] device=%s input=%d code=%d state=%d",
              device, input, code, state);
}

static const struct ico_exinput_listener exinput_listener = {
    cb_input_capabilities,
    cb_input_code,
    cb_input_input
};

static void
shell_surface_ping(void *data, struct wl_shell_surface *wl_shell_surface, uint32_t serial)
{
    print_log("CLIENT: shell_surface_ping: surface=%08x serial=%d",
              (int)wl_shell_surface, serial);
}

static void
shell_surface_configure(void *data, struct wl_shell_surface *wl_shell_surface,
                        uint32_t edges, int32_t width, int32_t height)
{
    print_log("CLIENT: shell_surface_configure: surface=%08x edg=%x, width=%d height=%d",
              (int)wl_shell_surface, edges, width, height);
}

static void
shell_surface_popup_done(void *data, struct wl_shell_surface *wl_shell_surface)
{
    print_log("CLIENT: shell_surface_popup_done: surface=%08x", (int)wl_shell_surface);
}

static const struct wl_shell_surface_listener shell_surface_listener = {
    shell_surface_ping,
    shell_surface_configure,
    shell_surface_popup_done
};

static void
handle_global(void *data, struct wl_registry *registry, uint32_t id,
              const char *interface, uint32_t version)
{
    struct display *display = data;
    struct input *input;
    struct output *output;

    print_log("CLIENT: handle_global: interface=<%s> id=%d", interface, (int)id);

    if (strcmp(interface, "wl_compositor") == 0) {
        display->compositor = wl_registry_bind(display->registry, id,
                                               &wl_compositor_interface, 1);
    }
    else if (strcmp(interface, "wl_seat") == 0) {
        input = calloc(1, sizeof *input);
        input->display = display;
        input->seat = wl_registry_bind(display->registry, id, &wl_seat_interface, 1);
        input->pointer_focus = NULL;
        input->keyboard_focus = NULL;

        wl_seat_add_listener(input->seat, &seat_listener, input);
        display->input = input;
    }
    else if (strcmp(interface, "wl_output") == 0) {
        output = malloc(sizeof *output);
        output->display = display;
        output->output = wl_registry_bind(display->registry, id, &wl_output_interface, 1);
        wl_output_add_listener(output->output,
                       &output_listener, output);
        display->output = output;

        print_log("CLIENT: created output global %p", display->output);
    }
    else if (strcmp(interface, "wl_shell") == 0)    {
        display->shell = wl_registry_bind(display->registry, id, &wl_shell_interface, 1);
    }
    else if (strcmp(interface, "ico_window_mgr") == 0)  {
        display->ico_window_mgr = wl_registry_bind(display->registry, id,
                                                   &ico_window_mgr_interface, 1);
        print_log("CLIENT: created window_mgr global %p", display->ico_window_mgr);
    }
    else if (strcmp(interface, "ico_exinput") == 0)   {
        display->ico_exinput = wl_registry_bind(display->registry, id,
                                                &ico_exinput_interface, 1);
        ico_exinput_add_listener(display->ico_exinput, &exinput_listener, display);
        print_log("CLIENT: created exinput global %p", display->ico_exinput);
    }
}

static const struct wl_registry_listener registry_listener = {
    handle_global
};

static void
surface_enter(void *data, struct wl_surface *wl_surface, struct wl_output *output)
{
    struct surface *surface = data;

    surface->output = wl_output_get_user_data(output);

    print_log("CLIENT: got surface enter, output %p", surface->output);
}

static void
surface_leave(void *data, struct wl_surface *wl_surface, struct wl_output *output)
{
    struct surface *surface = data;

    surface->output = NULL;

    print_log("CLIENT: got surface leave, output %p", wl_output_get_user_data(output));
}

static const struct wl_surface_listener surface_listener = {
    surface_enter,
    surface_leave
};

static void
send_keyboard_state(struct display *display)
{
    int focus = display->input->keyboard_focus != NULL;

    if (focus) {
        assert(display->input->keyboard_focus == display->surface);
    }

    wl_display_flush(display->display);

    print_log("CLIENT: keyboard_state %u %u %d",
              display->input->last_key, display->input->last_key_state, focus);

    wl_display_roundtrip(display->display);
}

static void
send_button_state(struct display *display)
{
    wl_display_roundtrip(display->display);

    print_log("CLIENT: button_state %u", display->input->button_mask);

    wl_display_roundtrip(display->display);
}

static void
send_state(struct display* display)
{
    int visible = display->surface->output != NULL;
    wl_fixed_t x = wl_fixed_from_int(-1);
    wl_fixed_t y = wl_fixed_from_int(-1);

    if (display->input->pointer_focus != NULL) {
        assert(display->input->pointer_focus == display->surface);
        x = wl_fixed_from_double(display->input->x);
        y = wl_fixed_from_double(display->input->y);
    }

    if (visible) {
        /* FIXME: this fails on multi-display setup */
        /* assert(display->surface->output == display->output); */
    }

    wl_display_flush(display->display);

    print_log("CLIENT: state %d %d %d", x, y, visible);

    wl_display_roundtrip(display->display);
}

static void
create_surface(struct display *display, const char *title)
{
    struct surface *surface;

    surface = malloc(sizeof *surface);
    assert(surface);
    surface->display = display;
    display->surface = surface;
    surface->surface = wl_compositor_create_surface(display->compositor);
    wl_surface_add_listener(surface->surface, &surface_listener, surface);

    if (display->shell) {
        surface->shell_surface =
            wl_shell_get_shell_surface(display->shell, surface->surface);
        if (surface->shell_surface) {
            wl_shell_surface_add_listener(surface->shell_surface,
                                          &shell_surface_listener, display);
            wl_shell_surface_set_toplevel(surface->shell_surface);
            wl_shell_surface_set_title(surface->shell_surface, title);
        }
    }
    wl_display_flush(display->display);

    print_log("CLIENT: create surface %d shell=%08x",
              wl_proxy_get_id((struct wl_proxy *) surface->surface),
              (int)surface->shell_surface);

    poll(NULL, 0, 100); /* Wait for next frame where we'll get events. */

    wl_display_roundtrip(display->display);

    surface->dpy = opengl_init(display->display, &surface->conf, &surface->ctx);
    if (surface->dpy)   {
        surface->egl_surface = opengl_create_window(display->display, surface->surface,
                                                    surface->dpy, surface->conf,
                                                    surface->ctx, display->init_width,
                                                    display->init_height,
                                                    display->init_color);
        clear_surface(display);
        print_log("CLIENT: created egl_surface %08x", (int)surface->egl_surface);
    }
}

static void
clear_surface(struct display *display)
{
    if (! display->surface) {
        create_surface(display, "test-client");
    }
    else    {
        opengl_clear_window(display->init_color);
        opengl_swap_buffer(display->display,
                           display->surface->dpy, display->surface->egl_surface);
    }
}

int main(int argc, char *argv[])
{
    struct display *display;
    char buf[256];
    int ret, fd;
    int msec;
    int postsec = 0;

    display = malloc(sizeof *display);
    assert(display);
    memset((char *)display, 0, sizeof *display);

    display->init_width = 640;
    display->init_height = 480;
    display->init_color = 0xA0A08020;
    for (fd = 1; fd < argc; fd++ )  {
        if (argv[fd][0] == '-') {
            if (strncasecmp(argv[fd], "-color=", 7) == 0)   {
                display->init_color = strtoul(&argv[fd][7], (char **)0, 0);
            }
            else if (strncasecmp(argv[fd], "-width=", 7) == 0)  {
                display->init_width = strtol(&argv[fd][7], (char **)0, 0);
            }
            else if (strncasecmp(argv[fd], "-height=", 8) == 0) {
                display->init_height = strtol(&argv[fd][8], (char **)0, 0);
            }
            else if (strncasecmp(argv[fd], "-display=", 9) == 0)   {
                strncpy(display->connect, &argv[fd][9], MAX_CON_NAME);
            }
            else if (strncasecmp(argv[fd], "-postsleep=", 11) == 0)   {
                postsec = sec_str_2_value(&argv[fd][11]);
            }
            else if (strncasecmp(argv[fd], "-prompt=", 8) == 0)  {
                if (argv[fd][8] == 0)   {
                    display->prompt = argv[fd][8] & 1;
                }
                else    {
                    display->prompt = 1;
                }
            }
        }
    }

    if (display->connect[0])    {
        display->display = wl_display_connect(display->connect);
    }
    else    {
        display->display = wl_display_connect(NULL);
    }
    assert(display->display);

    display->registry = wl_display_get_registry(display->display);
    wl_registry_add_listener(display->registry,
                 &registry_listener, display);
    wl_display_dispatch(display->display);
    sleep_with_wayland(display->display, 1000);

    fd = 0;

    while (1) {
        sleep_with_wayland(display->display, 20);
        if (display->prompt)    {
            printf("CLIENT: "); fflush(stdout);
        }
        ret = getdata(display->ico_window_mgr, "CLIENT: ", fd, buf, sizeof(buf));
        if (ret < 0) {
            fprintf(stderr, "CLIENT: read error: fd %d, %m\n",
                fd);
            break;
        }
        if (ret == 0)   continue;
        wl_display_flush(display->display);

        if ((strncasecmp(buf, "bye", 3) == 0) ||
            (strncasecmp(buf, "quit", 4) == 0) ||
            (strncasecmp(buf, "end", 3) == 0))  {
            /* Exit, end of test            */
            break;
        }
        else if (strncasecmp(buf, "create-surface", ret) == 0) {
            create_surface(display, "test-client");
        }
        else if (strncasecmp(buf, "clear-surface", 13) == 0) {
            display->init_color = strtoul(&buf[14], (char **)0, 0);
            clear_surface(display);
        }
        else if (strncasecmp(buf, "send-state", ret) == 0) {
            send_state(display);
        }
        else if (strncasecmp(buf, "send-button-state", ret) == 0) {
            send_button_state(display);
        }
        else if (strncasecmp(buf, "send-keyboard-state", ret) == 0) {
            send_keyboard_state(display);
        }
        else if (strncasecmp(buf, "sleep", 5) == 0) {
            msec = sec_str_2_value(&buf[6]);
            sleep_with_wayland(display->display, msec);
        }
        else {
            print_log("CLIENT: unknown command[%s]", buf);
            return(-1);
        }
    }
    if (postsec > 0)    {
        sleep_with_wayland(display->display, postsec);
    }

    exit(0);
}

