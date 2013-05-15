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
 * @date    Feb-08-2013
 */

#ifndef _ICO_IVI_SHELL_H_
#define _ICO_IVI_SHELL_H_

struct shell_surface;

/* option flag                          */
#define ICO_OPTION_FLAG_UNVISIBLE   0x00000001      /* unvisible control    */

/* Prototype for set function           */
void ivi_shell_set_layer(struct shell_surface *shsurf, const int layer);
bool ivi_shell_is_visible(struct shell_surface *shsurf);
void ivi_shell_set_visible(struct shell_surface *shsurf, const int visible);
void ivi_shell_set_raise(struct shell_surface *shsurf, const int raise);
void ivi_shell_set_toplevel(struct shell_surface *shsurf);
void ivi_shell_set_surface_type(struct shell_surface *shsurf);
void ivi_shell_send_configure(struct shell_surface *shsurf, const int id,
                              const int edges, const int width, const int height);
void ivi_shell_set_positionsize(struct shell_surface *shsurf, const int x,
                                const int y, const int width, const int height);
void ivi_shell_set_layer_visible(const int layer, const int visible);
void ivi_shell_surface_configure(struct shell_surface *shsurf, const int x,
                                 const int y, const int width, const int height);
void ivi_shell_set_active(struct shell_surface *shsurf, const int target);

/* Prototypr for hook routine           */
void ivi_shell_hook_bind(void (*hook_bind)(struct wl_client *client));
void ivi_shell_hook_unbind(void (*hook_unbind)(struct wl_client *client));
void ivi_shell_hook_create(void (*hook_create)(struct wl_client *client,
                            struct wl_resource *resource, struct weston_surface *surface,
                            struct shell_surface *shsurf));
void ivi_shell_hook_destroy(void (*hook_destroy)(struct weston_surface *surface));
void ivi_shell_hook_map(void (*hook_map)(struct weston_surface *surface,
                            int32_t *width, int32_t *height, int32_t *sx, int32_t *sy));
void ivi_shell_hook_change(void (*hook_change)(struct weston_surface *surface,
                            const int to, const int manager));
void ivi_shell_hook_select(void (*hook_select)(struct weston_surface *surface));

/* Prototype for hook of Multi Input Manager    */
void ico_win_mgr_hook_set_user(void (*hook_set_user)(struct wl_client *client,
                                                    const char *appid));
void ico_win_mgr_hook_create(void (*hook_create)(struct wl_client *client,
                                                 struct weston_surface *surface,
                                                 int surfaceId,
                                                 const char *appid));
void ico_win_mgr_hook_destroy(void (*hook_destroy)(struct weston_surface *surface));

#endif  /*_ICO_IVI_SHELL_H_*/
