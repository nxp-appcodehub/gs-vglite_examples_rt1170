/*
 * Copyright 2019, 2023 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* FreeRTOS kernel includes. */
#include <math.h>
#include "fsl_debug_console.h"
#include "tiger_blue.h"
#include "tiger_grey.h"
#include "tiger_laven.h"
#include "tiger_lime.h"
#include "tiger_turk.h"
#include "tiger_white.h"
#include "tiger_yellow.h"
#include "vg_lite.h"
#include "display_support.h"
#include "draw_cube.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

#define RAD(d)        (d*3.1415926/180.0)
#define TEXTURE_IMAGE_WIDTH          256
#define TEXTURE_IMAGE_HEIGHT         236
#define TEXTURE_IMAGE_BYTE_PER_PIXEL 4
#define TEXTURE_IMAGE_STRIDE (TEXTURE_IMAGE_WIDTH*TEXTURE_IMAGE_BYTE_PER_PIXEL)
#define TEXTURE_IMAGE_BUFFER_SIZE (TEXTURE_IMAGE_WIDTH*TEXTURE_IMAGE_HEIGHT)

typedef struct VertexRec
{
    float x;
    float y;
    float z;
} vertex_t;

typedef struct NormalRec
{
    float x;
    float y;
    float z;
} normal_t;


/*******************************************************************************
 * Variables
 ******************************************************************************/

static vg_lite_buffer_t image0, image1, image2, image3, image4, image5;

static vertex_t cube_v0 = {-1.0, -1.0, -1.0};
static vertex_t cube_v1 = {1.0, -1.0, -1.0};
static vertex_t cube_v2 = {1.0, 1.0, -1.0};
static vertex_t cube_v3 = {-1.0, 1.0, -1.0};
static vertex_t cube_v4 = {-1.0, -1.0, 1.0};
static vertex_t cube_v5 = {1.0, -1.0, 1.0};
static vertex_t cube_v6 = {1.0, 1.0, 1.0};
static vertex_t cube_v7 = {-1.0, 1.0, 1.0};

static normal_t normal0321 = {0.0, 0.0, -1.0};
static normal_t normal4567 = {0.0, 0.0, 1.0};
static normal_t normal1265 = {1.0, 0.0, 0.0};
static normal_t normal0473 = {-1.0, 0.0, 0.0};
static normal_t normal2376 = {0.0, 1.0, 0.0};
static normal_t normal0154 = {0.0, -1.0, 0.0};

static vg_lite_filter_t filter;
static vg_lite_matrix_t matrix, rotate_3D;
static vertex_t rv0, rv1, rv2, rv3, rv4, rv5, rv6, rv7;
static float nz0321, nz4567, nz5126, nz0473, nz7623, nz0154;
static float cbsize, xoff, yoff, xrot, yrot, zrot, rotstep;

static int fb_width  = DEMO_PANEL_WIDTH;
static int fb_height = DEMO_PANEL_HEIGHT;

/* Allocate a buffer for first conversion from linear to tile, the next
   conversion will use the buffer of previous image. */
AT_NONCACHEABLE_SECTION_ALIGN(uint32_t image_data_buf[TEXTURE_IMAGE_BUFFER_SIZE], 64);

/*******************************************************************************
 * Code
 ******************************************************************************/

void scale_cube(vertex_t *vertex, float scale)
{
    /* Scale cube vertex coordinates to proper size */
    vertex->x *= scale;
    vertex->y *= scale;
    vertex->z *= scale;
}

