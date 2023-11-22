/*
 * Copyright 2019, 2021, 2023 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "FreeRTOS.h"
#include "task.h"

#include "fsl_debug_console.h"
#include "board.h"

#include "vglite_support.h"
#include "vglite_window.h"
/*-----------------------------------------------------------*/
#include "vg_lite.h"

#include "pin_mux.h"
#include "fsl_soc_src.h"
/*******************************************************************************
 * Definitions
 ******************************************************************************/
#if (CUSTOM_VGLITE_MEMORY_CONFIG != 1)
#error "Application must be compiled with CUSTOM_VGLITE_MEMORY_CONFIG=1"
#else
#define VGLITE_COMMAND_BUFFER_SZ (128 * 1024)
/* On RT595S */
#if defined(CPU_MIMXRT595SFFOC_cm33)
#define VGLITE_HEAP_SZ 0x400000 /* 4 MB */
/* On RT1170 */
#elif defined(CPU_MIMXRT1176DVMAA_cm7) || defined(CPU_MIMXRT1166DVM6A_cm7)
#define VGLITE_HEAP_SZ 8912896 /* 8.5 MB */
#else
#error "Unsupported CPU !"
#endif
#if (720 * 1280 == (DEMO_PANEL_WIDTH) * (DEMO_PANEL_HEIGHT))
#define TW 720
/* On RT595S */
#if defined(CPU_MIMXRT595SFFOC_cm33)
/* Tessellation window = 720 x 640 */
#define TH 640
/* On RT1170 */
#elif defined(CPU_MIMXRT1176DVMAA_cm7) || defined(CPU_MIMXRT1166DVM6A_cm7)
/* Tessellation window = 720 x 1280 */
#define TH 1280
#else
#error "Unsupported CPU !"
#endif
/* Panel RM67162. Supported only by platform RT595S. */
#elif (400 * 400 == (DEMO_PANEL_WIDTH) * (DEMO_PANEL_HEIGHT))
/* Tessellation window = 400 x 400 */
#define TW 400
#define TH 400
#else
/* Tessellation window = 256 x 256 */
#define TW 256
#define TH 256
#endif
/* Allocate the heap and set the command buffer(s) size */
AT_NONCACHEABLE_SECTION_ALIGN(uint8_t vglite_heap[VGLITE_HEAP_SZ], 64);

void *vglite_heap_base        = &vglite_heap;
uint32_t vglite_heap_size     = VGLITE_HEAP_SZ;
#endif

#define OFFSCREEN_WIDTH  200
#define OFFSCREEN_HEIGHT 320
/*******************************************************************************
 * Prototypes
 ******************************************************************************/
static void vglite_task(void *pvParameters);

/*******************************************************************************
 * Variables
 ******************************************************************************/
static vg_lite_display_t display;
static vg_lite_window_t  window;

vg_lite_buffer_t buffer;
vg_lite_path_t paths[9];

vg_lite_filter_t mainFilter;
vg_lite_matrix_t matrix;

vg_lite_cap_style_t capStyles[3] = {
    VG_LITE_CAP_BUTT,
    VG_LITE_CAP_ROUND,
    VG_LITE_CAP_SQUARE
};

vg_lite_join_style_t joinStyles[3] = {
    VG_LITE_JOIN_MITER,
    VG_LITE_JOIN_ROUND,
    VG_LITE_JOIN_BEVEL
};

float stroke_dash_pattern[4] = {30.0f, 20.0f, 50.0f, 25.0f};

static int32_t pathData[] = {
    2, 200, 400,          //Move to (200, 400)
    4, 300, 300,          //Line from (200, 400) to (300, 300)
    8, 254, 228, 365, 190, 300, 100,  //Cubic Curve from (300, 300) to (300, 100) with control point 1 in (254, 228) and control point 2 in (365, 190)
    8, 300, 197, 200, 106, 200, 0,    //Cubic Curve from (300, 100) to (200, 0) with control point 1 in (300, 197) and control point 2 in (200, 106)
    8, 132,   0, 158, 187, 100, 100,  //Cubic Curve from (200, 0) to (100, 100) with control point 1 in (132, 0) and control point 2 in (158, 187)
    8,   0, 100, 200, 300, 100, 300,  //Cubic Curve from (100, 100) to (100, 300) with control point 1 in (0, 100) and control point 2 in (200, 300)
    4, 200, 400,          //Line from (100, 300) to (200, 400)
    0,
};

