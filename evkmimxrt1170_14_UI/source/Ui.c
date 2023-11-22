/*
 * Copyright 2019, 2021, 2023 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#include "fsl_debug_console.h"
#include "board.h"

#include "vglite_support.h"
#include "vglite_window.h"
/*-----------------------------------------------------------*/
#include "vg_lite.h"

#include "pin_mux.h"
#include "fsl_soc_src.h"

/* Icons */
#include "icons/1.h"
#include "icons/2.h"
#include "icons/3.h"
#include "icons/4.h"
#include "icons/5.h"
#include "icons/6.h"

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

#define ICON_COUNT             6
#define HIGHLIGHT_SIZE         10
#define HIGHLIGHT_RAD          2
#define HIGHLIGHT_PERIOD_MS    1000
/*******************************************************************************
 * Prototypes
 ******************************************************************************/
static void vglite_task(void *pvParameters);

/*******************************************************************************
 * Variables
 ******************************************************************************/
/* Generic variables for all test cases */
static vg_lite_display_t       display;
static vg_lite_window_t        window;
/* Test specific for this test cases */
static vg_lite_buffer_t        *fb;
/* FB with, height */
static int                     fb_width, fb_height;
static vg_lite_buffer_t        icons[ICON_COUNT];
static vg_lite_matrix_t        icon_matrix, highlight_matrix;
static int                     icon_pos[6][2];
static int                     icon_size = 128;

static TimerHandle_t           timer_handle = NULL;
static int                     highlight_index = 0;

/* Rounded rectangle path with original size 10x10 @ (0, 0) */
static int8_t path_data[] = {
    2, HIGHLIGHT_RAD,  0,

    4, HIGHLIGHT_SIZE - HIGHLIGHT_RAD, 0,
    6, HIGHLIGHT_SIZE, 0, HIGHLIGHT_SIZE, HIGHLIGHT_RAD,

    4, HIGHLIGHT_SIZE, HIGHLIGHT_SIZE - HIGHLIGHT_RAD,
    6, HIGHLIGHT_SIZE, HIGHLIGHT_SIZE, HIGHLIGHT_SIZE - HIGHLIGHT_RAD, HIGHLIGHT_SIZE,

    4, HIGHLIGHT_RAD, HIGHLIGHT_SIZE,
    6, 0, HIGHLIGHT_SIZE, 0, HIGHLIGHT_SIZE - HIGHLIGHT_RAD,

    4, 0, HIGHLIGHT_RAD,
    6, 0, 0, HIGHLIGHT_RAD, 0,
    0

};

