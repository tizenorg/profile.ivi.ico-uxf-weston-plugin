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
 * @brief   Window Animation (Weston(Wayland) PlugIn)
 *
 * @date    Jul-26-2013
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>

#include <weston/compositor.h>
#include "ico_ivi_common.h"
#include "ico_ivi_shell.h"
#include "ico_window_mgr.h"

/* Animation type               */
#define ANIMA_ZOOM              1           /* ZoomIn/ZoomOut                       */
#define ANIMA_FADE              2           /* FadeIn/FadeOut                       */
#define ANIMA_SLIDE_TORIGHT     3           /* SlideIn left to right/SlideOut right to left*/
#define ANIMA_SLIDE_TOLEFT      4           /* SlideIn right to left/SlideOut left to right*/
#define ANIMA_SLIDE_TOBOTTOM    5           /* SlideIn top to bottom/SlideOut bottom to top*/
#define ANIMA_SLIDE_TOTOP       6           /* SlideIn bottom to top/SlideOut top to bottom*/

/* Visible control at end of animation  */
#define ANIMA_NOCONTROL_AT_END  0           /* no need surface show/hide at end of animation*/
#define ANIMA_SHOW_AT_END       1           /* surface show at end of animation     */
#define ANIMA_HIDE_AT_END       2           /* surface hide at end of animation     */

/* animation data               */
struct animation_data   {
    struct animation_data   *next_free;     /* free data list                       */
    int     x;                              /* original X coordinate                */
    int     y;                              /* original Y coordinate                */
    int     width;                          /* original width                       */
    int     height;                         /* original height                      */
    char    geometry_saved;                 /* need geometry restore at end         */
    char    transform_set;                  /* need transform reset at end          */
    char    res[2];                         /* (unused)                             */
    struct weston_transform transform;      /* transform matrix                     */
    void    (*end_function)(struct weston_animation *animation);
                                            /* animation end function               */
};

/* static valiables             */
static struct weston_compositor *weston_ec; /* Weston compositor                    */
static char *default_animation;             /* default animation name               */
static int  animation_time;                 /* animation time(ms)                   */
static int  animation_fpar;                 /* animation frame parcent(%)           */
static struct animation_data    *free_data; /* free data list                       */

/* static function              */
                                            /* slide animation                      */
static void animation_slide(struct weston_animation *animation,
                            struct weston_output *output, uint32_t msecs);
                                            /* fade animation                       */
static void animation_fade(struct weston_animation *animation,
                           struct weston_output *output, uint32_t msecs);
                                            /* fade animation end                   */
static void animation_fade_end(struct weston_animation *animation);
                                            /* continue animation                   */
static int animation_cont(struct weston_animation *animation,
                          struct weston_output *output, uint32_t msecs);
                                            /* terminate animation                  */
static void animation_end(struct uifw_win_surface *usurf, const int disp);

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_window_animation: Animation addin entry
 *
 * @param[in]   op      animation operation
 * @param[in]   data    data
 * @return      result
 * @retval      ICO_WINDOW_MGR_ANIMATION_RET_ANIMA      success
 * @retval      ICO_WINDOW_MGR_ANIMATION_RET_ANIMASHOW  success(force visible)
 * @retval      ICO_WINDOW_MGR_ANIMATION_RET_NOANIMA    error(no animation)
 */