void compute_rotate(float rx, float ry, float rz, vg_lite_matrix_t *rotate)
{
    /* Rotation angles rx, ry, rz (degree) for axis X, Y, Z
       Compute 3D rotation matrix base on rotation angle rx, ry, rz about axis X, Y, Z.
    */
    
    static float rx_l = -1, ry_l = -1, rz_l = -1;
    
    static float rx_sin, rx_cos, ry_sin, ry_cos, rz_sin, rz_cos;
    
    if (rx_l != rx)
    {
        rx_sin = sin(RAD(rx));
        rx_cos = cos(RAD(rx));
    }
    
    if (ry_l != ry)
    {
        ry_sin = sin(RAD(ry));
        ry_cos = cos(RAD(ry));
    }
  
    if (rz_l != rz)
    {
        rz_sin = sin(RAD(rz));
        rz_cos = cos(RAD(rz));
    }
    
    rotate->m[0][0] = rz_cos * ry_cos;
    rotate->m[0][1] = rz_cos * ry_sin * rx_sin - rz_sin * rx_cos;
    rotate->m[0][2] = rz_cos * ry_sin * rx_cos + rz_sin * rx_sin;
    rotate->m[1][0] = rz_sin * ry_cos;
    rotate->m[1][1] = rz_sin * ry_sin * rx_sin + rz_cos * rx_cos; 
    rotate->m[1][2] = rz_sin * ry_sin * rx_cos - rz_cos * rx_sin;
    rotate->m[2][0] = -ry_sin;
    rotate->m[2][1] = ry_cos * rx_sin;
    rotate->m[2][2] = ry_cos * rx_cos;
}

void transform_rotate(vg_lite_matrix_t *rotate, vertex_t *vertex, vertex_t *rc, float tx, float ty)
{
    /* Compute the new cube vertex coordinates transformed by the rotation matrix */
    rc->x = rotate->m[0][0] * vertex->x + rotate->m[0][1] * vertex->y + rotate->m[0][2] * vertex->z;
    rc->y = rotate->m[1][0] * vertex->x + rotate->m[1][1] * vertex->y + rotate->m[1][2] * vertex->z;
    rc->z = rotate->m[2][0] * vertex->x + rotate->m[2][1] * vertex->y + rotate->m[2][2] * vertex->z;

    /* Translate the vertex in XY plane */
    rc->x += tx;
    rc->y += ty;
}

void transform_normalZ(vg_lite_matrix_t *rotate, normal_t *nVec, float *nZ)
{
    /* Compute the new normal Z coordinate transformed by the rotation matrix */
    *nZ = rotate->m[2][0] * nVec->x + rotate->m[2][1] * nVec->y + rotate->m[2][2] * nVec->z;
}


/* Calculate the homogeneous matrix to map a rectangle image (0,0),(w,0),(w,h),(0,h) 
   to a parallelogram (x0,y0),(x1,y1),(x2,y2),(x3,y3). An affine transformation maps 
   a point (x, y) into the point(x*sx + y*shx + tx, x*shy + y*sy + ty) using homogeneous 
   matrix multiplication. So having the following equations:
     x0 = tx;
     y0 = ty;
     x1 = w*sx + tx;
     y1 = w*shy + ty;
     x3 = h*shx + tx;
     y3 = h*sy + ty;
*/
void transform_blit(float w, float h, vertex_t *v0, vertex_t *v1, vertex_t *v2, vertex_t *v3, vg_lite_matrix_t *matrix)
{
    float sx, sy, shx, shy, tx, ty;

    /* Compute 3x3 image transform matrix to map a rectangle image (w,h) to
       a parallelogram (x0,y0), (x1,y1), (x2,y2), (x3,y3) counterclock wise.
    */
    sx = (v1->x - v0->x) / w;
    sy = (v3->y - v0->y) / h;
    shx = (v3->x - v0->x) / h;
    shy = (v1->y - v0->y) / w;
    tx = v0->x;
    ty = v0->y;

    /* Set the blit transformation matrix */
    matrix->m[0][0] = sx;
    matrix->m[0][1] = shx;
    matrix->m[0][2] = tx;
    matrix->m[1][0] = shy;
    matrix->m[1][1] = sy; 
    matrix->m[1][2] = ty;
    matrix->m[2][0] = 0.0;
    matrix->m[2][1] = 0.0;
    matrix->m[2][2] = 1.0;
}

static int vg_lite_set_image(vg_lite_buffer_t *buffer, uint8_t *img_array)
{
	if ((uint32_t)img_array & 0x3F) {
		PRINTF("Image is not aligned at 64 bytes \r\n");
		return -1;
	}
	/* Get width, height, stride and format info */
	buffer->width = TEXTURE_IMAGE_WIDTH;
	buffer->height = TEXTURE_IMAGE_HEIGHT;
	buffer->stride = TEXTURE_IMAGE_STRIDE;
	buffer->format = VG_LITE_BGRA8888;
	/* Set image data in the buffer */
	buffer->handle = NULL;
	buffer->memory = img_array;
	buffer->address = (uint32_t)img_array;
	return 1;
}

