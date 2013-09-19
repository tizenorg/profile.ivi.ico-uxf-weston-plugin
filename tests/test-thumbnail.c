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
 * @brief   Uint test thumbnail routines
 *
 * @date    Sep-05-2013
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "test-common.h"

/*--------------------------------------------------------------------------*/
/**
 * @brief   opengl_thumbnail: get application buffer thumbnail
 *
 * @param[in]   display     Wayland display
 * @param[in]   surfaceid   UIFW surface Id
 * @param[in]   dpy         EGL display
 * @param[in]   ctx         EGL context
 * @param[in]   target      EGL buffer name(Id)
 * @param[in]   width       Widown width
 * @param[in]   height      Window height
 * @param[in]   stride      Window stride
 * @param[in]   format      Buffer format
 * @return      nothing
 */
/*--------------------------------------------------------------------------*/
void
opengl_thumbnail(struct wl_display *display, uint32_t surfaceid, EGLDisplay dpy,
                 EGLConfig conf, EGLSurface egl_surface, EGLContext ctx,
                 uint32_t target, int width, int height, int stride, uint32_t format)
{
    print_log("opengl_thumbnail: Not support.");
}

