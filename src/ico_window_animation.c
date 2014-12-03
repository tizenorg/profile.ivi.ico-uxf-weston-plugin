/*
 * Copyright © 2010-2011 Intel Corporation
 * Copyright © 2008-2011 Kristian Høgsberg
 * Copyright © 2013-2014 TOYOTA MOTOR CORPORATION.
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
 * @date    Feb-21-2014
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
#include <weston/ivi-layout-export.h>
#include "ico_ivi_common_private.h"
#include "ico_window_mgr_private.h"

/* Animation type               */
#define ANIMA_ZOOM              1           /* ZoomIn/ZoomOut                       */
#define ANIMA_FADE              2           /* FadeIn/FadeOut                       */
#define ANIMA_SLIDE_TORIGHT    11           /* SlideIn left to right/SlideOut right to left */
#define ANIMA_SLIDE_TOLEFT     12           /* SlideIn right to left/SlideOut left to right */
#define ANIMA_SLIDE_TOBOTTOM   13           /* SlideIn top to bottom/SlideOut bottom to top */
#define ANIMA_SLIDE_TOTOP      14           /* SlideIn bottom to top/SlideOut top to bottom */
#define ANIMA_WIPE_TORIGHT     21           /* WipeIn left to right/WipeOut right to left   */
#define ANIMA_WIPE_TOLEFT      22           /* WipeIn right to left/WipeOut left to right   */
#define ANIMA_WIPE_TOBOTTOM    23           /* WipeIn top to bottom/WipeOut bottom to top   */
#define ANIMA_WIPE_TOTOP       24           /* WipeIn bottom to top/WipeOut top to bottom   */
#define ANIMA_SWING_TORIGHT    31           /* SwingIn left to right/SwingOut right to left */
#define ANIMA_SWING_TOLEFT     32           /* SwingIn right to left/SwingOut left to right */
#define ANIMA_SWING_TOBOTTOM   33           /* SwingIn top to bottom/SwingOut bottom to top */
#define ANIMA_SWING_TOTOP      34           /* SwingIn bottom to top/SwingOut top to bottom */

/* animation data               */
struct animation_data   {
    struct animation_data   *next_free;     /* free data list                       */
    char    transform_set;                  /* need transform reset at end          */
    char    res[3];                         /* (unused)                             */
    struct weston_transform transform;      /* transform matrix                     */
    void    (*end_function)(struct weston_animation *animation);
                                            /* animation end function               */
};

/* static valiables             */
static struct weston_compositor *weston_ec; /* Weston compositor                    */
static char *default_animation;             /* default animation name               */
static int  animation_fps;                  /* animation frame rate(frame/sec)      */
static int  animation_time;                 /* default animation time(ms)           */
static int  animation_count;                /* current number of animations         */
static struct animation_data    *free_data; /* free data list                       */

/* support animation names      */
static const struct _supprt_animaetions {
    char    *name;
    int     animaid;
}               supprt_animaetions[] = {
    { "fade", ANIMA_FADE },
    { "zoom", ANIMA_ZOOM },
    { "slide", ANIMA_SLIDE_TOTOP },
    { "slide.toleft", ANIMA_SLIDE_TOLEFT },
    { "slide.toright", ANIMA_SLIDE_TORIGHT },
    { "slide.totop", ANIMA_SLIDE_TOTOP },
    { "slide.tobottom", ANIMA_SLIDE_TOBOTTOM },
    { "wipe", ANIMA_WIPE_TOTOP },
    { "wipe.toleft", ANIMA_WIPE_TOLEFT },
    { "wipe.toright", ANIMA_WIPE_TORIGHT },
    { "wipe.totop", ANIMA_WIPE_TOTOP },
    { "wipe.tobottom", ANIMA_WIPE_TOBOTTOM },
    { "swing", ANIMA_SWING_TOTOP },
    { "swing.toleft", ANIMA_SWING_TOLEFT },
    { "swing.toright", ANIMA_SWING_TORIGHT },
    { "swing.totop", ANIMA_SWING_TOTOP },
    { "swing.tobottom", ANIMA_SWING_TOBOTTOM },
    { "\0", -1 }
};

/* static function              */
                                            /* slide animation                      */
static void animation_slide(struct weston_animation *animation,
                            struct weston_output *output, uint32_t msecs);
                                            /* wipe animation                       */
static void animation_wipe(struct weston_animation *animation,
                           struct weston_output *output, uint32_t msecs);
                                            /* swing animation                      */
static void animation_swing(struct weston_animation *animation,
                            struct weston_output *output, uint32_t msecs);
                                            /* swing animation end                  */
static void animation_swing_end(struct weston_animation *animation);
                                            /* fade animation                       */
static void animation_fade(struct weston_animation *animation,
                           struct weston_output *output, uint32_t msecs);
                                            /* fade animation end                   */
static void animation_fade_end(struct weston_animation *animation);
                                            /* slide animation end                  */
static void animation_slide_end(struct weston_animation *animation);
                                            /* zoom animation                       */
static void animation_zoom(struct weston_animation *animation,
                           struct weston_output *output, uint32_t msecs);
                                            /* zoom animation end                   */
static void animation_zoom_end(struct weston_animation *animation);
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
 * @retval      ICO_WINDOW_MGR_ANIMATION_RET_ANIMANOCTL success(no control)
 * @retval      ICO_WINDOW_MGR_ANIMATION_RET_NOANIMA    error(no animation)
 */
