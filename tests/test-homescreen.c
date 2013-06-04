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
 * @brief   HomeScreen for uint test of Weston(Wayland) IVI plugins
 *
 * @date    Feb-08-2013
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <poll.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <linux/input.h>
#include <wayland-client.h>
#include "ico_ivi_shell-client-protocol.h"
#include "ico_window_mgr-client-protocol.h"
#include "ico_input_mgr-client-protocol.h"
#include "test-common.h"

#define MAX_APPID   128
#define ICO_IVI_MAX_COORDINATE  16383

struct surface_name {
    struct surface_name *next;
    int     surfaceid;
    int     pid;
    char    appid[MAX_APPID];
    int     x;
    int     y;
    int     width;
    int     height;
    int     visible;
};

#define MAX_CON_NAME    127

struct display {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shell *shell;
    struct ico_ivi_shell *ico_ivi_shell;
    struct ico_window_mgr *ico_window_mgr;
    struct ico_input_mgr_control *ico_input_mgr;
    struct ico_input_mgr_device *ico_input_device;
    struct ico_exinput *ico_exinput;
    struct input *input;
    struct output *output;
    struct surface *surface;
    struct surface_name *surface_name;
    struct surface_name *bgsurface_name;
    int    bg_created;
    int    init_color;
    int    init_width;
    int    init_height;
    int    surface_created;
    int    surface_destroyed;
    int    prompt;
    int    visible_on_create;
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
    print_log("HOMESCREEN: got pointer enter (%d,%d), surface %p",
              (int)input->x, (int)input->y, surface);
}

static void
pointer_handle_leave(void *data, struct wl_pointer *pointer,
                     uint32_t serial, struct wl_surface *surface)
{
    struct input *input = data;

    input->pointer_focus = NULL;

    print_log("HOMESCREEN: got pointer leave, surface %p", surface);
}

static void
pointer_handle_motion(void *data, struct wl_pointer *pointer,
                      uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
    struct input *input = data;

    input->x = wl_fixed_to_double(x);
    input->y = wl_fixed_to_double(y);

    print_log("HOMESCREEN: got pointer motion (%d,%d)", (int)input->x, (int)input->y);
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
    print_log("HOMESCREEN: got pointer button %u %u", button, state_w);
}

static void
pointer_handle_axis(void *data, struct wl_pointer *pointer,
                    uint32_t time, uint32_t axis, wl_fixed_t value)
{
    print_log("HOMESCREEN: got pointer axis %u %d", axis, value);
}

static void
keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard,
                       uint32_t format, int fd, uint32_t size)
{
    close(fd);
    print_log("HOMESCREEN: got keyboard keymap");
}

static void
keyboard_handle_enter(void *data, struct wl_keyboard *keyboard,
                      uint32_t serial, struct wl_surface *surface, struct wl_array *keys)
{
    struct input *input = data;

    input->keyboard_focus = wl_surface_get_user_data(surface);
    print_log("HOMESCREEN: got keyboard enter, surface %p", surface);
}

static void
keyboard_handle_leave(void *data, struct wl_keyboard *keyboard,
                      uint32_t serial, struct wl_surface *surface)
{
    struct input *input = data;

    input->keyboard_focus = NULL;
    print_log("HOMESCREEN: got keyboard leave, surface %p", surface);
}

static void
keyboard_handle_key(void *data, struct wl_keyboard *keyboard,
                    uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
    struct input *input = data;

    input->last_key = key;
    input->last_key_state = state;

    print_log("HOMESCREEN: got keyboard key %u %u", key, state);
}

static void
keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard,
                          uint32_t serial, uint32_t mods_depressed,
                          uint32_t mods_latched, uint32_t mods_locked, uint32_t group)
{
    print_log("HOMESCREEN: got keyboard modifier");
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
surface_enter(void *data, struct wl_surface *wl_surface, struct wl_output *output)
{
    struct surface *surface = data;

    surface->output = wl_output_get_user_data(output);

    print_log("HOMESCREEN: got surface enter, output %p", surface->output);
}

static void
surface_leave(void *data, struct wl_surface *wl_surface, struct wl_output *output)
{
    struct surface *surface = data;

    surface->output = NULL;

    print_log("HOMESCREEN: got surface leave, output %p",
              wl_output_get_user_data(output));
}

static const struct wl_surface_listener surface_listener = {
    surface_enter,
    surface_leave
};

static void
create_surface(struct display *display)
{
    struct surface *surface;
    int id;

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
            wl_shell_surface_set_toplevel(surface->shell_surface);
        }
    }
    wl_display_flush(display->display);

    id = wl_proxy_get_id((struct wl_proxy *) surface->surface);
    print_log("HOMESCREEN: create surface = %d", id);

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
        print_log("HOMESCREEN: created egl_surface %08x", (int)surface->egl_surface);
    }
}

