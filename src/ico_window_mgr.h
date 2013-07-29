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
 * @date    Jul-26-2013
 */

#ifndef _ICO_WINDOW_MGR_H_
#define _ICO_WINDOW_MGR_H_

/* Cleint management table              */
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

/* Node information                     */
struct uifw_node_table {
    uint16_t    node;                       /* node Id                              */
    uint16_t    displayno;                  /* weston display number                */
    int         disp_x;                     /* display frame buffer X-coordinate    */
    int         disp_y;                     /* display frame buffer Y-coordinate    */
    int         disp_width;                 /* display width                        */
    int         disp_height;                /* display height                       */
};

/* Layer management table               */
struct  uifw_win_layer  {
    uint32_t layer;                         /* Layer Id                             */
    uint16_t attribute;                     /* Layer attribute                      */
    char     visible;                       /* visibility                           */
    char     res;                           /* (unused)                             */
    struct wl_list surface_list;            /* Surfacae list                        */
    struct wl_list link;                    /* Link pointer for layer list          */
};

/* UIFW surface                         */
struct shell_surface;
struct uifw_win_surface {
    uint32_t surfaceid;                     /* UIFW SurfaceId                       */
    struct uifw_node_table *node_tbl;       /* Node manager of ico_window_mgr       */
    struct uifw_win_layer *win_layer;       /* surface layer                        */
    struct weston_surface *surface;         /* Weston surface                       */
    struct shell_surface  *shsurf;          /* Shell(IVI-Shell) surface             */
    struct uifw_client    *uclient;         /* Client                               */
    int     x;                              /* X-coordinate                         */
    int     y;                              /* Y-coordinate                         */
    int     width;                          /* Width                                */
    int     height;                         /* Height                               */
    char    winname[ICO_IVI_WINNAME_LENGTH];/* Window name                          */
    char    visible;                        /* visibility                           */
    char    raise;                          /* raise(top of the layer)              */
    char    created;                        /* sended created event to manager      */
    char    mapped;                         /* end of map                           */
    char    restrain_configure;             /* restrant configure event             */
    char    res[3];                         /* (unused)                             */
    struct  _uifw_win_surface_animation {   /* wndow animation                      */
        struct weston_animation animation;  /* weston animation control             */
        uint16_t type;                      /* current animation type               */
        uint16_t name;                      /* curremt animation nameId             */
        uint16_t next_name;                 /* next animation nameId                */
        uint16_t hide_name;                 /* animation nameId for hide            */
        uint16_t hide_time;                 /* animation time(ms) for hide          */
        uint16_t show_name;                 /* animation nameId for show            */
        uint16_t show_time;                 /* animation time(ms) for show          */
        uint16_t move_name;                 /* animation nameId for move            */
        uint16_t move_time;                 /* animation time(ms) for move          */
        uint16_t resize_name;               /* animation nameId for resize          */
        uint16_t resize_time;               /* animation time(ms) for resize        */
        short   current;                    /* animation current percentage         */
        char    state;                      /* animation state                      */
        char    visible;                    /* need hide(1)/show(2) at end of animation*/
        char    restrain_configure;         /* restrain surface resize              */
        char    res;                        /* (unused)                             */
        uint32_t starttime;                 /* start time(ms)                       */
        void    *animadata;                 /* animation data                       */
    }       animation;
    struct wl_list          ivi_layer;      /* surface list of same layer           */
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
#define ICO_WINDOW_MGR_ANIMATION_STATE_SHOW     1   /* show(in) animation           */
#define ICO_WINDOW_MGR_ANIMATION_STATE_HIDE     2   /* hide(out) animation          */
#define ICO_WINDOW_MGR_ANIMATION_STATE_MOVE     3   /* move animation               */
#define ICO_WINDOW_MGR_ANIMATION_STATE_RESIZE   4   /* resize animation             */

/* extended(plugin) animation operation */
#define ICO_WINDOW_MGR_ANIMATION_NAME       0       /* convert animation name to Id */
#define ICO_WINDOW_MGR_ANIMATION_DESTROY    99      /* surface destroy              */
#define ICO_WINDOW_MGR_ANIMATION_OPNONE     0       /* no animation                 */
#define ICO_WINDOW_MGR_ANIMATION_OPHIDE     1       /* change to hide               */
#define ICO_WINDOW_MGR_ANIMATION_OPSHOW     2       /* change to show               */
#define ICO_WINDOW_MGR_ANIMATION_OPMOVE     3       /* surface move                 */
#define ICO_WINDOW_MGR_ANIMATION_OPRESIZE   4       /* surface resize               */
#define ICO_WINDOW_MGR_ANIMATION_OPCANCEL   9       /* animation cancel             */

/* Prototype for function               */
                                            /* surface visible control              */
void ico_window_mgr_set_visible(struct uifw_win_surface *usurf, const int visible);
                                            /* get client applicationId             */
char *ico_window_mgr_get_appid(struct wl_client* client);
                                            /* set window animation hook            */
void ico_window_mgr_set_hook_animation(int (*hook_animation)(const int op, void *data));

#endif  /*_ICO_WINDOW_MGR_H_*/

