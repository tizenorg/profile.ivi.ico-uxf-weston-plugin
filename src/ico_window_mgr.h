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

/* surface image buffer in wl_shm_pool  */
struct ico_uifw_image_buffer    {
    uint32_t    magich;                     /* Magic number, fixed "UIFH"(no NULL terminate)*/
    uint32_t    surfaceid;                  /* surface id                           */
    uint32_t    settime;                    /* buffer set time(set by weston)       */
    uint32_t    width;                      /* width                                */
    uint32_t    height;                     /* height                               */
    uint32_t    reftime;                    /* buffer refer time(set by app)        */
    uint32_t    res;                        /* (unused)                             */
    uint32_t    magict;                     /* Magic number, fixed "UIFT"(no NULL terminate)*/
    unsigned char image[4];                 /* surface image(variable length)       */
};

#define ICO_UIFW_IMAGE_BUFFER_MAGICH    "UIFH"
#define ICO_UIFW_IMAGE_BUFFER_MAGICT    "UIFT"

#endif  /*_ICO_WINDOW_MGR_H_*/