static void
clear_surface(struct display *display)
{
    if (! display->surface) {
        create_surface(display);
    }
    else    {
        opengl_clear_window(display->init_color);
        opengl_swap_buffer(display->display,
                           display->surface->dpy, display->surface->egl_surface);
    }
}

static void
output_handle_geometry(void *data, struct wl_output *wl_output, int x, int y,
                       int physical_width, int physical_height, int subpixel,
                       const char *make, const char *model, int32_t transform)
{
    struct output *output = data;

    print_log("HOMESCREEN: Event[handle_geometry] x/y=%d/%d p.w/h=%d/%d trans=%d",
              x, y, physical_width, physical_height, transform);

    output->x = x;
    output->y = y;
}

static void
output_handle_mode(void *data, struct wl_output *wl_output, uint32_t flags,
                   int width, int height, int refresh)
{
    struct output *output = data;

    print_log("HOMESCREEN: Event[handle_mode] x/y=%d/%d flags=%08x refresh=%d",
              width, height, flags, refresh);

    if (flags & WL_OUTPUT_MODE_CURRENT) {
        struct display  *display = output->display;

        output->width = width;
        output->height = height;

        display->init_width = width;
        display->init_height = height;

        if (display->bgsurface_name)    {
            ico_window_mgr_set_positionsize(display->ico_window_mgr,
                                            display->bgsurface_name->surfaceid,
                                            0, 0, width, height);
        }
        else if (display->bg_created == 0)  {
            display->bg_created = 9;
            create_surface(output->display);
        }
    }
}

static const struct wl_output_listener output_listener = {
    output_handle_geometry,
    output_handle_mode
};

static int
search_surface(struct display *display, const char *surfname)
{
    struct surface_name     *p;

    p = display->surface_name;
    while (p)   {
        if (strcmp(p->appid, surfname) == 0)    break;
        p = p->next;
    }

    if (p)  {
        return(p->surfaceid);
    }
    else    {
        return(-1);
    }
}

static struct surface_name *
search_surfacename(struct display *display, const char *surfname)
{
    struct surface_name     *p;

    p = display->surface_name;
    while (p)   {
        if (strcmp(p->appid, surfname) == 0)    break;
        p = p->next;
    }
    return(p);
}

static struct surface_name *
search_surfaceid(struct display *display, const int surfaceid)
{
    struct surface_name     *p;

    p = display->surface_name;
    while (p)   {
        if (p->surfaceid == surfaceid)  {
            return(p);
        }
        p = p->next;
    }
    return(NULL);
}

static void
window_created(void *data, struct ico_window_mgr *ico_window_mgr,
               uint32_t surfaceid, int32_t pid, const char *appid)
{
    struct display *display = data;
    struct surface_name     *p;
    struct surface_name     *fp;

    display->surface_created = 1;
    p = display->surface_name;
    fp = NULL;
    while (p)   {
        if (p->surfaceid == (int)surfaceid) break;
        fp = p;
        p = p->next;
    }
    if (p)  {
        print_log("HOMESCREEN: Event[window_created] surface=%08x(app=%s) exist",
                  (int)surfaceid, appid);
    }
    else    {
        print_log("HOMESCREEN: Event[window_created] new surface=%08x(app=%s)",
                  (int)surfaceid, appid);
        p = malloc(sizeof(struct surface_name));
        if (! p)    {
            return;
        }
        memset(p, 0, sizeof(struct surface_name));
        if (fp) {
            fp->next = p;
        }
        else    {
            display->surface_name = p;
        }
    }
    p->surfaceid = surfaceid;
    p->pid = pid;
    strncpy(p->appid, appid, MAX_APPID-1);

    /* Set default size and show        */
    if (p->width > 0)   {
        ico_window_mgr_set_positionsize(display->ico_window_mgr, surfaceid,
                                        p->x, p->y, p->width, p->height);
    }

    print_log("HOMESCREEN: Created window[%08x] (app=%s)", (int)surfaceid, appid);

    if (strncasecmp(appid, "test-homescreen", 15) == 0) {
        display->bgsurface_name = p;
        if (display->bg_created == 1)   {
            display->bg_created = 9;
            ico_window_mgr_set_positionsize(display->ico_window_mgr, surfaceid,
                                            0, 0, display->init_width, display->init_height);
        }
        ico_window_mgr_set_visible(display->ico_window_mgr, surfaceid, 1, 0);
        print_log("HOMESCREEN: Created window[%08x] (app=%s) Visible",
                  (int)surfaceid, appid);
        p->visible = 1;
    }
}

static void
window_destroyed(void *data, struct ico_window_mgr *ico_window_mgr, uint32_t surfaceid)
{
    struct display *display = data;
    struct surface_name     *p;
    struct surface_name     *fp;

    display->surface_destroyed = 1;
    p = search_surfaceid(display, (int)surfaceid);
    if (! p)    {
        print_log("HOMESCREEN: Event[window_destroyed] surface=%08x dose not exist",
                  (int)surfaceid);
    }
    else    {
        print_log("HOMESCREEN: Event[window_destroyed] surface=%08x", (int)surfaceid);
        if (p == display->surface_name) {
            display->surface_name = p->next;
        }
        else    {
            fp = display->surface_name;
            while (fp)  {
                if (fp->next == p)  {
                    fp->next = p->next;
                    break;
                }
                fp = fp->next;
            }
        }
        free(p);
    }
}