/*******************************************************************************
 * Code
 ******************************************************************************/
static void BOARD_ResetDisplayMix(void)
{
    /*
     * Reset the displaymix, otherwise during debugging, the
     * debugger may not reset the display, then the behavior
     * is not right.
     */
    SRC_AssertSliceSoftwareReset(SRC, kSRC_DisplaySlice);
    while (kSRC_SliceResetInProcess == SRC_GetSliceResetState(SRC, kSRC_DisplaySlice))
    {
    }
}

int main(void)
{
    /* Init board hardware. */
    BOARD_ConfigMPU();
    BOARD_BootClockRUN();
    BOARD_ResetDisplayMix();
    BOARD_InitLpuartPins();
    BOARD_InitMipiPanelPins();
    BOARD_InitDebugConsole();

    if (xTaskCreate(vglite_task, "vglite_task", configMINIMAL_STACK_SIZE + 200, NULL, configMAX_PRIORITIES - 1, NULL) !=
        pdPASS)
    {
        PRINTF("Task creation failed!.\r\n");
        while (1)
            ;
    }

    vTaskStartScheduler();
    for (;;)
        ;
}

static void cleanup(void)
{
    for (int i = 0; i < 9; i ++) {
        vg_lite_clear_path(&paths[i]);
    }
    vg_lite_free(&buffer);
    vg_lite_close();
}

static vg_lite_error_t init_vg_lite(void)
{
    vg_lite_error_t error = VG_LITE_SUCCESS;

    /* Define the dash pattern */

    error = VGLITE_CreateDisplay(&display);
    if (error) {
        PRINTF("VGLITE_CreateDisplay failed: VGLITE_CreateDisplay() returned error %d\n", error);
        return error;
    }
    /* Initialize the window */
    error = VGLITE_CreateWindow(&display, &window);
    if (error) {
        PRINTF("VGLITE_CreateWindow failed: VGLITE_CreateWindow() returned error %d\n", error);
        return error;
    }

    /* Initialize  the VGLite API. Two parameters provided are the tessellation buffer size.
       This is recommended to be the size of your most commonly rendered path size. A bigger
       tessellation buffer is fast to execute but consumes more memory
     */
    error = vg_lite_init(OFFSCREEN_WIDTH, OFFSCREEN_HEIGHT);
    if (error) {
        PRINTF("vg_lite engine init failed: vg_lite_init() returned error %d\n", error);
        cleanup();
        return error;
    }
    
    // Set GPU command buffer size for this drawing task.
    error = vg_lite_set_command_buffer_size(VGLITE_COMMAND_BUFFER_SZ);
    if (error)
    {
        PRINTF("vg_lite_set_command_buffer_size() returned error %d\r\n", error);
        cleanup();
        return error;
    }

    mainFilter = VG_LITE_FILTER_POINT;

    buffer.width = OFFSCREEN_WIDTH;
    buffer.height = OFFSCREEN_HEIGHT;
    buffer.format = VG_LITE_RGBA8888;
    error = vg_lite_allocate(&buffer);
    if (VG_LITE_SUCCESS != error)
    {
        PRINTF("Could not allocate Main Screen Render Target\n\r");
        cleanup();
        return error;
    }

    /* Allocate an offscreen buffer where the path will be rendered */
    int index = 0;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            paths[index].bounding_box[0] = 0;
            paths[index].bounding_box[1] = 0;
            paths[index].bounding_box[2] = 40;
            paths[index].bounding_box[3] = 40;
            paths[index].quality = VG_LITE_HIGH;
            paths[index].format = VG_LITE_S32;
            paths[index].path_length = sizeof(pathData);
            paths[index].path = pathData;
            paths[index].path_changed = 1;

            /* Sets the attributes of a stroked vector path */
            error = vg_lite_set_stroke(&paths[index], capStyles[i], joinStyles[j], 10.0f, 5, stroke_dash_pattern, sizeof(stroke_dash_pattern) / sizeof(stroke_dash_pattern[0]), 4.0f, 0xff000000);
            if (VG_LITE_SUCCESS != error)
            {
                PRINTF("Set the vector path type failed\n\r");
                cleanup();
                return error;
            }

            /* Set the vector path type */
            error = vg_lite_set_draw_path_type(&paths[index], VG_LITE_DRAW_FILL_STROKE_PATH);
            if (VG_LITE_SUCCESS != error)
            {
                PRINTF("Set stroke path's attributes failed\n\r");
                cleanup();
                return error;
            }

            /* Update the stroked path's parameters and generate the stroked path data */
            error = vg_lite_update_stroke(&paths[index]);
            if (VG_LITE_SUCCESS != error)
            {
                PRINTF("Update the stroked path failed\n\r");
                cleanup();
                return error;
            }
            index++;
        }
    }
    return error;
}

