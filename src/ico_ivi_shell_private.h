/*
 * Copyright © 2010-2011 Intel Corporation
 * Copyright © 2008-2011 Kristian Høgsberg
 * Copyright © 2013 TOYOTA MOTOR CORPORATION.
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/**
 * @brief   Public functions in ico_ivi_shell Weston plugin
 *
 * @date    Jul-26-2013
 */

#ifndef _ICO_IVI_SHELL_PRIVATE_H_
#define _ICO_IVI_SHELL_PRIVATE_H_

#include "ico_window_mgr-server-protocol.h"

struct shell_surface;

/* surface type                         */
enum shell_surface_type {
    SHELL_SURFACE_NONE,
    SHELL_SURFACE_TOPLEVEL,
    SHELL_SURFACE_TRANSIENT,
    SHELL_SURFACE_FULLSCREEN,
    SHELL_SURFACE_MAXIMIZED,
    SHELL_SURFACE_POPUP,
    SHELL_SURFACE_XWAYLAND
};

/* weston layer type            */
#define LAYER_TYPE_UNKNOWN      0
#define LAYER_TYPE_BACKGROUND   (ICO_WINDOW_MGR_LAYERTYPE_BACKGROUND >> 12)
#define LAYER_TYPE_PANEL        (ICO_WINDOW_MGR_LAYERTYPE_NORMAL >> 12)
#define LAYER_TYPE_FULLSCREEN   (ICO_WINDOW_MGR_LAYERTYPE_FULLSCREEN >> 12)
#define LAYER_TYPE_INPUTPANEL   (ICO_WINDOW_MGR_LAYERTYPE_INPUTPANEL >> 12)
#define LAYER_TYPE_TOUCH        (ICO_WINDOW_MGR_LAYERTYPE_TOUCH >> 12)
#define LAYER_TYPE_CURSOR       (ICO_WINDOW_MGR_LAYERTYPE_CURSOR >> 12)
#define LAYER_TYPE_LOCK         0xe
#define LAYER_TYPE_FADE         0xf

/* fullscreen surface control   */
enum shell_fullscreen_control   {
    SHELL_FULLSCREEN_ISTOP,
    SHELL_FULLSCREEN_SET,
    SHELL_FULLSCREEN_STACK,
    SHELL_FULLSCREEN_CONF,
    SHELL_FULLSCREEN_HIDEALL
};

/* Prototype for get/set function       */
struct weston_layer *ico_ivi_shell_weston_layer(void);
void ico_ivi_shell_set_toplevel(struct shell_surface *shsurf);
int ico_ivi_shell_get_surfacetype(struct shell_surface *shsurf);
void ico_ivi_shell_set_surface_type(struct shell_surface *shsurf);
void ico_ivi_shell_send_configure(struct weston_surface *surface,
                                  const uint32_t edges, const int width, const int height);
void ico_ivi_shell_startup(void *shell);
int ico_ivi_shell_layertype(struct weston_surface *surface);
void ivi_shell_set_surface_initial_position(struct weston_surface *surface);
void ivi_shell_set_default_display(struct weston_output *inputpanel);

/* Prototypr for hook routine           */
void ico_ivi_shell_hook_bind(void (*hook_bind)(struct wl_client *client, void *shell));
void ico_ivi_shell_hook_unbind(void (*hook_unbind)(struct wl_client *client));
void ico_ivi_shell_hook_create(void (*hook_create)(int layertype, struct wl_client *client,
                            struct wl_resource *resource, struct weston_surface *surface,
                            struct shell_surface *shsurf));
void ico_ivi_shell_hook_destroy(void (*hook_destroy)(struct weston_surface *surface));
void ico_ivi_shell_hook_map(void (*hook_map)(struct weston_surface *surface,
                            int32_t *width, int32_t *height, int32_t *sx, int32_t *sy));
void ico_ivi_shell_hook_configure(void (*hook_configure)(struct weston_surface *surface));
void ico_ivi_shell_hook_select(void (*hook_select)(struct weston_surface *surface));
void ico_ivi_shell_hook_title(char *(*hook_title)(struct weston_surface *surface,
                            const char *title));
void ico_ivi_shell_hook_move(void (*hook_move)(struct weston_surface *surface,
                            int *dx, int *dy));
void ico_ivi_shell_hook_show_layer(void (*hook_show)(int layertype, int show, void *data));
void ico_ivi_shell_hook_fullscreen(int (*hook_fullscreen)
                            (int event, struct weston_surface *surface));

#endif  /*_ICO_IVI_SHELL_PRIVATE_H_*/