static void
window_visible(void *data, struct ico_window_mgr *ico_window_mgr,
               uint32_t surfaceid, int32_t visible, int32_t raise, int32_t hint)
{
    struct display *display = data;
    struct surface_name     *p;

    p = search_surfaceid(display, (int)surfaceid);
    if (! p)    {
        print_log("HOMESCREEN: Event[window_visible] surface=%08x dose not exist",
                  (int)surfaceid);
    }
    else    {
        print_log("HOMESCREEN: Event[window_visible] surface=%08x "
                  "visible=%d raise=%d hint=%d", (int)surfaceid, visible, raise, hint);
        p->visible = visible;
        if (hint == 0)  {
            ico_window_mgr_set_visible(display->ico_window_mgr, surfaceid, visible, 9);
        }
    }
}

static void
window_configure(void *data, struct ico_window_mgr *ico_window_mgr,
                 uint32_t surfaceid, const char *appid, int32_t layer,
                 int32_t x, int32_t y, int32_t width, int32_t height, int32_t hint)
{
    struct display *display = data;
    struct surface_name     *p;

    print_log("HOMESCREEN: Event[window_configure] surface=%08x "
              "app=%s x/y=%d/%d w/h=%d/%d hint=%d",
              (int)surfaceid, appid, x, y, width, height, hint);

    p = search_surfaceid(display, (int)surfaceid);
    if (! p)    {
        print_log("HOMESCREEN: Event[window_configure] surface=%08x(app=%s) new create",
                  (int)surfaceid, appid);
        window_created(data, ico_window_mgr, surfaceid, 0, appid);
        p = search_surfaceid(display, (int)surfaceid);
        if (! p)    {
            print_log("HOMESCREEN: Event[window_configure] can not make table");
            return;
        }
    }
}

static void
window_active(void *data, struct ico_window_mgr *ico_window_mgr,
              uint32_t surfaceid, const uint32_t active)
{
    print_log("HOMESCREEN: Event[window_active] surface=%08x acive=%d",
              (int)surfaceid, (int)active);
}

static const struct ico_window_mgr_listener window_mgr_listener = {
    window_created,
    window_destroyed,
    window_visible,
    window_configure,
    window_active
};

static void
cb_input_capabilities(void *data, struct ico_exinput *ico_exinput,
                      const char *device, int32_t type, const char *swname, int32_t input,
                      const char *codename, int32_t code)
{
    print_log("HOMESCREEN: Event[input_capabilities] device=%s type=%d sw=%s input=%d "
              "code=%s[%d]", device, type, swname, input, codename, code);
}

static void
cb_input_code(void *data, struct ico_exinput *ico_exinput,
              const char *device, int32_t input, const char *codename, int32_t code)
{
    print_log("HOMESCREEN: Event[input_code] device=%s input=%d code=%s[%d]",
              device, input, codename, code);
}

static void
cb_input_input(void *data, struct ico_exinput *ico_exinput, uint32_t time,
               const char *device, int32_t input, int32_t code, int32_t state)
{
    print_log("HOMESCREEN: Event[input_input] device=%s input=%d code=%d state=%d",
              device, input, code, state);
}

static const struct ico_exinput_listener exinput_listener = {
    cb_input_capabilities,
    cb_input_code,
    cb_input_input
};

static void
handle_global(void *data, struct wl_registry *registry, uint32_t id,
              const char *interface, uint32_t version)
{
    struct display *display = data;
    struct input *input;
    struct output *output;

    print_log("HOMESCREEN: handle_global: interface=<%s> id=%d", interface, (int)id);

    if (strcmp(interface, "wl_compositor") == 0) {
        display->compositor =
            wl_registry_bind(display->registry, id, &wl_compositor_interface, 1);
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
        wl_output_add_listener(output->output, &output_listener, output);
        display->output = output;

        print_log("HOMESCREEN: created output global %p", display->output);
    }
    else if (strcmp(interface, "wl_shell") == 0)    {
        display->shell =
            wl_registry_bind(display->registry, id, &wl_shell_interface, 1);
    }
    else if (strcmp(interface, "ico_ivi_shell") == 0)   {
        display->ico_ivi_shell =
            wl_registry_bind(display->registry, id, &ico_ivi_shell_interface, 1);
    }
    else if (strcmp(interface, "ico_window_mgr") == 0)  {
        display->ico_window_mgr =
            wl_registry_bind(display->registry, id, &ico_window_mgr_interface, 1);
        ico_window_mgr_add_listener(display->ico_window_mgr, &window_mgr_listener, display);
        print_log("HOMESCREEN: created window_mgr global %p", display->ico_window_mgr);

        ico_window_mgr_set_eventcb(display->ico_window_mgr, 1);
    }
    else if (strcmp(interface, "ico_input_mgr_control") == 0)   {
        display->ico_input_mgr = wl_registry_bind(display->registry, id,
                                                  &ico_input_mgr_control_interface, 1);
        print_log("HOMESCREEN: created input_mgr global %p", display->ico_input_mgr);
    }
    else if (strcmp(interface, "ico_input_mgr_device") == 0)   {
        display->ico_input_device = wl_registry_bind(display->registry, id,
                                                     &ico_input_mgr_device_interface, 1);
        print_log("HOMESCREEN: created input_device global %p", display->ico_input_device);
    }
    else if (strcmp(interface, "ico_exinput") == 0)   {
        display->ico_exinput =
            wl_registry_bind(display->registry, id, &ico_exinput_interface, 1);
        ico_exinput_add_listener(display->ico_exinput, &exinput_listener, display);
        print_log("HOMESCREEN: created exinput global %p", display->ico_exinput);

        ico_window_mgr_set_eventcb(display->ico_window_mgr, 1);

        display->bg_created = 1;
        create_surface(display);
    }
}