static void redraw()
{
    vg_lite_error_t error = VG_LITE_SUCCESS;

    vg_lite_buffer_t *rt = VGLITE_GetRenderTarget(&window);
    if(rt == NULL)
    {
        PRINTF("vg_lite_get_renderTarget error\r\n");
        while (1)
            ;
    }

    /* Clear the buffer with a solid color, the color format is ABGR:
       The red channel is in the lower 8-bit of the color value, followed
       by the green and blue channels. The alpha channel is in the upper
       8-bit of the color value.*/

    /* Clear whole screen with solid blue */
    vg_lite_clear(rt, NULL, 0xFFFF0000);

    int start_x = DEMO_BUFFER_WIDTH/2  - 320;
    int start_y = DEMO_BUFFER_HEIGHT/2 - 500;

    int index = 0;

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            vg_lite_clear(&buffer, NULL, 0xFFFFFFFF);

            /* Define the transformation matrix that will be applied */
            vg_lite_identity(&matrix);

            /* Translate the path to the center of buffer */
            vg_lite_translate(-50, 10, &matrix);
            vg_lite_scale(0.75, 0.75, &matrix);

            /* Draw the path, defining the path data, fill rule, matrix, blend option and paint */
            error = vg_lite_draw(&buffer, &paths[index], VG_LITE_FILL_EVEN_ODD, &matrix, VG_LITE_BLEND_NONE, 0xFF0000FF);
            if (error) {
                PRINTF("vg_lite_draw() returned error %d\n", error);
                cleanup();
                return;
            }

            /* Reset the transformation matrix that will be applied */
            vg_lite_identity(&matrix);

            /* Translate the image to center of screen */
            vg_lite_translate(start_x, start_y, &matrix);

            /* Blit the buffer to the framebuffer so we can see something on the screen */
            error = vg_lite_blit(rt, &buffer, &matrix, VG_LITE_BLEND_SRC_OVER, 0, mainFilter);
            if (error)
            {
                PRINTF("vg_lite_blit() returned error %d\n", error);
                cleanup();
                return;
            }

            start_x += 220;
            index++;
        }
        start_y += 340;
        start_x = DEMO_BUFFER_WIDTH/2 - 320;
    }
    /* Switch the current framebuffer to be displayed */
    VGLITE_SwapBuffers(&window);

    return;
}

uint32_t getTime()
{
    return (uint32_t) (xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void vglite_task(void *pvParameters)
{
    status_t status;
    vg_lite_error_t error;
    uint32_t startTime, time, n = 0, fps_x_1000;

    status = BOARD_PrepareVGLiteController();
    if (status != kStatus_Success)
    {
        PRINTF("Prepare VGlite controller error\r\n");
        while (1)
            ;
    }

    error = init_vg_lite();
    if (error) {
        PRINTF("init_vg_lite failed: init_vg_lite() returned error %d\r\n", error);
        while (1)
            ;
    }

    startTime = getTime();
    while (1)
    {
        redraw();
        n++;
        if (n >= 60)
        {
            time       = getTime() - startTime;
            fps_x_1000 = (n * 1000 * 1000) / time;
            PRINTF("%d frames in %d mSec: %d.%d FPS\r\n", n, time, fps_x_1000 / 1000, fps_x_1000 % 1000);
            n         = 0;
            startTime = getTime();
        }
    }
}