static int vg_lite_linear_to_tiled(vg_lite_buffer_t *buffer)
{
    vg_lite_buffer_t tmpbuf;
    vg_lite_matrix_t mat;
    static uint8_t * pbuf = (uint8_t*)image_data_buf;
    if (buffer->tiled != VG_LITE_LINEAR)
    {
          PRINTF("Pixel layout must be linear!\r\n");
          return -1;
    }
    /* Get width, height, stride and format info */
    tmpbuf.width = TEXTURE_IMAGE_WIDTH;
    tmpbuf.height = TEXTURE_IMAGE_HEIGHT;
    tmpbuf.stride = TEXTURE_IMAGE_STRIDE;
    tmpbuf.format = VG_LITE_BGRA8888;
    /* Set target vglite buffer */
    tmpbuf.handle = NULL;
    tmpbuf.memory = pbuf;
    tmpbuf.address = (uint32_t)pbuf;
    tmpbuf.tiled = VG_LITE_TILED;
      
    vg_lite_identity(&mat);
    /* Convert the linear layout (source) to tiled layout (destination) */
    vg_lite_blit(&tmpbuf, buffer, &mat, VG_LITE_BLEND_NONE, 0, filter);
    
    /* Save the image buffer for next conversion */
    pbuf = buffer->memory;
      
    /* Return the converted and tiled buffer */  
    *buffer = tmpbuf;
    
    return 1;    
}




bool load_texture_images(void)
{
    /* Set image filter type. */
    filter = VG_LITE_FILTER_POINT;
    
    /* Load the image0 */
    if (vg_lite_set_image(&image0, (uint8_t *)image_data_tiger_lime) != 1) {
        PRINTF("load image file error\n");
        return false;
    }
    vg_lite_linear_to_tiled(&image0);

    /* Load the image1 */
    if (vg_lite_set_image(&image1, (uint8_t *)image_data_tiger_blue) != 1) {
        PRINTF("load image file error\n");
        return false;
    }
    vg_lite_linear_to_tiled(&image1);
      
    /* Load the image2 */
    if (vg_lite_set_image(&image2, (uint8_t *)image_data_tiger_turk) != 1) {
        PRINTF("load image file error\n");
        return false;
    }
    vg_lite_linear_to_tiled(&image2);
      
    /* Load the image3 */
    if (vg_lite_set_image(&image3, (uint8_t *)image_data_tiger_white) != 1) {
        PRINTF("load image file error\n");
        return false;
    }
    vg_lite_linear_to_tiled(&image3);
    
    /* Load the image4 */
    if (vg_lite_set_image(&image4, (uint8_t *)image_data_tiger_laven) != 1) {
        PRINTF("load image file error\n");
        return false;
    }
    vg_lite_linear_to_tiled(&image4);
      
    /* Load the image5 */
    if (vg_lite_set_image(&image5, (uint8_t *)image_data_tiger_yellow) != 1) {
        PRINTF("load image file error\n");
        return false;
    }
    vg_lite_linear_to_tiled(&image5);
    
    /* Scale the cube to proper size */
    cbsize = fb_width / 4.0;
    scale_cube(&cube_v0, cbsize);
    scale_cube(&cube_v1, cbsize);
    scale_cube(&cube_v2, cbsize);
    scale_cube(&cube_v3, cbsize);
    scale_cube(&cube_v4, cbsize);
    scale_cube(&cube_v5, cbsize);
    scale_cube(&cube_v6, cbsize);
    scale_cube(&cube_v7, cbsize);

    /* Translate the cube to the center of framebuffer */
    xoff = fb_width / 2.0;
    yoff = fb_height / 2.0;

    /* Set the initial cube rotation degree and step */
    xrot = 20.0;
    yrot = 0.0;
    zrot = 20.0;
    rotstep = 3.0;

    return true;
}