static const struct wl_registry_listener registry_listener = {
    handle_global
};

static char *
skip_spaces(char *buf)
{
    while ((*buf == ' ') || (*buf == '\t')) {
        buf++;
    }
    return(buf);
}

static int
pars_command(char *buf, char *pt[], const int len)
{
    char    *p;
    int     narg;

    memset(pt, 0, sizeof(int *)*10);
    p = buf;
    for (narg = 0; narg < len; narg++)  {
        p = skip_spaces(p);
        if (*p == 0)    break;
        pt[narg] = p;
        for (; *p; p++) {
            if ((*p == ' ') || (*p == '\t') ||
                (*p == '=') || (*p == ',')) break;
        }
        if (*p == 0)    {
            narg++;
            break;
        }
        *p = 0;
        p++;
    }
    return (narg);
}

static void
launch_app(struct display *display, char *buf)
{
    char    sbuf[256];

    display->surface_created = 0;
    display->surface_destroyed = 0;
    snprintf(sbuf, sizeof(sbuf)-1, "%s &", skip_spaces(buf));
    if (system(sbuf) < 0)   {
        print_log("HOMESCREEN: Can not launch application[%s]", sbuf);
    }
    else    {
        sleep_with_wayland(display->display, 500);
    }
}

static void
kill_app(struct display *display, char *buf)
{
    char    *args[10];
    int     narg;
    struct surface_name     *p;
    struct surface_name     *fp;

    narg = pars_command(buf, args, 10);
    if (narg >= 1)  {
        p = search_surfacename(display, args[0]);
        if (kill(p->pid, SIGINT) < 0)   {
            print_log("HOMESCREEN: kill[%s.%d] Application dose not exist",
                      p->appid, p->pid);
        }
        else    {
            sleep_with_wayland(display->display, 300);
            p = search_surfacename(display, args[0]);
            if ((p != NULL) && (kill(p->pid, SIGTERM) >= 0))    {
                sleep_with_wayland(display->display, 200);
                p = search_surfacename(display, args[0]);
                if (p)  {
                    kill(p->pid, SIGKILL);
                    sleep_with_wayland(display->display, 200);
                }
            }
        }
        p = search_surfacename(display, args[0]);
        if (p)  {
            if (p == display->surface_name) {
                display->surface_name = p->next;
            }
            else    {
                fp = display->surface_name;
                while (fp)  {
                    if (fp->next == p)  {
                        fp->next = p->next;
                        break;
                    }
                }
            }
            free(p);
        }
    }
    else    {
        print_log("HOMESCREEN: kill command[kill appid] has no argument");
    }
}

static void
layer_surface(struct display *display, char *buf)
{
    char    *args[10];
    int     narg;
    int     surfaceid;
    int     layerid;

    narg = pars_command(buf, args, 10);
    if (narg >= 2)  {
        surfaceid = search_surface(display, args[0]);
        layerid = strtol(args[1], (char **)0, 0);
        if ((surfaceid >= 0) && (layerid >= 0)) {
            print_log("HOMESCREEN: set_window_layer(%s,%08x)",
                      args[0], surfaceid, layerid);
            ico_window_mgr_set_window_layer(display->ico_window_mgr, surfaceid, layerid);
        }
        else    {
            print_log("HOMESCREEN: Unknown surface(%s) at layer command", args[0]);
        }
    }
    else    {
        print_log("HOMESCREEN: layer command[layer appid layerid] has no argument");
    }
}

