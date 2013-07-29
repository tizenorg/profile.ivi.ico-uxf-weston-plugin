/*
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
 * @brief   Multi Input Manager header for Device Input Controller
 *
 * @date    Jul-26-2013
 */

#ifndef _ICO_INPUT_MGR_H_
#define _ICO_INPUT_MGR_H_

/* Input Region struct for Haptic Device Controller */
struct ico_uifw_input_region    {
    uint16_t    node;                   /* display node                         */
    uint16_t    res;                    /* (unused)                             */
    uint32_t    surfaceid;              /* surface Id                           */
    uint16_t    surface_x;              /* surface absolute X coordinate        */
    uint16_t    surface_y;              /* surface absolute Y coordinate        */
    uint16_t    x;                      /* input region relative X coordinate   */
    uint16_t    y;                      /* input region relative Y coordinate   */
    uint16_t    width;                  /* input region width                   */
    uint16_t    height;                 /* input region height                  */
    int32_t     attr;                   /* input region attribute               */
};

#endif  /*_ICO_INPUT_MGR_H_*/

