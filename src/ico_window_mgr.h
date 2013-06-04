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
 * @brief   Public functions in ico_window_mgr Weston plugin
 *
 * @date    Feb-08-2013
 */

#ifndef _ICO_WINDOW_MGR_H_
#define _ICO_WINDOW_MGR_H_

<<<<<<< HEAD
struct shell_surface;

/* Prototype for function               */
char *ico_window_mgr_appid(struct wl_client* client);

#endif  /*_ICO_WINDOW_MGR_H_*/
=======
/* Cleint management table          */
struct uifw_client  {
    struct wl_client *client;               /* Wayland client                       */
    int     pid;                            /* ProcessId (pid)                      */
    char    appid[ICO_IVI_APPID_LENGTH];    /* ApplicationId(from AppCore AUL)      */
    char    manager;                        /* Manager flag (Need send event)       */
    char    noconfigure;                    /* no need configure event              */
    char    res[2];
    struct wl_resource *resource;
    struct wl_list  link;
};

/* UIFW surface                         */
struct shell_surface;
struct uifw_win_surface {
    uint32_t id;                            /* UIFW SurfaceId                       */
    int     layer;                          /* LayerId                              */
    struct weston_surface *surface;         /* Weston surface                       */
    struct shell_surface  *shsurf;          /* Shell(IVI-Shell) surface             */
    struct uifw_client    *uclient;         /* Client                               */
    int     x;                              /* X-axis                               */
    int     y;                              /* Y-axis                               */
    int     width;                          /* Width                                */
    int     height;                         /* Height                               */
    struct  _uifw_win_surface_animation {   /* wndow animation                      */
        struct weston_animation animation;  /* animation control                    */
        short   type;                       /* animation type                       */
        short   type_next;                  /* next animation type                  */
        short   current;                    /* animation current percentage         */
        char    state;                      /* animation state                      */
        char    visible;                    /* need visible(1)/hide(2) at end of animation*/
        uint32_t starttime;                 /* start time(ms)                       */
    }       animation;
    void    *animadata;                     /* animation data                       */
    struct wl_list link;                    /* surface link list                    */
    struct uifw_win_surface *next_idhash;   /* UIFW SurfaceId hash list             */
    struct uifw_win_surface *next_wshash;   /* Weston SurfaceId hash list           */
};

/* animation operation                  */
/* default animation                    */
#define ICO_WINDOW_MGR_ANIMATION_NONE           0   /* no animation                 */

/* return code of animation hook function*/
#define ICO_WINDOW_MGR_ANIMATION_RET_NOANIMA    -1  /* no animation                 */
#define ICO_WINDOW_MGR_ANIMATION_RET_ANIMA      0   /* animation                    */
#define ICO_WINDOW_MGR_ANIMATION_RET_ANIMASHOW  1   /* animation with visible       */

/* animation state                      */
#define ICO_WINDOW_MGR_ANIMATION_STATE_NONE     0   /* not animation                */
#define ICO_WINDOW_MGR_ANIMATION_STATE_IN       1   /* show(in) animation           */
#define ICO_WINDOW_MGR_ANIMATION_STATE_OUT      2   /* hide(out) animation          */
#define ICO_WINDOW_MGR_ANIMATION_STATE_MOVE     3   /* move animation               */
#define ICO_WINDOW_MGR_ANIMATION_STATE_RESIZE   4   /* resize animation             */

/* extended(plugin) animation operation */
#define ICO_WINDOW_MGR_ANIMATION_TYPE       0       /* convert animation name to value*/
#define ICO_WINDOW_MGR_ANIMATION_DESTROY    99      /* surface destroy              */
#define ICO_WINDOW_MGR_ANIMATION_OPIN       1       /* change to show               */
#define ICO_WINDOW_MGR_ANIMATION_OPOUT      2       /* change to hide               */
#define ICO_WINDOW_MGR_ANIMATION_OPMOVE     3       /* surface move                 */
#define ICO_WINDOW_MGR_ANIMATION_OPRESIZE   4       /* surface resize               */
#define ICO_WINDOW_MGR_ANIMATION_OPCANCEL   9       /* animation cancel             */

/* Prototype for function               */
                                            /* get client applicationId             */
char *ico_window_mgr_appid(struct wl_client* client);
                                            /* set window animation hook            */
void ico_window_mgr_set_animation(int (*hook_animation)(const int op, void *data));

#endif  /*_ICO_WINDOW_MGR_H_*/

>>>>>>> master
