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
#include <wayland-server.h>
#include <EGL/eglext.h>

/* wl_buffer (inport from wayland-1.2.0/src/wayland-server.h)                       */
struct uifw_wl_buffer   {
    struct wl_resource resource;
    int32_t width, height;
    uint32_t busy_count;
};

/* wl_drm (inport from mesa-9.1.3/src/egl/wayland/wayland-drm/wayland-drm.c)        */
struct uifw_drm {
    struct wl_display *display;
    void *user_data;
    char *device_name;
    void *callbacks;
};

/* wl_drm_buffer (inport from mesa-9.1.3/src/egl/wayland/wayland-drm/wayland-drm.h) */
struct uifw_drm_buffer {
    struct uifw_wl_buffer buffer;
    struct uifw_drm *drm;                  /* struct wl_drm    */
    uint32_t format;
    const void *driver_format;
    int32_t offset[3];
    int32_t stride[3];
    void *driver_buffer;
};

/* buffer management table          */
struct  _egl_buffer {
    struct  _egl_buffer *next;
    uint32_t    surfaceid;
    uint32_t    target;
    struct uifw_drm_buffer  buffer;
};

#if 0           /* not yet support  */
static struct _egl_buffer   *egl_buffer = NULL;
static struct uifw_drm      drm = { NULL };

/* EGL functions                    */
static PFNEGLQUERYWAYLANDBUFFERWL   query_buffer = NULL;
static PFNEGLCREATEIMAGEKHRPROC     create_image = NULL;
static PFNEGLDESTROYIMAGEKHRPROC    destroy_image = NULL;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC  image_target_texture_2d = NULL;
#endif

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
                 EGLContext ctx, uint32_t target, int width, int height,
                 int stride, uint32_t format)
{
#if 0           /* not yet support  */
    struct _egl_buffer  *buffer;            /* EGL buffer management table  */
    GLuint      texture;                    /* texture                      */
    EGLImageKHR image;                      /* image                        */
    EGLint      attribs[3];                 /* attributes                   */

    /* search created buffers               */
    if (! egl_buffer)   {
        memset(&drm, 0, sizeof(drm));
        drm.display = wl_display;
        drm.device_name = "intel";
    }
    buffer = egl_buffer;
    while(buffer)   {
        if ((buffer->surfaceid == surfaceid) &&
            (buffer->target == target)) break;
    }
    if (! buffer)   {
        /* create new buffer                */
        buffer = malloc(sizeof(struct _egl_buffer));
        if (! buffer)   {
            print_log("opengl_thumbnail: ERROR can not alloc buffer table");
            return;
        }
        memset(buffer, 0, sizeof(struct _egl_buffer));
        buffer->next = egl_buffer;
        egl_buffer = buffer;

        buffer->buffer.width = width;
        buffer->buffer.height = height;
        buffer->drm = &drm;
        buffer->format = format;
    }

    if (! query_buffer) {
        query_buffer = (void *) eglGetProcAddress("eglQueryWaylandBufferWL");
        create_image = (void *) eglGetProcAddress("eglCreateImageKHR");
        destory_image = (void *) eglGetProcAddress("eglDestroyImageKHR");
        image_target_texture_2d = (void *) eglGetProcAddress("glEGLImageTargetTexture2DOES");

        if ((! query_buffer) || (! create_image) ||
            (! destory_image) || (! image_target_texture_2d))   {
            print_log("opengl_thumbnail: ERROR can not get EGL extension cwfunctions");
            query_buffer = NULL;
            return;
        }
    }

    /* get image from EGL buffer    */
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    attribs[0] = EGL_WAYLAND_PLANE_WL;
    attribs[1] = 0;
    attribs[2] = EGL_NONE;
    image = create_image(dpy, NULL, EGL_WAYLAND_BUFFER_WL, egl_buffers[idx].buffer, attribs);
    if (! image)    {
        print_log("opengl_thumbnail: ERROR can not create image");
        return;
    }

    /* create texture from image    */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    image_target_texture_2d(GL_TEXTURE_2D, image);

    /* texture to surface           */
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
#endif
}