static void
positionsize_surface(struct display *display, char *buf)
{
    char    *args[10];
    int     narg;
    int     surfaceid;
    int     x, y, width, height;

    narg = pars_command(buf, args, 10);
    if (narg >= 5)  {
        surfaceid = search_surface(display, args[0]);
        x = strtol(args[1], (char **)0, 0);
        y = strtol(args[2], (char **)0, 0);
        width = strtol(args[3], (char **)0, 0);
        height = strtol(args[4], (char **)0, 0);
        if ((surfaceid >= 0) && (x >= 0) && (y >=0) && (width >= 0) && (height >=0))    {
            print_log("HOMESCREEN: set_positionsize(%s,%08x,%d,%d,%d,%d)",
                      args[0], surfaceid, x, y, width, height);
            ico_window_mgr_set_positionsize(display->ico_window_mgr, surfaceid,
                                            x, y, width, height);
        }
        else    {
            print_log("HOMESCREEN: Unknown surface(%s) at positionsize command", args[0]);
        }
    }
    else    {
        print_log("HOMESCREEN: positionsize command"
                  "[positionsize appid x y width heigh] has no argument");
    }
}

static void
move_surface(struct display *display, char *buf)
{
    char    *args[10];
    int     narg;
    int     surfaceid;
    int     x, y;

    narg = pars_command(buf, args, 10);
    if (narg >= 3)  {
        surfaceid = search_surface(display, args[0]);
        x = strtol(args[1], (char **)0, 0);
        y = strtol(args[2], (char **)0, 0);
        if ((surfaceid >= 0) && (x >= 0) && (y >=0))    {
            print_log("HOMESCREEN: move(%s,%08x,%d,%d)", args[0], surfaceid, x, y);
            ico_window_mgr_set_positionsize(display->ico_window_mgr, surfaceid,
                                            x, y,
                                            ICO_IVI_MAX_COORDINATE+1,
                                            ICO_IVI_MAX_COORDINATE+1);
        }
        else    {
            print_log("HOMESCREEN: Unknown surface(%s) at move command", args[0]);
        }
    }
    else    {
        print_log("HOMESCREEN: move command[positionsize appid x y] has no argument");
    }
}

static void
resize_surface(struct display *display, char *buf)
{
    char    *args[10];
    int     narg;
    int     surfaceid;
    int     width, height;

    narg = pars_command(buf, args, 10);
    if (narg >= 3)  {
        surfaceid = search_surface(display, args[0]);
        width = strtol(args[1], (char **)0, 0);
        height = strtol(args[2], (char **)0, 0);
        if ((surfaceid >= 0) && (width >= 0) && (height >=0))   {
            print_log("HOMESCREEN: resize(%s,%08x,%d,%d)",
                      args[0], surfaceid, width, height);
            ico_window_mgr_set_positionsize(display->ico_window_mgr, surfaceid,
                                            ICO_IVI_MAX_COORDINATE+1,
                                            ICO_IVI_MAX_COORDINATE+1, width, height);
        }
        else    {
            print_log("HOMESCREEN: Unknown surface(%s) at resize command", args[0]);
        }
    }
    else    {
        print_log("HOMESCREEN: positionsize command"
                  "[resize appid width heigh] has no argument");
    }
}

static void
visible_surface(struct display *display, char *buf)
{
    char    *args[10];
    int     narg;
    int     surfaceid;
    int     visible;
    int     raise;

    narg = pars_command(buf, args, 10);
    if (narg >= 3)  {
        surfaceid = search_surface(display, args[0]);
        visible = strtol(args[1], (char **)0, 0);
        raise = strtol(args[2], (char **)0, 0);
        if ((surfaceid >= 0) && (visible >= 0) && (raise >=0))  {
            print_log("HOMESCREEN: visible(%s,%08x,%d,%d)",
                      args[0], surfaceid, visible, raise);
            ico_window_mgr_set_visible(display->ico_window_mgr, surfaceid, visible, raise);
        }
        else    {
            print_log("HOMESCREEN: Unknown surface(%s) at visible command", args[0]);
        }
    }
    else    {
        print_log("HOMESCREEN: visible command[visible appid visible raise] "
                  "has no argument");
    }
}

static void
show_surface(struct display *display, char *buf, const int show)
{
    char    *args[10];
    int     narg;
    int     surfaceid;

    narg = pars_command(buf, args, 10);
    if (narg >= 1)  {
        surfaceid = search_surface(display, args[0]);
        if (surfaceid >= 0) {
            if (show)   {
                print_log("HOMESCREEN: show(%s,%08x)", args[0], surfaceid);
                ico_window_mgr_set_visible(display->ico_window_mgr, surfaceid, 1, 9);
            }
            else    {
                print_log("HOMESCREEN: hide(%s,%08x)", args[0], surfaceid);
                ico_window_mgr_set_visible(display->ico_window_mgr, surfaceid, 0, 9);
            }
        }
        else    {
            print_log("HOMESCREEN: Unknown surface(%s) at show/hide command", args[0]);
        }
    }
    else    {
        print_log("HOMESCREEN: show command[show/hide appid] has no argument");
    }
}

