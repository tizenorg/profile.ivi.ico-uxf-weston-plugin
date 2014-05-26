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
 * @brief   The common functions that each Plugin is available.
 *
 * @date    Feb-21-2014
 */

#ifndef _ICO_IVI_COMMON_PRIVATE_H_
#define _ICO_IVI_COMMON_PRIVATE_H_

/* Log for performance evaluations      */
#define PERFORMANCE_EVALUATIONS 1

/* Macros                               */
#define ICO_IVI_NODEID_2_HOSTID(nodeid)         (((unsigned int)nodeid) >> 8)
#define ICO_IVI_NODEID_2_DISPLAYNO(nodeid)      (((unsigned int)nodeid) & 0x0ff)
#define ICO_IVI_NODEDISP_2_NODEID(nodeid, displayno)  \
                                                ((nodeid << 8) | displayno)
#define ICO_IVI_SURFACEID_2_HOSTID(surfid)      (((unsigned int)surfid) >> 24)
#define ICO_IVI_SURFACEID_2_DISPLAYNO(surfid)   ((((unsigned int)surfid) >> 16) & 0x0ff)
#define ICO_IVI_SURFACEID_2_NODEID(surfid)      (((unsigned int)surfid) >> 16)
#define ICO_IVI_SURFACEID_BASE(nodeid)          (((unsigned int)nodeid) << 16)

/* System limit                         */
#define ICO_IVI_MAX_DISPLAY      (8)        /* Maximum numer of displays in a ECU   */
#define ICO_IVI_APPID_LENGTH     (80)       /* Maximum length of applicationId(AppCore) */
                                            /* (with terminate NULL)                */
#define ICO_IVI_WINNAME_LENGTH   (40)       /* Maximum length of window name (with NULL)*/
#define ICO_IVI_FILEPATH_LENGTH  (80)       /* Maximum length of file path (with NULL)*/
#define ICO_IVI_ANIMATION_LENGTH (16)       /* Maximum length of animation name (w NULL)*/
#define ICO_IVI_MAX_COORDINATE   (16383)    /* Maximum X or Y coordinate            */

/* Fixed value                          */
#ifndef TRUE
#define TRUE    1
#endif
#ifndef FALSE
#define FALSE   0
#endif

/* Option flags                         */
#define ICO_IVI_OPTION_SUPPORT_SHM      0x0010  /* support shm_buffer thumbnail     */
#define ICO_IVI_OPTION_SAVE_BITMAP      0x0100  /* save thumbnail bitmap files      */

/* Debug flags                          */
#define ICO_IVI_DEBUG_PERF_LOG          0x0001  /* performance log                  */

/* Function prototype                   */
int ico_ivi_get_mynode(void);               /* Get my node numner                   */
int ico_ivi_optionflag(void);               /* Get option flag                      */
int ico_ivi_debuglevel(void);               /* Get debug log level                  */
int ico_ivi_debugflag(void);                /* Get debug flag                       */
                                            /* Get default animation name           */
const char *ico_ivi_default_animation_name(void);
int ico_ivi_default_animation_time(void);   /* Get default animation time(ms)       */
int ico_ivi_default_animation_fps(void);    /* Get animation frame rate(fps)        */

/* Debug Traces                         */
/* Define for debug write               */
#define UIFW_DEBUG_OUT  1   /* 1=Debug Print/0=No Debug Print           */

#if UIFW_DEBUG_OUT > 0
#define uifw_perf(fmt,...)  \
    { if (ico_ivi_debugflag() & ICO_IVI_DEBUG_PERF_LOG) {weston_log("PRF>"fmt" (%s:%d)\n",##__VA_ARGS__,__FILE__,__LINE__);} }
#define uifw_debug(fmt,...)  \
    { if (ico_ivi_debuglevel() >= 5) {weston_log("DBG>"fmt" (%s:%d)\n",##__VA_ARGS__,__FILE__,__LINE__);} }
#define uifw_trace(fmt,...)  \
    { if (ico_ivi_debuglevel() >= 4) {weston_log("TRC>"fmt" (%s:%d)\n",##__VA_ARGS__,__FILE__,__LINE__);} }

#ifdef UIFW_DETAIL_OUT
#if UIFW_DETAIL_OUT > 0
#define uifw_detail(fmt,...)  \
    { if (ico_ivi_debuglevel() >= 5) {weston_log("DBG>"fmt" (%s:%d)\n",##__VA_ARGS__,__FILE__,__LINE__);} }
#else
#define uifw_detail(fmt,...)
#endif
#else
#define uifw_detail(fmt,...)
#endif

#else  /*UIFW_DEBUG_OUT*/
#define uifw_perf(fmt,...)
#define uifw_debug(fmt,...)
#define uifw_trace(fmt,...)
#endif /*UIFW_DEBUG_OUT*/

#define uifw_info(fmt,...)  \
    { if (ico_ivi_debuglevel() >= 3) {weston_log("INF>"fmt" (%s:%d)\n",##__VA_ARGS__,__FILE__,__LINE__);} }
#define uifw_warn(fmt,...)  \
    { if (ico_ivi_debuglevel() >= 2) {weston_log("WRN>"fmt" (%s:%d)\n",##__VA_ARGS__,__FILE__,__LINE__);} }
#define uifw_error(fmt,...)  \
    { if (ico_ivi_debuglevel() >= 1) {weston_log("ERR>"fmt" (%s:%d)\n",##__VA_ARGS__,__FILE__,__LINE__);} }

#endif  /*_ICO_IVI_COMMON_PRIVATE_H_*/
