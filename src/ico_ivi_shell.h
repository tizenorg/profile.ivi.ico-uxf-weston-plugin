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

#ifndef _ICO_IVI_SHELL_H_
#define _ICO_IVI_SHELL_H_

struct shell_surface;

/* Prototype for get/set function       */
struct weston_layer *ico_ivi_shell_current_layer(void);
void ico_ivi_shell_set_toplevel(struct shell_surface *shsurf);
void ico_ivi_shell_set_surface_type(struct shell_surface *shsurf);
void ico_ivi_shell_send_configure(struct weston_surface *surface,
                                  const uint32_t edges, const int width, const int height);
void ico_ivi_shell_startup(void *shell);

/* Prototypr for hook routine           */
void ico_ivi_shell_hook_bind(void (*hook_bind)(struct wl_client *client, void *shell));
void ico_ivi_shell_hook_unbind(void (*hook_unbind)(struct wl_client *client));
void ico_ivi_shell_hook_create(void (*hook_create)(struct wl_client *client,
                            struct wl_resource *resource, struct weston_surface *surface,
                            struct shell_surface *shsurf));
void ico_ivi_shell_hook_destroy(void (*hook_destroy)(struct weston_surface *surface));
void ico_ivi_shell_hook_map(void (*hook_map)(struct weston_surface *surface,
                            int32_t *width, int32_t *height, int32_t *sx, int32_t *sy));
void ico_ivi_shell_hook_configure(void (*hook_configure)(struct weston_surface *surface));
void ico_ivi_shell_hook_select(void (*hook_select)(struct weston_surface *surface));
void ico_ivi_shell_hook_title(void (*hook_title)(struct weston_surface *surface,
                            const char *title));
void ico_ivi_shell_hook_move(void (*hook_move)(struct weston_surface *surface,
                            int *dx, int *dy));

#endif  /*_ICO_IVI_SHELL_H_*/

