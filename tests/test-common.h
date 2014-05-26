/*
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
 * @brief   Header file for uint test common routines
 *
 * @date    Feb-08-2013
 */

#ifndef _TEST_COMMON_H_
#define _TEST_COMMON_H_

#include    <EGL/egl.h>                 /* EGL                              */
#include    <GLES2/gl2.h>               /* OpenGL ES 2.x                    */
#include    <wayland-client.h>          /* Wayland client library           */
#include    <wayland-egl.h>             /* Wayland EGL library              */
#include    <wayland-util.h>            /* Wayland Misc library             */

/* Function prototype           */
int getdata(void *window_mgr, const char *prompt, int fd, char *buf, const int size);
void print_log(const char *fmt, ...);
char *skip_spaces(char *buf);
int pars_command(char *buf, char *pt[], const int len);
void wayland_dispatch_nonblock(struct wl_display *display);
void sleep_with_wayland(struct wl_display *display, int msec);
void wait_with_wayland(struct wl_display *display, int msec, int *endflag);
int sec_str_2_value(const char *ssec);
EGLDisplay opengl_init(struct wl_display *display, EGLConfig *rconf, EGLContext *rctx);
EGLSurface opengl_create_window(struct wl_display *display, struct wl_surface *surface,
                                EGLDisplay dpy, EGLConfig conf, EGLContext ctx,
                                const int width, const int height, const int color);
void opengl_clear_window(const unsigned int color);
void opengl_swap_buffer(struct wl_display *display, EGLDisplay dpy, EGLSurface egl_surface);
void opengl_thumbnail(struct wl_display *display, uint32_t surfaceid, EGLDisplay dpy,
                      EGLConfig conf, EGLSurface egl_surface, EGLContext ctx, uint32_t name,
                      int width, int height, int stride, uint32_t format);

#endif /*_TEST_COMMON_H_*/