static void
raise_surface(struct display *display, char *buf, const int raise)
{
    char    *args[10];
    int     narg;
    int     surfaceid;

    narg = pars_command(buf, args, 10);
    if (narg >= 1)  {
        surfaceid = search_surface(display, args[0]);
        if (surfaceid >= 0) {
            if (raise)  {
                print_log("HOMESCREEN: raise(%s,%08x)", args[0], surfaceid);
                ico_window_mgr_set_visible(display->ico_window_mgr, surfaceid, 9, 1);
            }
            else    {
                print_log("HOMESCREEN: lower(%s,%08x)", args[0], surfaceid);
                ico_window_mgr_set_visible(display->ico_window_mgr, surfaceid, 9, 0);
            }
        }
        else    {
            print_log("HOMESCREEN: Unknown surface(%s) at raise/lower command", args[0]);
        }
    }
    else    {
        print_log("HOMESCREEN: show command[raise/lower appid] has no argument");
    }
}

static void
animation_surface(struct display *display, char *buf)
{
    char    *args[10];
    int     narg;
    int     surfaceid;

    narg = pars_command(buf, args, 10);
    if (narg >= 2)  {
        surfaceid = search_surface(display, args[0]);
        if (surfaceid >= 0) {
            print_log("HOMESCREEN: animation(%s,%08x,%d)", args[0], surfaceid, args[1]);
<<<<<<< HEAD
            ico_window_mgr_set_animation(display->ico_window_mgr, surfaceid, args[1]);
=======
            ico_window_mgr_set_animation(display->ico_window_mgr, surfaceid,
										 ICO_WINDOW_MGR_ANIMATION_CHANGE_VISIBLE, args[1]);
>>>>>>> master
        }
        else    {
            print_log("HOMESCREEN: Unknown surface(%s) at animation command", args[0]);
        }
    }
    else    {
        print_log("HOMESCREEN: animation command"
                  "[animation appid animation] has no argument");
    }
}

static void
visible_layer(struct display *display, char *buf)
{
    char    *args[10];
    int     narg;
    int     layer;
    int     visible;

    narg = pars_command(buf, args, 10);
    if (narg >= 2)  {
        layer = strtol(args[0], (char **)0, 0);
        visible = strtol(args[1], (char **)0, 0);
        ico_window_mgr_set_layer_visible(display->ico_window_mgr, layer, visible);
    }
    else    {
        print_log("HOMESCREEN: layer_visible command"
                  "[layer_visible layer visible] has no argument");
    }
}

static void
input_add(struct display *display, char *buf)
{
    char    *args[10];
    int     narg;
    int     input;
    int     fix;

    narg = pars_command(buf, args, 10);
    if (narg >= 3)  {
        input = strtol(args[1], (char **)0, 0);
        if (narg >= 4)  {
            fix = strtol(args[3], (char **)0, 0);
        }
        else    {
            fix = 0;
        }
        if ((input >= 0) && (fix >=0))  {
            print_log("HOMESCREEN: input_add(%s.%d to %s[%d])",
                      args[0], input, args[2], fix);
            ico_input_mgr_control_add_input_app(display->ico_input_mgr,
                                                args[2], args[0], input, fix);
        }
        else    {
            print_log("HOMESCREEN: Unknown input(%s) at input_add command", args[1]);
        }
    }
    else    {
        print_log("HOMESCREEN: input_add command[input_add device inputId appid fix] "
                  "has no argument");
    }
}

static void
input_del(struct display *display, char *buf)
{
    char    *args[10];
    int     narg;
    int     input;
    char    wk1[32], wk2[32];

    narg = pars_command(buf, args, 10);
    if (narg >= 3)  {
        input = strtol(args[1], (char **)0, 0);
        if (args[0][0] == '@')  {
            wk1[0] = 0;
            args[0] = wk1;
        }
        if (args[2][0] == '@')  {
            wk2[0] = 0;
            args[2] = wk2;
        }
        print_log("HOMESCREEN: input_del(%s.%d to %s)", args[0], input, args[2]);
        ico_input_mgr_control_del_input_app(display->ico_input_mgr,
                                            args[2], args[0], input);
    }
    else    {
        print_log("HOMESCREEN: input_del command[input_del device inputId appid] "
                  "has no argument");
    }
}

static void
input_conf(struct display *display, char *buf)
{
    char    *args[10];
    int     narg;
    int     type;
    int     input;
    int     code;
    char    wk1[32], wk2[32];

    narg = pars_command(buf, args, 10);
    if (narg >= 4)  {
        type = strtol(args[1], (char **)0, 0);
        input = strtol(args[3], (char **)0, 0);
        if (narg >= 6)  {
            code = strtol(args[5], (char **)0, 0);
        }
        else    {
            code = 0;
            args[4] = wk1;
            strcpy(wk1, args[2]);
            args[5] = wk2;
            strcpy(wk2, "0");
        }
        if ((type >= 0) && (input >= 0) && (code >=0))  {
            ico_input_mgr_device_configure_input(display->ico_input_device, args[0], type,
                                                 args[2], input, args[4], code);
        }
        else    {
            print_log("HOMESCREEN: Unknown type(%s),input(%s) or code(%s) "
                      "at input_conf command", args[1], args[3], args[5]);
        }
    }
    else    {
        print_log("HOMESCREEN: input_conf command[input_conf device type swname input "
                  "codename code] has no argument");
    }
}

