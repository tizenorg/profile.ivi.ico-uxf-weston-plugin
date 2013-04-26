#ifdef HAVE_CONFIG_H
#undef HAVE_CONFIG_H
#endif

#ifdef HAVE_CONFIG_H

#include "config.h"
#else
#define __UNUSED__
#define PACKAGE_EXAMPLES_DIR "."
#endif

#include <Ecore.h>
#include <Ecore_Evas.h>
#include <stdio.h>

#define WIDTH  (520)
#define HEIGHT (380)

static Ecore_Evas *ee;
static const char *border_img_path = PACKAGE_EXAMPLES_DIR "/red.png";

static Evas_Object *bg, *r1, *r2, *r3; /* "sub" canvas objects */
static Evas_Object *border, *img; /* canvas objects */

static void
_on_destroy(Ecore_Evas *ee __UNUSED__)
{
    ecore_main_loop_quit();
}

static void
_resize_cb(Ecore_Evas *ee)
{
    int x, y, w, h, ow, oh;
    int rw, rh;

    evas_object_geometry_get(img, &x, &y, &ow, &oh);
    ecore_evas_geometry_get(ee, &x, &y, &w, &h);
    ecore_evas_request_geometry_get(ee, NULL, NULL, &rw, &rh);
    fprintf(stderr, "EFL-App resize x/y=%d/%d req=%d/%d w/h=%d/%d obj=%d/%d\n", x, y, rw, rh, w, h, ow, oh); fflush(stderr);
}

int
main(int argc, char *argv[])
{
    Evas *canvas, *sub_canvas;
    Ecore_Evas *sub_ee;
    int     i;
    int     width;
    int     height;
    unsigned int    color;
    int     r, g, b, a;
    int     appno = 1;
    char    sTitle[64];

    width = WIDTH;
    height = HEIGHT;
    color = 0xc0c0c0c0;
    for (i = 1; i < argc; i++)  {
        if (argv[i][0] == '@')  {
            appno = strtol(&argv[i][1], (char **)0, 0);
        }
        if (argv[i][0] != '-')  continue;
        if (strncasecmp(argv[i], "-width=", 7) == 0)    {
            width = strtol(&argv[i][7], (char **)0, 0);
        }
        else if (strncasecmp(argv[i], "-height=", 8) == 0)  {
            height = strtol(&argv[i][8], (char **)0, 0);
        }
        else if (strncasecmp(argv[i], "-color=", 7) == 0)   {
            color = strtoul(&argv[i][7], (char **)0, 0);
        }
    }

    ecore_evas_init();

    /* this will give you a window with an Evas canvas under the first
     * engine available */
    ee = ecore_evas_new(NULL, 0, 0, width, height, "frame=0");
    if (!ee) goto error;

    ecore_evas_size_min_set(ee, width/4, height/4);
    ecore_evas_size_max_set(ee, width*4, height*4);

    ecore_evas_callback_resize_set(ee, _resize_cb);

    ecore_evas_callback_delete_request_set(ee, _on_destroy);
    sprintf(sTitle, "EFL Native Application %d", appno);
    ecore_evas_title_set(ee, sTitle);
    ecore_evas_show(ee);

    canvas = ecore_evas_get(ee);

    bg = evas_object_rectangle_add(canvas);
    r = (color>>16)&0x0ff;
    g = (color>>8)&0x0ff;
    b = color&0x0ff;
    a = (color>>24)&0x0ff;
    if (a != 255)   {
        r = (r * a) / 255;
        g = (g * a) / 255;
        b = (b * a) / 255;
    }
    evas_object_color_set(bg, r, g, b, a); /* bg color */
    evas_object_move(bg, 0, 0); /* at origin */
    evas_object_resize(bg, width, height); /* covers full canvas */
    evas_object_show(bg);

    /* this is a border around the image containing a scene of another
     * canvas */
    border = evas_object_image_filled_add(canvas);
    evas_object_image_file_set(border, border_img_path, NULL);
    evas_object_image_border_set(border, 3, 3, 3, 3);
    evas_object_image_border_center_fill_set(border, EVAS_BORDER_FILL_NONE);

    evas_object_move(border, width / 6, height / 6);
    evas_object_resize(border, (2 * width) / 3, (2 * height) / 3);
    evas_object_show(border);

    img = ecore_evas_object_image_new(ee);
    evas_object_image_filled_set(img, EINA_TRUE);
    evas_object_image_size_set(
        img, ((2 * width) / 3) - 6, ((2 * height) / 3) - 6);
    sub_ee = ecore_evas_object_ecore_evas_get(img);
    sub_canvas = ecore_evas_object_evas_get(img);

    evas_object_move(img, (width / 6) + 3, (height / 6) + 3);

    /* apply the same size on both! */
    evas_object_resize(img, ((2 * width) / 3) - 6, ((2 * height) / 3) - 6);
    ecore_evas_resize(sub_ee, ((2 * width) / 3) - 6, ((2 * height) / 3) - 6);

    r1 = evas_object_rectangle_add(sub_canvas);
    evas_object_color_set(r1, g, b, r, 255);
    evas_object_move(r1, 10, 10);
    evas_object_resize(r1, 100, 100);
    evas_object_show(r1);

    r2 = evas_object_rectangle_add(sub_canvas);
    evas_object_color_set(r2, b/2, g/2, r/2, 128);
    evas_object_move(r2, 10, 10);
    evas_object_resize(r2, 50, 50);
    evas_object_show(r2);

    r3 = evas_object_rectangle_add(sub_canvas);
    evas_object_color_set(r3, b, r, g, 255);
    evas_object_move(r3, 60, 60);
    evas_object_resize(r3, 50, 50);
    evas_object_show(r3);

    evas_object_show(img);
    ecore_main_loop_begin();

    ecore_evas_free(ee);
    ecore_evas_shutdown();

    return 0;

error:
    fprintf(stderr, "You got to have at least one Evas engine built"
            " and linked up to ecore-evas for this example to run properly.\n");
    ecore_evas_shutdown();
    return -1;
}