static vg_lite_path_t path = {
    {-10, -10, 10, 10},        /* left,top, right,bottom */
    VG_LITE_HIGH,              /* quality */
    VG_LITE_S8,                /* -128 to 127 coordinate range */
    {0},                       /* uploaded */
    sizeof(path_data),         /* path length */
    path_data,                 /* path data */
    1
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

static void cleanup(void)
{
    vg_lite_free(fb);
    vg_lite_clear_path(&path);
    vg_lite_close();
}

/******************************************************************************/
static int vg_lite_set_image(vg_lite_buffer_t *buffer, uint8_t *imm_array, int32_t width, int32_t height, int32_t stride, vg_lite_buffer_format_t format)
{
    if ((uint32_t)imm_array & 0x3F) {
        PRINTF("Image is not aligned at 64 bytes \r\n");
        return -1;
    }

    /* Get width, height, stride and format info */
    buffer->width = width;
    buffer->height = height;
    PRINTF("o   Loaded icon : %d x %d pixels \r\n",
           buffer->width, buffer->height);
    buffer->stride = stride;
    buffer->format = format;
    /* 'Copy' image data in the buffer */
    buffer->handle = NULL;
    buffer->memory = imm_array;
    buffer->address = (uint32_t)imm_array;

    return 0;
}

/******************************************************************************/
static int loadImages(void)
{
    int        err;

    /* Load the icons */
    err = vg_lite_set_image(&icons[0], &img1_data[0], IMG1_WIDTH, IMG1_HEIGHT, IMG1_STRIDE, IMG1_FORMAT);
    if (err) {
        PRINTF("Load img1 file error \r\n");
        return -1;
    }
    err = vg_lite_set_image(&icons[1], &img2_data[0], IMG2_WIDTH, IMG2_HEIGHT, IMG2_STRIDE, IMG2_FORMAT);
    if (err) {
        PRINTF("Load img2 file error \r\n");
        return -1;
    }
    err = vg_lite_set_image(&icons[2], &img3_data[0], IMG3_WIDTH, IMG3_HEIGHT, IMG3_STRIDE, IMG3_FORMAT);
    if (err) {
        PRINTF("Load img3 file error \r\n");
        return -1;
    }
    err = vg_lite_set_image(&icons[3], &img4_data[0], IMG4_WIDTH, IMG4_HEIGHT, IMG4_STRIDE, IMG4_FORMAT);
    if (err) {
        PRINTF("Load img4 file error \r\n");
        return -1;
    }
    err = vg_lite_set_image(&icons[4], &img5_data[0], IMG5_WIDTH, IMG5_HEIGHT, IMG5_STRIDE, IMG5_FORMAT);
    if (err) {
        PRINTF("Load img5 file error \r\n");
        return -1;
    }
    err = vg_lite_set_image(&icons[5], &img6_data[0], IMG6_WIDTH, IMG6_HEIGHT, IMG6_STRIDE, IMG6_FORMAT);
    if (err) {
        PRINTF("Load img6 file error \r\n");
        return -1;
    }
    return 0;
}

/******************************************************************************/

static void timer_callback(void* parameter)
{
    highlight_index += 1;
}

static vg_lite_error_t init_vg_lite(void)
{
    vg_lite_error_t error;
    status_t        status;
    int             err;

    error = VG_LITE_SUCCESS;

    status = BOARD_PrepareVGLiteController();
    if (status != kStatus_Success) {
        PRINTF("[%d] : BOARD_PrepareVGLiteController failed \r\n", status);
        return VG_LITE_NOT_SUPPORT;
    }
    error = VGLITE_CreateDisplay(&display);
    if (error) {
        PRINTF("[%d] : VGLITE_CreateDisplay failed \r\n", error);
        return VG_LITE_NOT_SUPPORT;
    }
    /* Initialize the window */
    error = VGLITE_CreateWindow(&display, &window);
    if (error) {
        PRINTF("[%d] : VGLITE_CreateWindow failed \r\n", error);
        return VG_LITE_NOT_SUPPORT;
    }

    error = vg_lite_init(32, 32);
    if (error) {
        PRINTF("[%d] : vg_lite_init failed \r\n", error);
        cleanup();
        return error;
    }

    /* Set GPU command buffer size for this drawing task. */
    error = vg_lite_set_command_buffer_size(VGLITE_COMMAND_BUFFER_SZ);
    if (error)
    {
        PRINTF("vg_lite_set_command_buffer_size() returned error %d\r\n", error);
        cleanup();
        return error;
    }

    PRINTF("Framebuffer size : %d x %d \r\n", fb->width, fb->height);

    fb_width = fb->width;
    fb_height = fb->height;

    /* Load the 'icons' in buffers */
    err = loadImages();
    if (err) {
        cleanup();
        return VG_LITE_NOT_SUPPORT;
    }

    return error;
}

static void redraw()
{
    vg_lite_filter_t        filter;
    int i, j, icon_id, gap_x, gap_y, x, y;
    vg_lite_error_t error = VG_LITE_SUCCESS;

    /* Set image filter type according to hardware feature */
    filter = VG_LITE_FILTER_BI_LINEAR;

    /* Get the framebuffer */
    fb = VGLITE_GetRenderTarget(&window);
    if (fb == NULL) {
        PRINTF("VGLITE_GetRenderTarget() error\r\n");
        cleanup();
        return;
    }
    // PRINTF("Framebuffer size : %d x %d \r\n", fb->width, fb->height);

    fb_width = fb->width;
    fb_height = fb->height;

    /* Clear with WHITE */
    error = vg_lite_clear(fb, NULL, 0xFFFFFFFF);
    if (error) {
        PRINTF("vg_lite_clear() error\r\n");
        cleanup();
        return;
    }

    /* Draw the highlighted rectangle. */
    /* Setup a 10x10 scale at center of buffer */
    highlight_index = highlight_index % ICON_COUNT;

    vg_lite_identity(&highlight_matrix);
    vg_lite_translate(icon_pos[highlight_index][0], icon_pos[highlight_index][1], &highlight_matrix);
    vg_lite_scale(icon_size / (float)HIGHLIGHT_SIZE, icon_size / (float)HIGHLIGHT_SIZE, &highlight_matrix);

    /* Draw the path using the matrix */
    error = vg_lite_draw(fb, &path, VG_LITE_FILL_EVEN_ODD, &highlight_matrix, VG_LITE_BLEND_SRC_OVER, 0xFFE5AF71);
    if (error) {
        PRINTF("vg_lite_draw() returned error %d\n", error);
        cleanup();
        return;
    }

    /* Draw the 6 icons (3 x 2) */
    gap_x = (fb_width - icon_size * 3) / 4;
    gap_y = (fb_height - icon_size * 2) / 3;
    icon_id = 0;

    y = gap_y;
    for (i = 0; i < 2; i++) {
        x = gap_x;
        for (j = 0; j < 3; j++) {
            icon_pos[icon_id][0] = x;
            icon_pos[icon_id][1] = y;
            x += icon_size + gap_x;
            /* Setup the matrix. */
            vg_lite_identity(&icon_matrix);
            vg_lite_translate(icon_pos[icon_id][0], icon_pos[icon_id][1], &icon_matrix);
            vg_lite_scale((float)icon_size / icons[icon_id].width, (float)icon_size / icons[icon_id].height, &icon_matrix);

            /* Scale up the highlighted icon */
            // if ((i * 3) + j == highlight_index)
            // {
            //     vg_lite_scale(1.1f, 1.1f, &icon_matrix);
            //     vg_lite_translate(-(0.05f * icons[icon_id].width), -(0.05f * icons[icon_id].height), &icon_matrix);
            // }

            error = vg_lite_blit(fb, &icons[icon_id], &icon_matrix, VG_LITE_BLEND_SRC_OVER, 0, filter);
            if (error) {
                PRINTF("vg_lite_blit() returned error %d\n", error);
                cleanup();
                return;
            }

            icon_id++;
        }
        y += icon_size + gap_y;
    }

    vg_lite_finish();

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

    timer_handle = xTimerCreate("Timer", pdMS_TO_TICKS(HIGHLIGHT_PERIOD_MS), pdTRUE, (void *)NULL, (TimerCallbackFunction_t)timer_callback);
    if (timer_handle == NULL)
    {
        PRINTF("Timer creation failed!.\r\n");
        while (1)
        ;
    }
    xTimerStart(timer_handle, 0);

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

/******************************************************************************/
int main(int argc, const char *argv[])
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