static void
input_code(struct display *display, char *buf)
{
    char    *args[10];
    int     narg;
    int     input;
    int     code;

    narg = pars_command(buf, args, 10);
    if (narg >= 4)  {
        input = strtol(args[1], (char **)0, 0);
        code = strtol(args[3], (char **)0, 0);
        if ((input >= 0) && (code >= 0))    {
            ico_input_mgr_device_configure_code(display->ico_input_device, args[0], input,
                                                args[2], code);
        }
        else    {
            print_log("HOMESCREEN: Unknown input(%s) or code(%s) "
                      "at input_code command", args[1], args[3]);
        }
    }
    else    {
        print_log("HOMESCREEN: input_conf command[input_code device input codename code] "
                  "has no argument");
    }
}

static void
input_sw(struct display *display, char *buf)
{
    char    *args[10];
    int     narg;
    int     timems;
    int     input;
    int     code;
    int     state;
    struct timeval  stv;

    narg = pars_command(buf, args, 10);
    if (narg >= 4)  {
        input = strtol(args[1], (char **)0, 0);
        code = strtol(args[2], (char **)0, 0);
        state = strtol(args[3], (char **)0, 0);
        if ((input >= 0) && (state >= 0))   {
            gettimeofday(&stv, (struct timezone *)NULL);
            timems = (stv.tv_sec % 1000) * 1000 + (stv.tv_usec / 1000);
            ico_input_mgr_device_input_event(display->ico_input_device,
                                             timems, args[0], input, code, state);
        }
        else    {
            print_log("HOMESCREEN: Unknown input(%s),code(%s) or state(%s) "
                      "at input_sw command", args[1], args[2], args[3]);
        }
    }
    else    {
        print_log("HOMESCREEN: input_sw command[input_sw device input code, state] "
                  "has no argument");
    }
}

static void
send_event(const char *cmd)
{
    static int  nmqinfo = 0;
    static struct   {
        int     mqkey;
        int     mqid;
    }           mqinfo[10];
    int     mqkey;
    int     mqid;
    struct {
        long    mtype;
        char    buf[240];
    }       mqbuf;
    int     pt, i;

    if (cmd == NULL)    {
        return;
    }
    mqkey = 0;
    for (pt = 0; cmd[pt]; pt++) {
        if ((cmd[pt] >= '0') && (cmd[pt] <= '9'))   {
            mqkey = mqkey * 10 + cmd[pt] - '0';
        }
        else    {
            break;
        }
    }
    for (; cmd[pt] == ' '; pt++)    ;

    if (mqkey <= 0) {
        mqkey = 5551;
        pt = 0;
    }
    for (i = 0; i < nmqinfo; i++)   {
        if (mqinfo[i].mqkey == mqkey)   {
            mqid = mqinfo[i].mqid;
            break;
        }
    }
    if (i >= nmqinfo)   {
        if (nmqinfo >= 10)  {
            fprintf(stderr, "HOMESCREEN: message queue(%d) overflow\n", mqkey);
            return;
        }
        mqid = msgget(mqkey, 0);
        if (mqid < 0)   {
            fprintf(stderr, "HOMESCREEN: message queue(%d(0x%x)) get error[%d]\n",
                    mqkey, mqkey, errno);
            return;
        }
        mqinfo[nmqinfo].mqkey = mqkey;
        mqinfo[nmqinfo].mqid = mqid;
        nmqinfo ++;
    }

    memset(&mqbuf, 0, sizeof(mqbuf));
    mqbuf.mtype = 1;
    strncpy(mqbuf.buf, &cmd[pt], sizeof(mqbuf)-sizeof(long));

    if (msgsnd(mqid, &mqbuf, sizeof(mqbuf)-sizeof(long), 0) < 0)    {
        fprintf(stderr, "HOMESCREEN: message queue(%d(0x%x)) send error[%d]\n",
                mqkey, mqkey, errno);
        return;
    }
}

/*
 * Main Program
 *
 *   usage:
 *     test-homescreen < test-case-data-file > test-result-output
 */
