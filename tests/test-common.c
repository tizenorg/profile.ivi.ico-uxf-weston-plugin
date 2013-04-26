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
 * @brief   Uint test common routines
 *
 * @date    Feb-08-2013
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <errno.h>
#include <wayland-client.h>
#include "ico_window_mgr-client-protocol.h"
#include "test-common.h"

/*--------------------------------------------------------------------------*/
/**
 * @brief   getdata: Input data string
 *
 * @param[in]   window_mgr  ico_window_mgr, if Null, output to stderr
 * @param[in]   prompt      Echoback printout header
 * @param[in]   fd          Input file discriptor
 * @param[in]   buf         Input buffer
 * @param[in]   size        Buffer size
 * @return      Number of input characters
 * @retval      >= 0        Number of input characters
 * @retval      < 0         Input error
 */
/*--------------------------------------------------------------------------*/
int
getdata(void *window_mgr, const char *prompt, int fd, char *buf, const int size)
{
    int     ret;
    int     i, j;

    j = -1;
    for (i = 0; i < (size-1); i++)  {
        ret = read(fd, &buf[i], 1);

        if (ret < 0)    {
            return(ret);
        }

        if ((buf[i] == '\n') || (buf[i] == '\r'))   break;
        if (buf[i] == '\t') {
            buf[i] = ' ';
        }
        if ((buf[i] == '#') && (j < 0)) {
            j = i;
        }
    }
    buf[i] = 0;
    print_log("%s%s", prompt, buf);

    /* Delete trailing spaces       */
    if (j >= 0) {
        for (; j > 0; j--)  {
            if (buf[j-1] != ' ')    break;
        }
        buf[j] = 0;
        i = j;
    }

    /* Delete header spaces         */
    for (j = 0; buf[j]; j++)    {
        if (buf[j] != ' ')  break;
    }
    if (j > 0)  {
        strcpy( buf, &buf[j] );
        i -= j;
    }
    return(i);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   print_log: Weston log output
 *
 * @param[in]   fmt         printf format
 * @param[in]   ...         printf arguments
 * @return      None
 */
/*--------------------------------------------------------------------------*/
void
print_log(const char *fmt, ...)
{
    va_list     ap;
    char        log[128];
    struct timeval  NowTime;
    extern long timezone;
    static int  sTimeZone = (99*60*60);

    va_start(ap, fmt);
    vsnprintf(log, sizeof(log)-2, fmt, ap);
    va_end(ap);

    gettimeofday( &NowTime, (struct timezone *)0 );
    if( sTimeZone > (24*60*60) )    {
        tzset();
        sTimeZone = timezone;
    }
    NowTime.tv_sec -= sTimeZone;
    fprintf(stderr, "[%02d:%02d:%02d.%03d@%d] %s\n", (int)((NowTime.tv_sec/3600) % 24),
            (int)((NowTime.tv_sec/60) % 60), (int)(NowTime.tv_sec % 60),
            (int)NowTime.tv_usec/1000, getpid(), log);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   wayland_dispatch_nonblock: Read from wayland if receive data exist
 *
 * @param[in]   display     Wayland connection
 * @return      None
 */
/*--------------------------------------------------------------------------*/
void
wayland_dispatch_nonblock(struct wl_display *display)
{
    int     nread;

    /* Check wayland input      */
    do  {
        /* Flush send data          */
        wl_display_flush(display);

        nread = 0;
        if (ioctl(wl_display_get_fd(display), FIONREAD, &nread) < 0)    {
            nread = 0;
        }
        if (nread >= 8) {
            /* Read event from wayland  */
            wl_display_dispatch(display);
        }
    } while (nread > 0);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   sleep_with_wayland: Sleep and receive wayland event
 *
 * @param[in]   display     Wayland connection
 * @param[in]   msec        Sleep time (miri-sec)
 * @return      None
 */
/*--------------------------------------------------------------------------*/
void
sleep_with_wayland(struct wl_display *display, int msec)
{
    int     nread;
    int     fd;


    fd = wl_display_get_fd(display);

    do  {
        /* Flush send data          */
        wl_display_flush(display);

        /* Check wayland input      */
        nread = 0;
        if (ioctl(fd, FIONREAD, &nread) < 0)    {
            nread = 0;
        }
        if (nread >= 8) {
            /* Read event from wayland  */
            wl_display_dispatch(display);
        }
        msec -= 20;
        if (msec >= 0)   usleep(20*1000);
    } while (msec > 0);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   wait_with_wayland: Wait for end and receive wayland event
 *
 * @param[in]   display     Wayland connection
 * @param[in]   msec        Maximum wait time (miri-sec)
 * @param[in]   endflag     End flag address
 * @return      None
 */
/*--------------------------------------------------------------------------*/
void
wait_with_wayland(struct wl_display *display, int msec, int *endflag)
{
    int     nread;
    int     fd;

    fd = wl_display_get_fd(display);

    do  {
        /* Flush send data          */
        wl_display_flush(display);

        /* Check wayland input      */
        nread = 0;
        if (ioctl(fd, FIONREAD, &nread) < 0)    {
            nread = 0;
        }
        if (nread >= 8) {
            /* Read event from wayland  */
            wl_display_dispatch(display);
        }
        msec -= 20;
        if (msec >= 0)   usleep(20*1000);
    } while ((*endflag == 0) && (msec > 0));
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   sec_str_2_value: Convert seconds string to value
 *
 * @param[in]   ssec        Time ("sec.msec")
 * @return      miri-second
 */
/*--------------------------------------------------------------------------*/
int
sec_str_2_value(const char *ssec)
{
    int     sec;
    int     msec;
    int     n;
    char    *errp = NULL;

    sec = strtol(ssec, &errp, 0) * 1000;
    if ((errp != NULL) && (*errp == '.'))   {
        msec = 0;
        n = 0;
        for (errp++; *errp; errp++) {
            if ((*errp < '0') || (*errp > '9')) break;
            msec = msec * 10 + *errp - '0';
            n++;
            if (n >= 3) break;
        }
        if (n == 1)     msec *= 100;
        if (n == 2)     msec *= 10;
        sec += msec;
    }
    return(sec);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   opengl_init: Initialize OpenGL ESv2/EGL
 *
 * @param[in]   display     Wayland connection
 * @param[out]  rconf       EGL configuration
 * @param[out]  rctx        EGL context
 * @return      EGL display
 * @retval      != NULL     EGL display
 * @retval      == NULL     OpenGL/EGL initialize error
 */
/*--------------------------------------------------------------------------*/
EGLDisplay
opengl_init(struct wl_display *display, EGLConfig *rconf, EGLContext *rctx)
{
    EGLDisplay  dpy;                    /* EGL dsplay id                    */
    EGLint      major, minor;
    EGLint      num_configs;
    EGLConfig   conf = 0;
    EGLContext  ctx;

    static const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 1,
        EGL_GREEN_SIZE, 1,
        EGL_BLUE_SIZE, 1,
        EGL_ALPHA_SIZE, 1,
        EGL_DEPTH_SIZE, 1,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    static const EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE
    };

    dpy = eglGetDisplay((EGLNativeDisplayType)display);
    if (! dpy)  {
        fprintf(stderr, "eglGetDisplay Error\n");
        return NULL;
    }

    if (eglInitialize(dpy, &major, &minor) == EGL_FALSE)    {
        fprintf(stderr, "eglInitialize Error\n");
        return NULL;
    }

    if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
        fprintf(stderr, "eglBindAPI Error\n");
        return NULL;
    }

    if (eglChooseConfig(dpy, config_attribs, &conf, 1, &num_configs) == EGL_FALSE)  {
        fprintf(stderr, "eglChooseConfig Error\n");
        return NULL;
    }

    ctx = eglCreateContext(dpy, conf, EGL_NO_CONTEXT, context_attribs);
    if (! ctx)  {
        fprintf(stderr, "eglCreateContext Error\n");
        return NULL;
    }
    *rconf = conf;
    *rctx = ctx;

    wayland_dispatch_nonblock(display);

    return(dpy);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   opengl_create_window: Create OpenGL/EGL window
 *
 * @param[in]   display     Wayland connection
 * @param[in]   surface     Wayland surface
 * @param[in]   dpy         EGL display
 * @param[in]   conf        EGL configuration
 * @param[in]   ctx         EGL context
 * @param[in]   width       Widown width
 * @param[in]   height      Window height
 * @param[in]   color       Initiale color(A<<24|R<<16|G<<8|B)
 * @return      EGL surface
 * @retval      != NULL     EGL surface
 * @retval      == NULL     Create window error
 */
/*--------------------------------------------------------------------------*/
EGLSurface
opengl_create_window(struct wl_display *display, struct wl_surface *surface,
                     EGLDisplay dpy, EGLConfig conf, EGLContext ctx,
                     const int width, const int height, const int color)
{
    struct wl_egl_window    *egl_window;
    EGLSurface              egl_surface;

    static const EGLint surface_attribs[] = {
        EGL_ALPHA_FORMAT, EGL_ALPHA_FORMAT_PRE, EGL_NONE
    };

    egl_window = wl_egl_window_create(surface, width, height);
    egl_surface = eglCreateWindowSurface(dpy, conf, (EGLNativeWindowType)egl_window,
                                         surface_attribs);
    eglMakeCurrent(dpy, egl_surface, egl_surface, ctx);
    glViewport(0, 0, width, height);

    wayland_dispatch_nonblock(display);

    opengl_clear_window(color);

    opengl_swap_buffer(display, dpy, egl_surface);

    return(egl_surface);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   opengl_clear_window: OpenGL window clear
 *
 * @param[in]   color       Initiale color(A<<24|R<<16|G<<8|B)
 * @return      None
 */
/*--------------------------------------------------------------------------*/
void
opengl_clear_window(const unsigned int color)
{
    double                  r, g, b, a;

    r = (double)((color>>16) & 0x0ff);
    r = r / 256.0;
    g = (double)((color>>8) & 0x0ff);
    g = g / 256.0;
    b = (double)(color & 0x0ff);
    b = b / 256.0;
    a = (double)((color>>24) & 0x0ff);
    a = (a + 1.0) / 256.0;

    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT| GL_STENCIL_BUFFER_BIT);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   opengl_create_window: Create OpenGL/EGL window
 *
 * @param[in]   display     Wayland connection
 * @param[in]   dpy         EGL display
 * @param[in]   egl_surface EGL surface
 * @return      None
 */
/*--------------------------------------------------------------------------*/
void
opengl_swap_buffer(struct wl_display *display, EGLDisplay dpy, EGLSurface egl_surface)
{
    eglSwapBuffers(dpy, egl_surface);

    wayland_dispatch_nonblock(display);
}