/*--------------------------------------------------------------------------*/
static int
ico_window_animation(const int op, void *data)
{
    struct uifw_win_surface *usurf;
    struct weston_output *output;
    int         idx;
    int         ret;
    uint32_t    nowsec;
    int         animaid;

    if (op == ICO_WINDOW_MGR_ANIMATION_NAME)    {
        /* convert animation name to animation type value   */
        for (idx = 0; supprt_animaetions[idx].animaid > 0; idx++)   {
            if (strcasecmp(supprt_animaetions[idx].name, (char *)data) == 0)    {
                uifw_trace("ico_window_animation: Type %s(%d)",
                           (char *)data, supprt_animaetions[idx].animaid);
                return supprt_animaetions[idx].animaid;
            }
        }
        uifw_warn("ico_window_animation: Unknown Type %s", (char *)data);
        return ICO_WINDOW_MGR_ANIMATION_RET_NOANIMA;
    }

    usurf = (struct uifw_win_surface *)data;

    if (op == ICO_WINDOW_MGR_ANIMATION_DESTROY) {
        if ((usurf->animation.state != ICO_WINDOW_MGR_ANIMATION_STATE_NONE) ||
            (usurf->animation.animadata != NULL)) {
            uifw_trace("ico_window_animation: Destroy %08x", usurf->surfaceid);
            animation_end(usurf, 0);
        }
        return ICO_WINDOW_MGR_ANIMATION_RET_ANIMA;
    }

    usurf->animation.visible = ICO_WINDOW_MGR_ANIMA_NOCONTROL_AT_END;

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
            usurf->animation.ahalf = 0;
            if (usurf->animation.state == ICO_WINDOW_MGR_ANIMATION_STATE_NONE)  {
                wl_list_init(&usurf->animation.animation.link);
                output = container_of(weston_ec->output_list.next,
                                      struct weston_output, link);
                wl_list_insert(output->animation_list.prev,
                               &usurf->animation.animation.link);
                animation_count ++;
            }
        }
        else if (((usurf->animation.state == ICO_WINDOW_MGR_ANIMATION_STATE_SHOW) &&
                  ((op == ICO_WINDOW_MGR_ANIMATION_OPHIDE) ||
                   (op == ICO_WINDOW_MGR_ANIMATION_OPHIDEPOS))) ||
                 ((usurf->animation.state == ICO_WINDOW_MGR_ANIMATION_STATE_HIDE) &&
                  ((op == ICO_WINDOW_MGR_ANIMATION_OPSHOW) ||
                   (op == ICO_WINDOW_MGR_ANIMATION_OPSHOWPOS))))   {
            /* change ...In(ex.FadeIn) to ...Out(FadeOut) or ...Out to ...In    */
            nowsec = weston_compositor_get_time();
            usurf->animation.current = 100 - usurf->animation.current;
            if ((op == ICO_WINDOW_MGR_ANIMATION_OPHIDE)||
                (op == ICO_WINDOW_MGR_ANIMATION_OPHIDEPOS)) {
                usurf->animation.time = usurf->animation.hide_time;
            }
            else    {
                usurf->animation.time = usurf->animation.show_time;
            }
            if (usurf->animation.time == 0) {
                usurf->animation.time = animation_time;
            }
            ret = ((usurf->animation.current) * usurf->animation.time) / 100;
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
        if ((op == ICO_WINDOW_MGR_ANIMATION_OPSHOW) ||
            (op == ICO_WINDOW_MGR_ANIMATION_OPSHOWPOS)) {
            usurf->animation.state = ICO_WINDOW_MGR_ANIMATION_STATE_SHOW;
            animaid = usurf->animation.show_anima;
            usurf->animation.anima = animaid;
            uifw_trace("ico_window_animation: show(in) %s.%08x anima=%d",
                       usurf->uclient->appid, usurf->surfaceid, animaid);
            ret = ICO_WINDOW_MGR_ANIMATION_RET_ANIMA;
        }
        else if ((op == ICO_WINDOW_MGR_ANIMATION_OPHIDE) ||
                 (op == ICO_WINDOW_MGR_ANIMATION_OPHIDEPOS))    {
            usurf->animation.state = ICO_WINDOW_MGR_ANIMATION_STATE_HIDE;
            animaid = usurf->animation.hide_anima;
            usurf->animation.anima = animaid;
            uifw_trace("ico_window_animation: hide(out) %s.%08x anima=%d",
                       usurf->uclient->appid, usurf->surfaceid, animaid);
            ret = ICO_WINDOW_MGR_ANIMATION_RET_ANIMANOCTL;
            usurf->animation.visible = ICO_WINDOW_MGR_ANIMA_HIDE_AT_END;
        }
        else if (op == ICO_WINDOW_MGR_ANIMATION_OPMOVE)    {
            usurf->animation.state = ICO_WINDOW_MGR_ANIMATION_STATE_MOVE;
            animaid = usurf->animation.move_anima;
            usurf->animation.anima = animaid;
            uifw_trace("ico_window_animation: move %s.%08x anima=%d",
                       usurf->uclient->appid, usurf->surfaceid, animaid);
            ret = ICO_WINDOW_MGR_ANIMATION_RET_ANIMANOCTL;
        }
        else if (op == ICO_WINDOW_MGR_ANIMATION_OPRESIZE)    {
            usurf->animation.state = ICO_WINDOW_MGR_ANIMATION_STATE_RESIZE;
            animaid = usurf->animation.resize_anima;
            usurf->animation.anima = animaid;
            uifw_trace("ico_window_animation: resize %s.%08x anima=%d",
                       usurf->uclient->appid, usurf->surfaceid, animaid);
            ret = ICO_WINDOW_MGR_ANIMATION_RET_ANIMANOCTL;
        }
        else    {
            uifw_trace("ico_window_animation: Op=%d unknown", op);
            return ICO_WINDOW_MGR_ANIMATION_RET_NOANIMA;
        }
        if ((animaid == ANIMA_SLIDE_TOLEFT) || (animaid == ANIMA_SLIDE_TORIGHT) ||
            (animaid == ANIMA_SLIDE_TOTOP) || (animaid == ANIMA_SLIDE_TOBOTTOM))  {
            usurf->animation.animation.frame = animation_slide;
            usurf->restrain_configure = 1;
            (*usurf->animation.animation.frame)(&usurf->animation.animation, NULL, 1);
        }
        else if ((animaid == ANIMA_WIPE_TOLEFT) || (animaid == ANIMA_WIPE_TORIGHT) ||
                 (animaid == ANIMA_WIPE_TOTOP) || (animaid == ANIMA_WIPE_TOBOTTOM))  {
            usurf->animation.animation.frame = animation_wipe;
            usurf->restrain_configure = 1;
            (*usurf->animation.animation.frame)(&usurf->animation.animation, NULL, 1);
        }
        else if ((animaid == ANIMA_SWING_TOLEFT) || (animaid == ANIMA_SWING_TORIGHT) ||
                 (animaid == ANIMA_SWING_TOTOP) || (animaid == ANIMA_SWING_TOBOTTOM))  {
            usurf->animation.animation.frame = animation_swing;
            usurf->restrain_configure = 1;
            (*usurf->animation.animation.frame)(&usurf->animation.animation, NULL, 1);
        }
        else if (animaid == ANIMA_FADE)   {
            usurf->animation.animation.frame = animation_fade;
            usurf->restrain_configure = 1;
            (*usurf->animation.animation.frame)(&usurf->animation.animation, NULL, 1);
        }
        else if (animaid == ANIMA_ZOOM)   {
            usurf->animation.animation.frame = animation_zoom;
            usurf->restrain_configure = 1;
            (*usurf->animation.animation.frame)(&usurf->animation.animation, NULL, 1);
        }
        else    {
            /* no yet support   */
            usurf->animation.animation.frame = NULL;
            usurf->animation.state = ICO_WINDOW_MGR_ANIMATION_STATE_NONE;
            usurf->restrain_configure = 0;
            usurf->animation.anima = 0;
            wl_list_remove(&usurf->animation.animation.link);
            ret = ICO_WINDOW_MGR_ANIMATION_RET_NOANIMA;
            if (usurf->org_animation.saved) {
                usurf->animation.type = usurf->org_animation.type;
                usurf->animation.anima = usurf->org_animation.anima;
                usurf->animation.next_anima = usurf->org_animation.next_anima;
                usurf->animation.hide_anima = usurf->org_animation.hide_anima;
                usurf->animation.hide_time = usurf->org_animation.hide_time;
                usurf->animation.show_anima = usurf->org_animation.show_anima;
                usurf->animation.show_time = usurf->org_animation.show_time;
                usurf->animation.move_anima = usurf->org_animation.move_anima;
                usurf->animation.move_time = usurf->org_animation.move_time;
                usurf->animation.resize_anima = usurf->org_animation.resize_anima;
                usurf->animation.resize_time = usurf->org_animation.resize_time;
                usurf->org_animation.saved = 0;
            }
        }
        usurf->animation.type = op;
#if  PERFORMANCE_EVALUATIONS > 0
        if (ret != ICO_WINDOW_MGR_ANIMATION_RET_NOANIMA)    {
            uifw_perf("SWAP_BUFFER Start Animation appid=%s surface=%08x anima=%d",
                      usurf->uclient->appid, usurf->surfaceid, usurf->animation.anima);
        }
#endif /*PERFORMANCE_EVALUATIONS*/
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
    int         par;
    uint32_t    nowsec;

    nowsec = weston_compositor_get_time();

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
        usurf->animation.time = usurf->animation.show_time;
        break;
    case ICO_WINDOW_MGR_ANIMATION_STATE_HIDE:
        usurf->animation.time = usurf->animation.hide_time;
        break;
    case ICO_WINDOW_MGR_ANIMATION_STATE_MOVE:
        usurf->animation.time = usurf->animation.move_time;
        break;
    default:
        usurf->animation.time = usurf->animation.resize_time;
        break;
    }
    if (usurf->animation.time == 0) {
        usurf->animation.time = animation_time;
    }
    if (((output == NULL) && (msecs == 0)) || (nowsec >= ((uint32_t)usurf->animation.time))) {
        par = 100;
    }
    else    {
        par = (nowsec * 100 + usurf->animation.time / 2) / usurf->animation.time;
        if (par < 2)    par = 2;
    }
    if ((par >= 100) ||
        (abs(usurf->animation.current - par) >=
         (((1000 * 100) / animation_fps) / usurf->animation.time)) ||
        ((animation_count > 1) && (par != usurf->animation.current)))   {
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
    struct weston_view      *ev;

    usurf->animation.state = ICO_WINDOW_MGR_ANIMATION_STATE_NONE;
    if (usurf->org_animation.saved) {
        usurf->animation.type = usurf->org_animation.type;
        usurf->animation.anima = usurf->org_animation.anima;
        usurf->animation.next_anima = usurf->org_animation.next_anima;
        usurf->animation.hide_anima = usurf->org_animation.hide_anima;
        usurf->animation.hide_time = usurf->org_animation.hide_time;
        usurf->animation.show_anima = usurf->org_animation.show_anima;
        usurf->animation.show_time = usurf->org_animation.show_time;
        usurf->animation.move_anima = usurf->org_animation.move_anima;
        usurf->animation.move_time = usurf->org_animation.move_time;
        usurf->animation.resize_anima = usurf->org_animation.resize_anima;
        usurf->animation.resize_time = usurf->org_animation.resize_time;
        usurf->org_animation.saved = 0;
    }
    animadata = (struct animation_data *)usurf->animation.animadata;

    if (animation_count > 0)    {
        animation_count --;
    }
    ev = ico_ivi_get_primary_view(usurf);

    if (animadata)  {
        if (animadata->end_function)    {
            (*animadata->end_function)(&usurf->animation.animation);
        }
        wl_list_remove(&usurf->animation.animation.link);
        if (animadata->transform_set)   {
            wl_list_remove(&animadata->transform.link);
            animadata->transform_set = 0;
        }
        if (ev) {
            weston_view_geometry_dirty(ev);
        }
    }
    if (disp)   {
        uifw_trace("animation_end: %08x vis=%d(%x)",
                   usurf->surfaceid, usurf->visible, usurf->animation.visible);
        usurf->internal_propchange |= 0x10;
        if ((usurf->animation.visible == ICO_WINDOW_MGR_ANIMA_HIDE_AT_END) &&
            (usurf->visible != 0))  {
            usurf->visible = 0;
            ivi_layout_surfaceSetVisibility(usurf->ivisurf, 0);
            ivi_layout_commitChanges();
            weston_surface_damage(usurf->surface);
            if (ev) {
                weston_view_geometry_dirty(ev);
            }
        }
        if ((usurf->animation.visible == ICO_WINDOW_MGR_ANIMA_SHOW_AT_END) &&
            (usurf->visible == 0))  {
            usurf->visible = 1;
            ivi_layout_surfaceSetVisibility(usurf->ivisurf, 1);
            ivi_layout_commitChanges();
            weston_surface_damage(usurf->surface);
            if (ev) {
                weston_view_geometry_dirty(ev);
            }
        }
        usurf->internal_propchange &= ~0x10;
        usurf->restrain_configure = 0;
    }
    usurf->animation.visible = ICO_WINDOW_MGR_ANIMA_NOCONTROL_AT_END;
    if (usurf->animation.next_anima != ICO_WINDOW_MGR_ANIMATION_NONE)    {
        switch(usurf->animation.type)   {
        case ICO_WINDOW_MGR_ANIMATION_OPHIDE:
        case ICO_WINDOW_MGR_ANIMATION_OPHIDEPOS:
            usurf->animation.hide_anima = usurf->animation.next_anima;
            break;
        case ICO_WINDOW_MGR_ANIMATION_OPSHOW:
        case ICO_WINDOW_MGR_ANIMATION_OPSHOWPOS:
            usurf->animation.show_anima = usurf->animation.next_anima;
            break;
        case ICO_WINDOW_MGR_ANIMATION_OPMOVE:
            usurf->animation.move_anima = usurf->animation.next_anima;
            break;
        case ICO_WINDOW_MGR_ANIMATION_OPRESIZE:
            usurf->animation.resize_anima = usurf->animation.next_anima;
            break;
        default:
            break;
        }
        usurf->animation.next_anima = ICO_WINDOW_MGR_ANIMATION_NONE;
    }
    if (animadata)   {
        usurf->animation.animadata = NULL;
        animadata->next_free = free_data;
        free_data = animadata;
    }
    usurf->animation.type = ICO_WINDOW_MGR_ANIMATION_OPNONE;
#if  PERFORMANCE_EVALUATIONS > 0
    uifw_perf("SWAP_BUFFER End Animation appid=%s surface=%08x anima=%d",
              usurf->uclient->appid, usurf->surfaceid, usurf->animation.anima);
#endif /*PERFORMANCE_EVALUATIONS*/
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
    const struct ivi_layout_surface_properties  *prop;
    int         dwidth, dheight;
    int         par, x, y;

    usurf = container_of(animation, struct uifw_win_surface, animation.animation);

    par = animation_cont(animation, output, msecs);
    if (par > 0)    {
        /* continue animation   */
        if( par <= 100) {
            weston_compositor_schedule_repaint(weston_ec);
        }
        return;
    }
    if (animation->frame_counter == 1)  {
        animadata = (struct animation_data *)usurf->animation.animadata;
        animadata->end_function = animation_slide_end;
    }
    par = usurf->animation.current;

    uifw_debug("animation_slide: %08x count=%d %d%% anima=%d state=%d",
               usurf->surfaceid, animation->frame_counter, par,
               usurf->animation.anima, usurf->animation.state);

    x = usurf->x;
    y = usurf->y;

    switch (usurf->animation.anima)  {
    case ANIMA_SLIDE_TORIGHT:           /* slide in left to right           */
        if (usurf->animation.state == ICO_WINDOW_MGR_ANIMATION_STATE_SHOW)    {
            /* slide in left to right   */
            x = 0 - usurf->animation.pos_width +
                ((usurf->animation.pos_x + usurf->animation.pos_width) * par / 100);
        }
        else    {
            /* slide out right to left  */
            x = 0 - usurf->animation.pos_width +
                ((usurf->animation.pos_x + usurf->animation.pos_width) * (100 - par) / 100);
        }
        break;
    case ANIMA_SLIDE_TOLEFT:            /* slide in right to left           */
        dwidth = usurf->node_tbl->disp_width;
        if (usurf->animation.state == ICO_WINDOW_MGR_ANIMATION_STATE_SHOW)    {
            /* slide in right to left   */
            x = usurf->animation.pos_x +
                (dwidth - usurf->animation.pos_x) * (100 - par) / 100;
        }
        else    {
            /* slide out left to right  */
            x = usurf->animation.pos_x + (dwidth - usurf->animation.pos_x) * par / 100;
        }
        break;
    case ANIMA_SLIDE_TOBOTTOM:          /* slide in top to bottom           */
        if (usurf->animation.state == ICO_WINDOW_MGR_ANIMATION_STATE_SHOW)    {
            /* slide in top to bottom   */
            y = 0 - usurf->animation.pos_height +
                ((usurf->animation.pos_y + usurf->animation.pos_height) * par / 100);
        }
        else    {
            /* slide out bottom to top  */
            y = 0 - usurf->animation.pos_height +
                ((usurf->animation.pos_y + usurf->animation.pos_height) * (100 - par) / 100);
        }
        break;
    default: /*ANIMA_SLIDE_TOTOP*/      /* slide in bottom to top           */
        dheight = usurf->node_tbl->disp_height;
        if (usurf->animation.state == ICO_WINDOW_MGR_ANIMATION_STATE_SHOW)    {
            /* slide in bottom to top   */
            y = usurf->animation.pos_y +
                (dheight - usurf->animation.pos_y) * (100 - par) / 100;
        }
        else    {
            /* slide out top to bottom  */
            y = usurf->animation.pos_y + (dheight - usurf->animation.pos_y) * par / 100;
        }
        break;
    }
    if ((par < 8) || (par > 92))    {
        uifw_debug("animation_slide: %08x %d%% %d/%d(target %d/%d) %08x",
                   usurf->surfaceid, par, x, y, usurf->x, usurf->y, (int)usurf->ivisurf);
    }
    if ((prop = ivi_layout_get_properties_of_surface(usurf->ivisurf)))   {
        usurf->internal_propchange |= 0x20;
        if (ivi_layout_surface_set_destination_rectangle(usurf->ivisurf, x, y,
                                             prop->dest_width, prop->dest_height) == 0) {
            ivi_layout_commitChanges();
        }
        usurf->internal_propchange &= ~0x20;
    }
    if (par >= 100) {
        /* end of animation     */
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
 * @brief   animation_slide_end: slide animation end
 *
 * @param[in]   animation   weston animation table
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
animation_slide_end(struct weston_animation *animation)
{
    struct uifw_win_surface *usurf;

    usurf = container_of(animation, struct uifw_win_surface, animation.animation);
    if (usurf)  {
        ico_window_mgr_set_weston_surface(usurf,
                                          usurf->animation.pos_x, usurf->animation.pos_y,
                                          usurf->animation.pos_width,
                                          usurf->animation.pos_height);
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   animation_wipe: wipe animation
 *
 * @param[in]   animation   weston animation table
 * @param[in]   outout      weston output table
 * @param[in]   mseces      current time(unused)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
animation_wipe(struct weston_animation *animation,
               struct weston_output *output, uint32_t msecs)
{
    struct uifw_win_surface *usurf;
    struct weston_surface   *es;
    struct weston_view      *ev;
    int         par;
    int         x;
    int         y;
    int         width;
    int         height;

    usurf = container_of(animation, struct uifw_win_surface, animation.animation);

    par = animation_cont(animation, output, msecs);
    if (par > 0)    {
        /* continue animation   */
        if( par <= 100) {
            weston_compositor_schedule_repaint(weston_ec);
        }
        return;
    }
    ev = ico_ivi_get_primary_view(usurf);
    par = usurf->animation.current;

    uifw_debug("animation_wipe: %08x count=%d %d%% anima=%d state=%d",
               usurf->surfaceid, animation->frame_counter, par,
               usurf->animation.anima, usurf->animation.state);

    es = usurf->surface;
    x = usurf->x;
    y = usurf->y;
    width = usurf->width;
    height = usurf->width;

    if (par < 100)  {
        switch (usurf->animation.anima)  {
        case ANIMA_WIPE_TORIGHT:            /* wipe in left to right            */
            if (usurf->animation.state == ICO_WINDOW_MGR_ANIMATION_STATE_SHOW)    {
                /* wipe in left to right    */
                width = ((float)width) * ((float)par + 5.0f) / 105.0f;
            }
            else    {
                /* wipe out right to left   */
                width = width - (((float)width) * ((float)par + 5.0f) / 105.0f);
            }
            if (width <= 0) width = 1;
            break;
        case ANIMA_WIPE_TOLEFT:             /* wipe in right to left            */
            if (usurf->animation.state == ICO_WINDOW_MGR_ANIMATION_STATE_SHOW)    {
                /* wipe in right to left    */
                width = ((float)width) * ((float)par + 5.0f) / 105.0f;
            }
            else    {
                /* wipe out left to right   */
                width = width - (((float)width) * ((float)par + 5.0f) / 105.0f);
            }
            if (width <= 0) width = 1;
            x = x + (usurf->width - width);
            break;
        case ANIMA_WIPE_TOBOTTOM:           /* wipe in top to bottom            */
            if (usurf->animation.state == ICO_WINDOW_MGR_ANIMATION_STATE_SHOW)    {
                /* wipe in top to bottom    */
                height = ((float)height) * ((float)par + 5.0f) / 105.0f;
            }
            else    {
                /* wipe out bottom to top   */
                height = height - (((float)height) * ((float)par + 5.0f) / 105.0f);
            }
            if (height <= 0)    height = 1;
            break;
        default: /*ANIMA_WIPE_TOTOP*/       /* wipe in bottom to top            */
            if (usurf->animation.state == ICO_WINDOW_MGR_ANIMATION_STATE_SHOW)    {
                /* wipe in bottom to top    */
                height = ((float)height) * ((float)par + 5.0f) / 105.0f;
            }
            else    {
                /* wipe out top to bottom   */
                height = height - (((float)height) * ((float)par + 5.0f) / 105.0f);
            }
            if (height <= 0)    height = 1;
            y = y + (usurf->height - height);
            break;
        }
    }

    es->width = width;
    es->height = height;
    if (ev) {
        ev->geometry.x = usurf->node_tbl->disp_x + x;
        ev->geometry.y = usurf->node_tbl->disp_y + y;
        if ((ev->output) && (es->buffer_ref.buffer))    {
            weston_view_geometry_dirty(ev);
            weston_surface_damage(es);
        }
    }
    if (par >= 100) {
        /* end of animation     */
        animation_end(usurf, 1);
        uifw_trace("animation_wipe: End of animation");
    }
    else    {
        /* continue animation   */
        weston_compositor_schedule_repaint(weston_ec);
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   animation_swing: swing animation
 *
 * @param[in]   animation   weston animation table
 * @param[in]   outout      weston output table
 * @param[in]   mseces      current time(unused)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
animation_swing(struct weston_animation *animation,
                struct weston_output *output, uint32_t msecs)
{
    struct uifw_win_surface *usurf;
    struct weston_surface   *es;
    struct weston_view      *ev;
    struct animation_data   *animadata;
    int         par;
    int         x;
    int         y;
    float       scalex;
    float       scaley;

    usurf = container_of(animation, struct uifw_win_surface, animation.animation);

    par = animation_cont(animation, output, msecs);
    if (par > 0)    {
        /* continue animation   */
        if( par <= 100) {
            weston_compositor_schedule_repaint(weston_ec);
        }
        return;
    }

    uifw_debug("animation_swing: %08x count=%d %d%% anima=%d state=%d",
               usurf->surfaceid, animation->frame_counter, par,
               usurf->animation.anima, usurf->animation.state);

    animadata = (struct animation_data *)usurf->animation.animadata;
    es = usurf->surface;
    ev = ico_ivi_get_primary_view(usurf);
    par = usurf->animation.current;
    if (animation->frame_counter == 1)  {
        if (animadata->transform_set == 0)  {
            animadata->transform_set = 1;
            weston_matrix_init(&animadata->transform.matrix);
            wl_list_init(&animadata->transform.link);
            if (ev) {
                wl_list_insert(&ev->geometry.transformation_list,
                               &animadata->transform.link);
            }
        }
        animadata->end_function = animation_swing_end;
    }

    x = usurf->x;
    y = usurf->y;
    scalex = 1.0;
    scaley = 1.0;

    if (par < 100)  {
        switch (usurf->animation.anima)  {
        case ANIMA_SWING_TORIGHT:           /* swing in left to right           */
            if (usurf->animation.state == ICO_WINDOW_MGR_ANIMATION_STATE_SHOW)    {
                /* swing in left to right   */
                scalex = ((float)par + 5.0f) / 105.0f;
            }
            else    {
                /* swing out right to left  */
                scalex = 1.0 - (((float)par + 5.0f) / 105.0f);
            }
            if (scalex <= 0.0)  scalex = 0.01;
            break;
        case ANIMA_SWING_TOLEFT:            /* seing in right to left           */
            if (usurf->animation.state == ICO_WINDOW_MGR_ANIMATION_STATE_SHOW)    {
                /* swing in right to left   */
                scalex = ((float)par + 5.0f) / 105.0f;
            }
            else    {
                /* swing out left to right  */
                scalex = 1.0 - (((float)par + 5.0f) / 105.0f);
            }
            if (scalex <= 0.0)  scalex = 0.01;
            x = x + (int)((float)usurf->width * (1.0f - scalex));
            break;
        case ANIMA_SWING_TOBOTTOM:          /* swing in top to bottom           */
            if (usurf->animation.state == ICO_WINDOW_MGR_ANIMATION_STATE_SHOW)    {
                /* swing in top to bottom   */
                scaley = ((float)par + 5.0f) / 105.0f;
            }
            else    {
                /* swing out bottom to top  */
                scalex = 1.0 - (((float)par + 5.0f) / 105.0f);
            }
            if (scaley <= 0.0)  scaley = 0.01;
            break;
        default: /*ANIMA_SWING*/        /* swing in bottom to top               */
            if (usurf->animation.state == ICO_WINDOW_MGR_ANIMATION_STATE_SHOW)    {
                /* wipe in bottom to top    */
                scaley = ((float)par + 5.0f) / 105.0f;
            }
            else    {
                /* wipe out top to bottom   */
                scalex = 1.0 - (((float)par + 5.0f) / 105.0f);
            }
            if (scaley <= 0.0)  scaley = 0.01;
            y = y + (int)((float)usurf->height * (1.0f - scaley));
            break;
        }
    }

    weston_matrix_init(&animadata->transform.matrix);
    weston_matrix_translate(&animadata->transform.matrix,
                            -0.5f * usurf->width, -0.5f * usurf->height, 0);
    weston_matrix_scale(&animadata->transform.matrix, scalex, scaley, 1.0f);
    weston_matrix_translate(&animadata->transform.matrix,
                            0.5f * usurf->width, 0.5f * usurf->height, 0);

    if (ev) {
        ev->geometry.x = usurf->node_tbl->disp_x + x;
        ev->geometry.y = usurf->node_tbl->disp_y + y;
        if ((ev->output) && (es->buffer_ref.buffer))    {
            weston_view_geometry_dirty(ev);
            weston_surface_damage(es);
        }
    }
    if (par >= 100) {
        /* end of animation     */
        animation_end(usurf, 1);
        uifw_trace("animation_swing: End of animation");
    }
    else    {
        /* continue animation   */
        weston_compositor_schedule_repaint(weston_ec);
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   animation_swing_end: swing animation end
 *
 * @param[in]   animation   weston animation table
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
animation_swing_end(struct weston_animation *animation)
{
    struct uifw_win_surface *usurf;
    struct weston_surface   *es;
    struct weston_view      *ev;

    usurf = container_of(animation, struct uifw_win_surface, animation.animation);
    if (usurf && usurf->surface)    {
        es = usurf->surface;
        ev = ico_ivi_get_primary_view(usurf);
        if (ev) {
            ev->alpha = usurf->animation.alpha;
            uifw_debug("animation_swing_end: %08x set alpha=%.2f",
                       usurf->surfaceid, usurf->animation.alpha);
            if ((ev->output) && (es->buffer_ref.buffer))    {
                weston_surface_damage(es);
            }
        }
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
    struct weston_view      *ev;
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
    ev = ico_ivi_get_primary_view(usurf);
    par = usurf->animation.current;
    if (animation->frame_counter == 1)  {
        if (animadata->transform_set == 0)  {
            animadata->transform_set = 1;
            weston_matrix_init(&animadata->transform.matrix);
            wl_list_init(&animadata->transform.link);
            if (ev) {
                wl_list_insert(&ev->geometry.transformation_list,
                               &animadata->transform.link);
            }
        }
        animadata->end_function = animation_fade_end;

        if ((usurf->animation.type == ICO_WINDOW_MGR_ANIMATION_OPHIDEPOS) ||
            (usurf->animation.type == ICO_WINDOW_MGR_ANIMATION_OPSHOWPOS))  {
            ico_window_mgr_set_weston_surface(usurf,
                                              usurf->animation.pos_x,
                                              usurf->animation.pos_y,
                                              usurf->animation.pos_width,
                                              usurf->animation.pos_height);
        }
    }

    if (ev) {
        if (usurf->animation.state == ICO_WINDOW_MGR_ANIMATION_STATE_SHOW)    {
            /* fade in                  */
            ev->alpha = ((float)par) / 100.0f;
        }
        else if (usurf->animation.state == ICO_WINDOW_MGR_ANIMATION_STATE_HIDE)    {
            /* fade out                 */
            ev->alpha = 1.0f - (((float)par) / 100.0f);
        }
        else    {
            /* fade move/resize         */
            if ((par >= 50) || (usurf->animation.ahalf))    {
                ev->alpha = ((float)(par*2 - 100)) / 100.0f;
                if (usurf->animation.ahalf == 0)    {
                    uifw_trace("animation_fade: fade move chaneg to show");
                    usurf->animation.ahalf = 1;
                    ev->alpha = 0.0;
                    ico_window_mgr_set_weston_surface(usurf, usurf->x, usurf->y,
                                                      usurf->width, usurf->height);
                }
            }
            else    {
                ev->alpha = 1.0f - (((float)(par*2)) / 100.0f);
            }
        }
        if (ev->alpha < 0.0f)       ev->alpha = 0.0f;
        else if (ev->alpha > 1.0f)  ev->alpha = 1.0f;

        if ((par < 8) || (par > 92))    {
            uifw_debug("animation_fade: %08x count=%d %d%% alpha=%1.2f anima=%d state=%d",
                       usurf->surfaceid, animation->frame_counter, par,
                       ev->alpha, usurf->animation.anima, usurf->animation.state);
        }
        if ((ev->output) && (es->buffer_ref.buffer) &&
            (es->width > 0) && (es->height > 0))    {
            weston_surface_damage(es);
        }
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
    struct weston_view      *ev;

    usurf = container_of(animation, struct uifw_win_surface, animation.animation);
    if (usurf && usurf->surface)    {
        es = usurf->surface;
        ev = ico_ivi_get_primary_view(usurf);
        if (ev) {
            ev->alpha = usurf->animation.alpha;
            uifw_debug("animation_fade_end: %08x set alpha=%.2f",
                       usurf->surfaceid, usurf->animation.alpha);
            if ((ev->output) && (es->buffer_ref.buffer) &&
                (es->width > 0) && (es->height > 0))    {
                weston_surface_damage(es);
            }
        }
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   animation_zoom: zoom animation
 *
 * @param[in]   animation   weston animation table
 * @param[in]   outout      weston output table
 * @param[in]   mseces      current time(unused)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
animation_zoom(struct weston_animation *animation,
               struct weston_output *output, uint32_t msecs)
{
    struct uifw_win_surface *usurf;
    struct animation_data   *animadata;
    struct weston_surface   *es;
    struct weston_view      *ev;
    int         par;
    float       scalex, scaley;
    float       fu, fa, fp;
    int         x, y;

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
    ev = ico_ivi_get_primary_view(usurf);
    par = usurf->animation.current;
    if (animation->frame_counter == 1)  {
        if (animadata->transform_set == 0)  {
            animadata->transform_set = 1;
            weston_matrix_init(&animadata->transform.matrix);
            wl_list_init(&animadata->transform.link);
            if (ev) {
                wl_list_insert(&ev->geometry.transformation_list,
                               &animadata->transform.link);
            }
        }
        animadata->end_function = animation_zoom_end;

        if ((usurf->animation.type == ICO_WINDOW_MGR_ANIMATION_OPHIDEPOS) ||
            (usurf->animation.type == ICO_WINDOW_MGR_ANIMATION_OPSHOWPOS))  {
            ico_window_mgr_set_weston_surface(usurf,
                                              usurf->animation.pos_x, usurf->animation.pos_y,
                                              usurf->animation.pos_width,
                                              usurf->animation.pos_height);
        }
    }

    if (usurf->animation.state == ICO_WINDOW_MGR_ANIMATION_STATE_SHOW)    {
        /* zoom in                  */
        scalex = ((float)par + 5.0f) / 105.0f;
        scaley = scalex;
    }
    else if (usurf->animation.state == ICO_WINDOW_MGR_ANIMATION_STATE_HIDE)    {
        /* zoom out                 */
        scalex = 1.0f - (((float)par + 5.0f) / 105.0f);
        scaley = scalex;
    }
    else    {
        /* zoom move/resize         */
        ico_window_mgr_set_weston_surface(usurf, usurf->x, usurf->y,
                                          usurf->width, usurf->height);
        fu = (float)usurf->width;
        fa = (float)usurf->animation.pos_width;
        fp = (100.0f - (float)par) / 100.0f;
        scalex = (fu - (fu - fa) * fp) / fu;
        fu = (float)usurf->height;
        fa = (float)usurf->animation.pos_height;
        scaley = (fu - (fu - fa) * fp) / fu;

        x = (((float)usurf->animation.pos_x) - ((float)usurf->x)) * fp + (float)usurf->x
            + (((float)usurf->width * scalex) - (float)usurf->width) / 2.0f;
        y = (((float)usurf->animation.pos_y) - ((float)usurf->y)) * fp + (float)usurf->y
            + (((float)usurf->height * scaley) - (float) usurf->height) / 2.0f;
        uifw_trace("animation_zoom: %08x %d%% x=%d/%d y=%d/%d",
                   usurf->surfaceid, par, x, usurf->x, y, usurf->y);
        uifw_trace("animation_zoom: sx=%4.2f sy=%4.2f x=%d->%d y=%d->%d cur=%d,%d",
                   scalex, scaley, usurf->animation.pos_x, usurf->x,
                   usurf->animation.pos_y, usurf->y, x, y);
        ico_window_mgr_set_weston_surface(usurf, x, y, usurf->width, usurf->height);
    }
    weston_matrix_init(&animadata->transform.matrix);
    weston_matrix_translate(&animadata->transform.matrix,
                            -0.5f * usurf->width, -0.5f * usurf->height, 0);
    weston_matrix_scale(&animadata->transform.matrix, scalex, scaley, 1.0f);
    weston_matrix_translate(&animadata->transform.matrix,
                            0.5f * usurf->width, 0.5f * usurf->height, 0);

    uifw_trace("animation_zoom: %08x count=%d %d%% w=%d/%d h=%d/%d anima=%d state=%d",
               usurf->surfaceid, animation->frame_counter, par,
               (int)(usurf->width * scalex), usurf->width,
               (int)(usurf->height * scaley), usurf->height,
               usurf->animation.anima, usurf->animation.state);

    if (ev) {
        if ((ev->output) && (es->buffer_ref.buffer) &&
            (es->width > 0) && (es->height > 0))    {
            weston_view_geometry_dirty(ev);
            weston_surface_damage(es);
        }
    }
    if (par >= 100) {
        /* end of animation     */
        animation_end(usurf, 1);
        uifw_trace("animation_zoom: End of animation");
    }
    else    {
        /* continue animation   */
        weston_compositor_schedule_repaint(weston_ec);
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   animation_zoom_end: zoom animation end
 *
 * @param[in]   animation   weston animation table
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
animation_zoom_end(struct weston_animation *animation)
{
    struct uifw_win_surface *usurf;
    struct weston_surface   *es;
    struct weston_view      *ev;

    usurf = container_of(animation, struct uifw_win_surface, animation.animation);
    if (usurf && usurf->surface)    {
        es = usurf->surface;
        ev = ico_ivi_get_primary_view(usurf);
        if (ev) {
            ev->alpha = usurf->animation.alpha;
            if ((ev->output) && (es->buffer_ref.buffer) &&
                (es->width > 0) && (es->height > 0))    {
                weston_surface_damage(es);
            }
        }
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
    animation_fps = ico_ivi_default_animation_fps();
    animation_count = 0;

    ico_window_mgr_set_hook_animation(ico_window_animation);

    uifw_info("ico_window_animation: Leave(module_init)");

    return 0;
}