/*--------------------------------------------------------------------------*/
static int
ico_window_animation(const int op, void *data)
{
    struct uifw_win_surface *usurf;
    struct weston_output *output;
    int         ret;
    uint32_t    nowsec;
    struct timeval  nowtv;
    int         time;
    int         name;

    if (op == ICO_WINDOW_MGR_ANIMATION_NAME)    {
        /* convert animation name to animation type value   */
        if (strcasecmp((char *)data, "fade") == 0)  {
            uifw_trace("ico_window_animation: Type %s(%d)", (char *)data, ANIMA_FADE);
            return ANIMA_FADE;
        }
        else if (strcasecmp((char *)data, "zoom") == 0) {
            uifw_trace("ico_window_animation: Type %s(%d)", (char *)data, ANIMA_ZOOM);
            return ANIMA_ZOOM;
        }
        else if (strcasecmp((char *)data, "slide.toleft") == 0) {
            uifw_trace("ico_window_animation: Type %s(%d)", (char *)data, ANIMA_SLIDE_TOLEFT);
            return ANIMA_SLIDE_TOLEFT;
        }
        else if (strcasecmp((char *)data, "slide.toright") == 0)    {
            uifw_trace("ico_window_animation: Type %s(%d)", (char *)data, ANIMA_SLIDE_TORIGHT);
            return ANIMA_SLIDE_TORIGHT;
        }
        else if (strcasecmp((char *)data, "slide.totop") == 0)  {
            uifw_trace("ico_window_animation: Type %s(%d)", (char *)data, ANIMA_SLIDE_TOTOP);
            return ANIMA_SLIDE_TOTOP;
        }
        else if (strcasecmp((char *)data, "slide.tobottom") == 0)   {
            uifw_trace("ico_window_animation: Type %s(%d)", (char *)data, ANIMA_SLIDE_TOBOTTOM);
            return ANIMA_SLIDE_TOBOTTOM;
        }
        uifw_warn("ico_window_animation: Unknown Type %s", (char *)data);
        return ICO_WINDOW_MGR_ANIMATION_RET_NOANIMA;
    }

    usurf = (struct uifw_win_surface *)data;

    if (op == ICO_WINDOW_MGR_ANIMATION_DESTROY) {
        if ((usurf->animation.state != ICO_WINDOW_MGR_ANIMATION_STATE_NONE) ||
            (usurf->animation.animadata != NULL)) {
            uifw_trace("ico_window_animation: Destroy %08x", (int)usurf);
            animation_end(usurf, 0);
        }
        return ICO_WINDOW_MGR_ANIMATION_RET_NOANIMA;
    }

    if (op == ICO_WINDOW_MGR_ANIMATION_OPCANCEL)    {
        /* cancel animation                     */
        if ((usurf->animation.state != ICO_WINDOW_MGR_ANIMATION_STATE_NONE) &&
            (usurf->animation.animation.frame != NULL)) {
            uifw_trace("ico_window_animation: cancel %s.%08x",
                       usurf->uclient->appid, usurf->surfaceid);
            (*usurf->animation.animation.frame)(&usurf->animation.animation, NULL, 0);
        }
        animation_end(usurf, 1);
        ret = ICO_WINDOW_MGR_ANIMATION_RET_ANIMA;
    }
    else    {
        /* setup animation              */
        if ((usurf->animation.state == ICO_WINDOW_MGR_ANIMATION_STATE_NONE) ||
            (usurf->animation.current > 95))    {
            usurf->animation.animation.frame_counter = 1;
            usurf->animation.current = 0;
            if (usurf->animation.state == ICO_WINDOW_MGR_ANIMATION_STATE_NONE)  {
                wl_list_init(&usurf->animation.animation.link);
                output = container_of(weston_ec->output_list.next,
                                      struct weston_output, link);
                wl_list_insert(output->animation_list.prev,
                               &usurf->animation.animation.link);
            }
        }
        else if (((usurf->animation.state == ICO_WINDOW_MGR_ANIMATION_STATE_SHOW) &&
                  (op == ICO_WINDOW_MGR_ANIMATION_OPHIDE)) ||
                 ((usurf->animation.state == ICO_WINDOW_MGR_ANIMATION_STATE_HIDE) &&
                  (op == ICO_WINDOW_MGR_ANIMATION_OPSHOW)))   {
            /* change ...In(ex.FadeIn) to ...Out(FadeOut) or ...Out to ...In    */
            gettimeofday(&nowtv, NULL);
            nowsec = (uint32_t)(((long long)nowtv.tv_sec) * 1000L +
                                ((long long)nowtv.tv_usec) / 1000L);
            usurf->animation.current = 100 - usurf->animation.current;
            if (op == ICO_WINDOW_MGR_ANIMATION_OPHIDE)  {
                time = usurf->animation.hide_time;
            }
            else    {
                time = usurf->animation.show_time;
            }
            time = (time > 0) ? time : animation_time;
            ret = ((usurf->animation.current) * time) / 100;
            if (nowsec >= (uint32_t)ret)    {
                usurf->animation.starttime = nowsec - ret;
            }
            else    {
                usurf->animation.starttime = ((long long)nowsec) + ((long long)0x100000000L)
                                             - ((long long)ret);
            }
            usurf->animation.animation.frame_counter = 2;
        }

        /* set animation function       */
        if (op == ICO_WINDOW_MGR_ANIMATION_OPSHOW)    {
            usurf->animation.state = ICO_WINDOW_MGR_ANIMATION_STATE_SHOW;
            name = usurf->animation.show_name;
            usurf->animation.name = name;
            uifw_trace("ico_window_animation: show(in) %s.%08x anima=%d",
                       usurf->uclient->appid, usurf->surfaceid, name);
            ret = ICO_WINDOW_MGR_ANIMATION_RET_ANIMA;
        }
        else if (op == ICO_WINDOW_MGR_ANIMATION_OPHIDE)    {
            usurf->animation.state = ICO_WINDOW_MGR_ANIMATION_STATE_HIDE;
            name = usurf->animation.hide_name;
            usurf->animation.name = name;
            uifw_trace("ico_window_animation: hide(out) %s.%08x anima=%d",
                       usurf->uclient->appid, usurf->surfaceid, name);
            ret = ICO_WINDOW_MGR_ANIMATION_RET_ANIMASHOW;
        }
        else if (op == ICO_WINDOW_MGR_ANIMATION_OPMOVE)    {
            usurf->animation.state = ICO_WINDOW_MGR_ANIMATION_STATE_MOVE;
            name = usurf->animation.move_name;
            usurf->animation.name = name;
            uifw_trace("ico_window_animation: move %s.%08x anima=%d",
                       usurf->uclient->appid, usurf->surfaceid, name);
            ret = ICO_WINDOW_MGR_ANIMATION_RET_ANIMA;
        }
        else    {
            usurf->animation.state = ICO_WINDOW_MGR_ANIMATION_STATE_RESIZE;
            name = usurf->animation.resize_name;
            usurf->animation.name = name;
            uifw_trace("ico_window_animation: resize %s.%08x anima=%d",
                       usurf->uclient->appid, usurf->surfaceid, name);
            ret = ICO_WINDOW_MGR_ANIMATION_RET_ANIMA;
        }
        if ((name == ANIMA_SLIDE_TOLEFT) || (name == ANIMA_SLIDE_TORIGHT) ||
            (name == ANIMA_SLIDE_TOTOP) || (name == ANIMA_SLIDE_TOBOTTOM))  {
            usurf->animation.animation.frame = animation_slide;
            usurf->restrain_configure = 1;
            (*usurf->animation.animation.frame)(&usurf->animation.animation, NULL, 1);
        }
        else if (name == ANIMA_FADE)   {
            usurf->animation.animation.frame = animation_fade;
            (*usurf->animation.animation.frame)(&usurf->animation.animation, NULL, 1);
        }
        else    {
            /* no yet support   */
            usurf->animation.animation.frame = NULL;
            usurf->animation.state = ICO_WINDOW_MGR_ANIMATION_STATE_NONE;
            usurf->restrain_configure = 0;
            usurf->animation.name = 0;
            wl_list_remove(&usurf->animation.animation.link);
            ret = ICO_WINDOW_MGR_ANIMATION_RET_NOANIMA;
        }
        usurf->animation.type = op;
    }
    if (ret == ICO_WINDOW_MGR_ANIMATION_RET_ANIMASHOW)  {
        usurf->animation.visible = ANIMA_HIDE_AT_END;
    }
    else    {
        usurf->animation.visible = ANIMA_NOCONTROL_AT_END;
    }
    weston_compositor_schedule_repaint(weston_ec);
    return ret;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   animation_cont: continue animation
 *
 * @param[in]   animation   weston animation table
 * @param[in]   output      weston output table
 * @param[in]   msecs       current time stamp
 * @return      time has come
 * @retval      =0          time has come
 * @retval      >0          time has not yet come(return value is current parcent)
 */
/*--------------------------------------------------------------------------*/
static int
animation_cont(struct weston_animation *animation, struct weston_output *output,
               uint32_t msecs)
{
    struct uifw_win_surface *usurf;
    struct animation_data   *animadata;
    int         par;
    uint32_t    nowsec;
    struct timeval  nowtv;
    int         time;

    gettimeofday(&nowtv, NULL);
    nowsec = (uint32_t)(((long long)nowtv.tv_sec) * 1000L +
                        ((long long)nowtv.tv_usec) / 1000L);

    usurf = container_of(animation, struct uifw_win_surface, animation.animation);

    if (animation->frame_counter <= 1)  {
        /* first call, initialize           */
        animation->frame_counter = 1;
        usurf->animation.starttime = nowsec;
        usurf->animation.current = 1000;
        if (! usurf->animation.animadata) {
            if (free_data)  {
                usurf->animation.animadata = (void *)free_data;
                free_data = free_data->next_free;
            }
            else    {
                usurf->animation.animadata = (void *)malloc(sizeof(struct animation_data));
            }
            memset(usurf->animation.animadata, 0, sizeof(struct animation_data));
        }
        animadata = (struct animation_data *)usurf->animation.animadata;
        animadata->x = usurf->x;
        animadata->y = usurf->y;
        animadata->width = usurf->width;
        animadata->height = usurf->height;
        animadata->geometry_saved = 1;
    }
    else if (! usurf->animation.animadata)    {
        animation_end(usurf, 0);
        return 999;
    }

    if (nowsec >= usurf->animation.starttime)   {
        nowsec = nowsec - usurf->animation.starttime;   /* elapsed time(ms) */
    }
    else    {
        nowsec = (uint32_t)(((long long)0x100000000L) +
                            ((long long)nowsec) - ((long long)usurf->animation.starttime));
    }
    switch (usurf->animation.state) {
    case ICO_WINDOW_MGR_ANIMATION_STATE_SHOW:
        time = usurf->animation.show_time;
        break;
    case ICO_WINDOW_MGR_ANIMATION_STATE_HIDE:
        time = usurf->animation.hide_time;
        break;
    case ICO_WINDOW_MGR_ANIMATION_STATE_MOVE:
        time = usurf->animation.move_time;
        break;
    default:
        time = usurf->animation.resize_time;
        break;
    }
    time = (time > 0) ? time : animation_time;
    if (((output == NULL) && (msecs == 0)) || (nowsec >= ((uint32_t)time))) {
        par = 100;
    }
    else    {
        par = (nowsec * 100 + time / 2) / time;
        if (par < 2)    par = 2;
    }
    if ((par >= 100) ||
        (abs(usurf->animation.current - par) >= animation_fpar)) {
        usurf->animation.current = par;
        return 0;
    }
    return par;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   animation_end: terminate animation
 *
 * @param[in]   usurf       UIFW surface table
 * @param[in]   disp        display control(1)/no display(0)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
animation_end(struct uifw_win_surface *usurf, const int disp)
{
    struct animation_data   *animadata;

    usurf->animation.state = ICO_WINDOW_MGR_ANIMATION_STATE_NONE;
    animadata = (struct animation_data *)usurf->animation.animadata;

    if (animadata)  {
        if (animadata->end_function)    {
            (*animadata->end_function)(&usurf->animation.animation);
        }
        if (animadata->geometry_saved > 1)  {
            usurf->x = animadata->x;
            usurf->y = animadata->y;
            usurf->width = animadata->width;
            usurf->height = animadata->height;
            animadata->geometry_saved = 0;
        }
        wl_list_remove(&usurf->animation.animation.link);
        if (animadata->transform_set)   {
            wl_list_remove(&animadata->transform.link);
            animadata->transform_set = 0;
        }
        weston_surface_geometry_dirty(usurf->surface);
    }
    if (disp)   {
        if ((usurf->animation.visible == ANIMA_HIDE_AT_END) &&
            (usurf->visible != 0))  {
            ico_window_mgr_set_visible(usurf, 0);
            weston_surface_damage(usurf->surface);
        }
        if ((usurf->animation.visible == ANIMA_SHOW_AT_END) &&
            (usurf->visible == 0))  {
            ico_window_mgr_set_visible(usurf, 1);
            weston_surface_damage(usurf->surface);
        }
        usurf->restrain_configure = 0;
        weston_surface_geometry_dirty(usurf->surface);
        weston_compositor_schedule_repaint(weston_ec);
    }
    usurf->animation.visible = ANIMA_NOCONTROL_AT_END;
    if (usurf->animation.next_name != ICO_WINDOW_MGR_ANIMATION_NONE)    {
        switch(usurf->animation.type)   {
        case ICO_WINDOW_MGR_ANIMATION_OPHIDE:
            usurf->animation.hide_name = usurf->animation.next_name;
            break;
        case ICO_WINDOW_MGR_ANIMATION_OPSHOW:
            usurf->animation.show_name = usurf->animation.next_name;
            break;
        case ICO_WINDOW_MGR_ANIMATION_OPMOVE:
            usurf->animation.move_name = usurf->animation.next_name;
            break;
        case ICO_WINDOW_MGR_ANIMATION_OPRESIZE:
            usurf->animation.resize_name = usurf->animation.next_name;
            break;
        default:
            break;
        }
        usurf->animation.next_name = ICO_WINDOW_MGR_ANIMATION_NONE;
    }
    if (animadata)   {
        usurf->animation.animadata = NULL;
        animadata->next_free = free_data;
        free_data = animadata;
    }
    usurf->animation.type = ICO_WINDOW_MGR_ANIMATION_OPNONE;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   animation_slide: slide animation
 *
 * @param[in]   animation   weston animation table
 * @param[in]   outout      weston output table
 * @param[in]   mseces      current time(unused)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
animation_slide(struct weston_animation *animation,
                struct weston_output *output, uint32_t msecs)
{
    struct uifw_win_surface *usurf;
    struct animation_data   *animadata;
    struct weston_surface   *es;
    int         dwidth, dheight;
    int         par;
    int         x;
    int         y;

    usurf = container_of(animation, struct uifw_win_surface, animation.animation);

    par = animation_cont(animation, output, msecs);
    if (par > 0)    {
        /* continue animation   */
        if( par <= 100) {
            weston_compositor_schedule_repaint(weston_ec);
        }
        return;
    }
    par = usurf->animation.current;
    animadata = (struct animation_data *)usurf->animation.animadata;

    uifw_trace("animation_slide: usurf=%08x count=%d %d%% name=%d state=%d",
               (int)usurf, animation->frame_counter, par,
               usurf->animation.name, usurf->animation.state);

    es = usurf->surface;
    x = usurf->x;
    y = usurf->y;

    switch (usurf->animation.name)  {
    case ANIMA_SLIDE_TORIGHT:           /* slide in left to right           */
        if (usurf->animation.state == ICO_WINDOW_MGR_ANIMATION_STATE_SHOW)    {
            /* slide in left to right   */
            x = 0 - animadata->width +
                ((animadata->x + animadata->width) * par / 100);
        }
        else    {
            /* slide out right to left  */
            x = 0 - animadata->width +
                ((animadata->x + animadata->width) * (100 - par) / 100);
        }
        break;
    case ANIMA_SLIDE_TOLEFT:            /* slide in right to left           */
        dwidth = (container_of(weston_ec->output_list.next,
                               struct weston_output, link))->width;
        if (usurf->animation.state == ICO_WINDOW_MGR_ANIMATION_STATE_SHOW)    {
            /* slide in right to left   */
            x = animadata->x + (dwidth - animadata->x) * (100 - par) / 100;
        }
        else    {
            /* slide out left to right  */
            x = animadata->x + (dwidth - animadata->x) * par / 100;
        }
        break;
    case ANIMA_SLIDE_TOBOTTOM:          /* slide in top to bottom           */
        if (usurf->animation.state == ICO_WINDOW_MGR_ANIMATION_STATE_SHOW)    {
            /* slide in top to bottom   */
            y = 0 - animadata->height +
                ((animadata->y + animadata->height) * par / 100);
        }
        else    {
            /* slide out bottom to top  */
            y = 0 - animadata->height +
                ((animadata->y + animadata->height) * (100 - par) / 100);
        }
        break;
    default: /*ANIMA_SLIDE_TOTOP*/      /* slide in bottom to top           */
        dheight = (container_of(weston_ec->output_list.next,
                                struct weston_output, link))->height;
        if (usurf->animation.state == ICO_WINDOW_MGR_ANIMATION_STATE_SHOW)    {
            /* slide in bottom to top   */
            y = animadata->y + (dheight - animadata->y) * (100 - par) / 100;
        }
        else    {
            /* slide out top to bottom  */
            y = animadata->y + (dheight - animadata->y) * par / 100;
        }
        break;
    }

    es->geometry.x = usurf->node_tbl->disp_x + x;
    es->geometry.y = usurf->node_tbl->disp_y + y;
    if ((es->output) && (es->buffer_ref.buffer) &&
        (es->geometry.width > 0) && (es->geometry.height > 0)) {
        weston_surface_geometry_dirty(es);
        weston_surface_damage(es);
    }
    if (par >= 100) {
        /* end of animation     */
        animadata->geometry_saved ++;       /* restore geometry     */
        animation_end(usurf, 1);
        uifw_trace("animation_slide: End of animation");
    }
    else    {
        /* continue animation   */
        weston_compositor_schedule_repaint(weston_ec);
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   animation_fade: fade animation
 *
 * @param[in]   animation   weston animation table
 * @param[in]   outout      weston output table
 * @param[in]   mseces      current time(unused)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
animation_fade(struct weston_animation *animation,
               struct weston_output *output, uint32_t msecs)
{
    struct uifw_win_surface *usurf;
    struct animation_data   *animadata;
    struct weston_surface   *es;
    int         par;

    usurf = container_of(animation, struct uifw_win_surface, animation.animation);

    par = animation_cont(animation, output, msecs);
    if (par > 0)    {
        /* continue animation   */
        if( par <= 100) {
            weston_compositor_schedule_repaint(weston_ec);
        }
        return;
    }

    animadata = (struct animation_data *)usurf->animation.animadata;
    es = usurf->surface;
    par = usurf->animation.current;
    if (animation->frame_counter == 1)  {
        if (animadata->transform_set == 0)  {
            animadata->transform_set = 1;
            weston_matrix_init(&animadata->transform.matrix);
            wl_list_init(&animadata->transform.link);
            wl_list_insert(&es->geometry.transformation_list,
                           &animadata->transform.link);
        }
        animadata->end_function = animation_fade_end;
    }

    uifw_trace("animation_fade: usurf=%08x count=%d %d%% name=%d state=%d",
               (int)usurf, animation->frame_counter, par,
               usurf->animation.name, usurf->animation.state);

    if (usurf->animation.state == ICO_WINDOW_MGR_ANIMATION_STATE_SHOW)    {
        /* fade in                  */
        es->alpha = ((double)par) / ((double)100.0);
    }
    else    {
        /* fade out                 */
        es->alpha = ((double)1.0) - (((double)par) / ((double)100.0));
    }
    if (es->alpha < 0.0)        es->alpha = 0.0;
    else if (es->alpha > 1.0)   es->alpha = 1.0;

    if ((es->output) && (es->buffer_ref.buffer) &&
        (es->geometry.width > 0) && (es->geometry.height > 0)) {
        weston_surface_damage(es);
    }
    if (par >= 100) {
        /* end of animation     */
        animation_end(usurf, 1);
        uifw_trace("animation_fade: End of animation");
    }
    else    {
        /* continue animation   */
        weston_compositor_schedule_repaint(weston_ec);
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   animation_fade_end: fade animation end
 *
 * @param[in]   animation   weston animation table
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
animation_fade_end(struct weston_animation *animation)
{
    struct uifw_win_surface *usurf;
    struct weston_surface   *es;

    usurf = container_of(animation, struct uifw_win_surface, animation.animation);
    es = usurf->surface;
    es->alpha = 1.0;

    if ((es->output) && (es->buffer_ref.buffer) &&
        (es->geometry.width > 0) && (es->geometry.height > 0)) {
        weston_surface_damage(es);
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   module_init: initialize ico_window_animation
 *                       this function called from ico_pluign_loader
 *
 * @param[in]   es          weston compositor
 * @param[in]   argc        number of arguments(unused)
 * @param[in]   argv        argument list(unused)
 * @return      result
 * @retval      0           sccess
 * @retval      -1          error
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT int
module_init(struct weston_compositor *ec, int *argc, char *argv[])
{
    int     i;
    struct animation_data   *animadata;

    uifw_info("ico_window_animation: Enter(module_init)");

    /* allocate animation datas     */
    free_data = NULL;
    for (i = 0; i < 50; i++)    {
        animadata = (struct animation_data *)malloc(sizeof(struct animation_data));
        if (! animadata)    {
            uifw_error("ico_window_animation: No Memory(module_init)");
            return -1;
        }
        animadata->next_free = free_data;
        free_data = animadata;
    }

    weston_ec = ec;
    default_animation = (char *)ico_ivi_default_animation_name();
    animation_time = ico_ivi_default_animation_time();
    animation_fpar = ico_ivi_default_animation_fps();

    animation_fpar = ((1000 * 100) / animation_fpar) / animation_time;

    ico_window_mgr_set_hook_animation(ico_window_animation);

    uifw_info("ico_window_animation: Leave(module_init)");

    return 0;
}