int main(int argc, char *argv[])
{
    struct display *display;
    char buf[256];
    int ret, fd;
    int msec;

    display = malloc(sizeof *display);
    assert(display);
    memset((char *)display, 0, sizeof *display);

    display->init_width = 640;
    display->init_height = 480;
    display->init_color = 0xFF304010;

    for (fd = 1; fd < argc; fd++)   {
        if (argv[fd][0] == '-') {
            if (strncasecmp(argv[fd], "-visible=", 9) == 0) {
                display->visible_on_create = argv[fd][9] & 1;
            }
            else if (strncasecmp(argv[fd], "-display=", 9) == 0)    {
                strncpy(display->connect, &argv[fd][9], MAX_CON_NAME);
            }
            else if (strncasecmp(argv[fd], "-prompt=", 8) == 0) {
                display->prompt = argv[fd][8] & 1;
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

    fd = 0;

    while (1) {
        sleep_with_wayland(display->display, 20);
        if (display->prompt)    {
            printf("HOMESCREEN: "); fflush(stdout);
        }
        ret = getdata(display->ico_window_mgr, "HOMESCREEN: ", fd, buf, sizeof(buf));
        if (ret < 0) {
            fprintf(stderr, "HOMESCREEN: read error: fd %d, %m\n", fd);
            return -1;
        }
        if (ret == 0)   continue;
        wl_display_flush(display->display);

        if ((strncasecmp(buf, "bye", 3) == 0) ||
            (strncasecmp(buf, "quit", 4) == 0) ||
            (strncasecmp(buf, "end", 3) == 0))  {
            /* Exit, end of test            */
            return 0;
        }
        else if (strncasecmp(buf, "launch", 6) == 0) {
            /* Launch test application      */
            launch_app(display, &buf[6]);
        }
        else if (strncasecmp(buf, "kill", 4) == 0) {
            /* Launch test application      */
            kill_app(display, &buf[4]);
        }
        else if (strncasecmp(buf, "layer_visible", 13) == 0) {
            /* Change layer visiblety       */
            visible_layer(display, &buf[13]);
        }
        else if (strncasecmp(buf, "layer", 5) == 0) {
            /* layer change surface window  */
            layer_surface(display, &buf[5]);
        }
        else if (strncasecmp(buf, "positionsize", 12) == 0) {
            /* Move and Ressize surface window*/
            positionsize_surface(display, &buf[12]);
        }
        else if (strncasecmp(buf, "move", 4) == 0) {
            /* Move surface window          */
            move_surface(display, &buf[4]);
        }
        else if (strncasecmp(buf, "resize", 6) == 0) {
            /* Resize surface window        */
            resize_surface(display, &buf[6]);
        }
        else if (strncasecmp(buf, "visible", 7) == 0) {
            /* Visible and Raise surface window*/
            visible_surface(display, &buf[7]);
        }
        else if (strncasecmp(buf, "show", 4) == 0) {
            /* Show/Hide surface window     */
            show_surface(display, &buf[4], 1);
        }
        else if (strncasecmp(buf, "hide", 4) == 0) {
            /* Show/Hide surface window     */
            show_surface(display, &buf[4], 0);
        }
        else if (strncasecmp(buf, "raise", 5) == 0) {
            /* Raise/Lower surface window   */
            raise_surface(display, &buf[5], 1);
        }
        else if (strncasecmp(buf, "lower", 5) == 0) {
            /* Raise/Lower surface window   */
            raise_surface(display, &buf[5], 0);
        }
        else if (strncasecmp(buf, "animation", 9) == 0) {
            /* Set animation surface window*/
            animation_surface(display, &buf[9]);
        }
        else if (strncasecmp(buf, "input_add", 9) == 0) {
            /* Set input switch to application */
            input_add(display, &buf[9]);
        }
        else if (strncasecmp(buf, "input_del", 9) == 0) {
            /* Reset input switch to application*/
            input_del(display, &buf[9]);
        }
        else if (strncasecmp(buf, "input_conf", 10) == 0) {
            /* input switch configuration       */
            input_conf(display, &buf[10]);
        }
        else if (strncasecmp(buf, "input_code", 10) == 0) {
            /* input code configuration         */
            input_code(display, &buf[10]);
        }
        else if (strncasecmp(buf, "input_sw", 8) == 0) {
            /* input switch event               */
            input_sw(display, &buf[8]);
        }
        else if (strncasecmp(buf, "sleep", 5) == 0) {
            /* Sleep                            */
            msec = sec_str_2_value(&buf[6]);
            sleep_with_wayland(display->display, msec);
        }
        else if (strncasecmp(buf, "waitcreate", 10) == 0) {
            /* Wait surface create              */
            msec = sec_str_2_value(&buf[11]);
            wait_with_wayland(display->display, msec, &display->surface_created);
        }
        else if (strncasecmp(buf, "waitdestroy", 11) == 0) {
            /* Wait surface destrpy             */
            msec = sec_str_2_value(&buf[12]);
            wait_with_wayland(display->display, msec, &display->surface_destroyed);
        }
        else if (strncasecmp(buf, "event", 5) == 0) {
            /* Send touch panel event to Weston */
            send_event(&buf[6]);
        }
        else {
            print_log("HOMESCREEN: unknown command[%s]", buf);
            return -1;
        }
    }

    print_log("HOMESCREEN: end");

    send_event(NULL);

    return(0);
}