void draw_cube(vg_lite_buffer_t *rt)
{
    /* Rotation angles (degree) for axis X, Y, Z */
    compute_rotate(xrot, yrot, zrot, &rotate_3D);
    //xrot += rotstep;
    yrot += rotstep;
    //zrot += rotstep;

    /* Compute the new cube vertex coordinates transformed by the rotation matrix */
    transform_rotate(&rotate_3D, &cube_v0, &rv0, xoff, yoff);
    transform_rotate(&rotate_3D, &cube_v1, &rv1, xoff, yoff);
    transform_rotate(&rotate_3D, &cube_v2, &rv2, xoff, yoff);
    transform_rotate(&rotate_3D, &cube_v3, &rv3, xoff, yoff);
    transform_rotate(&rotate_3D, &cube_v4, &rv4, xoff, yoff);
    transform_rotate(&rotate_3D, &cube_v5, &rv5, xoff, yoff);
    transform_rotate(&rotate_3D, &cube_v6, &rv6, xoff, yoff);
    transform_rotate(&rotate_3D, &cube_v7, &rv7, xoff, yoff);

    /* Compute the surface normal direction to determine the front/back face */
    transform_normalZ(&rotate_3D, &normal0321, &nz0321);
    transform_normalZ(&rotate_3D, &normal4567, &nz4567);
    transform_normalZ(&rotate_3D, &normal1265, &nz5126);
    transform_normalZ(&rotate_3D, &normal0473, &nz0473);
    transform_normalZ(&rotate_3D, &normal2376, &nz7623);
    transform_normalZ(&rotate_3D, &normal0154, &nz0154);

    if (nz0321 > 0.0)
    {
        /* Compute 3x3 image transform matrix to map a rectangle image (w,h) to
           a parallelogram (x0,y0), (x1,y1), (x2,y2), (x3,y3) counterclock wise.
        */
        transform_blit(image0.width, image0.height, &rv0, &rv3, &rv2, &rv1, &matrix);

        /* Blit the image using the matrix */
        vg_lite_blit(rt, &image0, &matrix, VG_LITE_BLEND_SCREEN, 0, filter);
    }

    if (nz4567 > 0.0)
    {
        /* Compute 3x3 image transform matrix to map a rectangle image (w,h) to
           a parallelogram (x0,y0), (x1,y1), (x2,y2), (x3,y3) counterclock wise.
        */
        transform_blit(image1.width, image1.height, &rv4, &rv5, &rv6, &rv7, &matrix);

        /* Blit the image using the matrix */
        vg_lite_blit(rt, &image1, &matrix, VG_LITE_BLEND_SCREEN, 0, filter);
    }

    if (nz5126 > 0.0)
    {
        /* Compute 3x3 image transform matrix to map a rectangle image (w,h) to
           a parallelogram (x0,y0), (x1,y1), (x2,y2), (x3,y3) counterclock wise.
        */
        transform_blit(image2.width, image2.height, &rv5, &rv1, &rv2, &rv6, &matrix);

        /* Blit the image using the matrix */
        vg_lite_blit(rt, &image2, &matrix, VG_LITE_BLEND_SCREEN, 0, filter);
    }

    if (nz0473 > 0.0)
    {
        /* Compute 3x3 image transform matrix to map a rectangle image (w,h) to
           a parallelogram (x0,y0), (x1,y1), (x2,y2), (x3,y3) counterclock wise.
        */
        transform_blit(image3.width, image3.height, &rv0, &rv4, &rv7, &rv3, &matrix);

        /* Blit the image using the matrix */
        vg_lite_blit(rt, &image3, &matrix, VG_LITE_BLEND_SCREEN, 0, filter);
    }

    if (nz7623 > 0.0)
    {
        /* Compute 3x3 image transform matrix to map a rectangle image (w,h) to
           a parallelogram (x0,y0), (x1,y1), (x2,y2), (x3,y3) counterclock wise.
        */
        transform_blit(image4.width, image4.height, &rv7, &rv6, &rv2, &rv3, &matrix);

        /* Blit the image using the matrix */
        vg_lite_blit(rt, &image4, &matrix, VG_LITE_BLEND_SCREEN, 0, filter);
    }

    if (nz0154 > 0.0)
    {
        /* Compute 3x3 image transform matrix to map a rectangle image (w,h) to
           a parallelogram (x0,y0), (x1,y1), (x2,y2), (x3,y3) counterclock wise.
        */
        transform_blit(image5.width, image5.height, &rv0, &rv1, &rv5, &rv4, &matrix);

        /* Blit the image using the matrix */
        vg_lite_blit(rt, &image5, &matrix, VG_LITE_BLEND_SCREEN, 0, filter);
    }
}
