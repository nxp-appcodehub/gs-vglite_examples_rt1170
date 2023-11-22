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

#include "picture.h"
/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define DEFAULT_WIDTH 720
#define DEFAULT_HEIGHT 1280

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
/*******************************************************************************
 * Prototypes
 ******************************************************************************/
static void vglite_task(void *pvParameters);

/*******************************************************************************
 * Variables
 ******************************************************************************/
static vg_lite_buffer_t renderTarget;
static vg_lite_buffer_t image;
static vg_lite_display_t display;
static vg_lite_window_t  window;
vg_lite_filter_t mainFilter;

/*
 *-----*
 /       \
 /         \
 *           *
 |          /
 |         X
 |          \
 *           *
 \         /
 \       /
 *-----*
 */
static int8_t path_data[] = {
    2, -5, -10, // moveto   -5,-10
    4, 5, -10,  // lineto    5,-10
    4, 10, -5,  // lineto   10, -5
    4, 0, 0,    // lineto    0,  0
    4, 10, 5,   // lineto   10,  5
    4, 5, 10,   // lineto    5, 10
    4, -5, 10,  // lineto   -5, 10
    4, -10, 5,  // lineto  -10,  5
    4, -10, -5, // lineto  -10, -5
    0, // end
};

static vg_lite_path_t path = {
    {-10, -10, // left,top
        10, 10}, // right,bottom
    VG_LITE_HIGH, // quality
    VG_LITE_S8, // -128 to 127 coordinate range
    {0}, // uploaded
    sizeof(path_data), // path length
    path_data, // path data
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
    vg_lite_free(&renderTarget);
    vg_lite_clear_path(&path);
    vg_lite_close();
}

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

static vg_lite_error_t init_vg_lite(void)
{
    vg_lite_error_t error = VG_LITE_SUCCESS;

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
    error = vg_lite_init(256, 256);
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

    if(vg_lite_set_image(&image, (uint8_t *) &picture_raw, IMG_WIDTH, IMG_HEIGHT, IMG_STRIDE, IMG_FORMAT))
    {
        PRINTF("Could not load image\n\r");
        cleanup();
        return error;
    }

    renderTarget.width = DEFAULT_WIDTH;
    renderTarget.height = DEFAULT_HEIGHT;
    renderTarget.format = VG_LITE_RGB565;
    error = vg_lite_allocate(&renderTarget);
    if (VG_LITE_SUCCESS != error) 
    {
        PRINTF("Could not allocate Main Screen Render Target\n\r");
        cleanup();
        return error;
    }
    
    return error;
}

static void redraw()
{
    vg_lite_matrix_t matrix, matPath;
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
    vg_lite_clear(&renderTarget, NULL, 0xFFFF0000);
   
    // Build a 33 degree rotation matrix from the center of the buffer.
    vg_lite_identity(&matrix);
    vg_lite_translate(DEFAULT_WIDTH / 2, DEFAULT_HEIGHT / 4, &matrix);
    vg_lite_rotate(33.0f, &matrix);
    vg_lite_translate(DEFAULT_WIDTH / -4.0f, DEFAULT_HEIGHT / -6.0f, &matrix);
    vg_lite_identity(&matPath);
    vg_lite_translate(DEFAULT_WIDTH / 2.0f, DEFAULT_HEIGHT / 4.0f, &matPath);
    vg_lite_scale(25, 25, &matPath);

    //vg_lite_blit(&image, &matrix, VG_LITE_BLEND_NONE, 0, VG_LITE_FILTER_POINT);
    // Fill the path using an image with the VG_LITE_PATTERN_COLOR mode.
    error =
        vg_lite_draw_pattern(&renderTarget, &path, VG_LITE_FILL_EVEN_ODD, &matPath, &image, &matrix, VG_LITE_BLEND_NONE, VG_LITE_PATTERN_COLOR, 0xffaabbcc, mainFilter);

    if (error != VG_LITE_SUCCESS) 
    {
        PRINTF("Patter fill is not supported.\n");
        cleanup();
        return ;
    }

    // Build a 33 degree rotation matrix from the center of the buffer.
    vg_lite_identity(&matrix);
    vg_lite_translate(DEFAULT_WIDTH / 2.0f, (3 * DEFAULT_HEIGHT) / 4, &matrix);
    vg_lite_rotate(33.0f, &matrix);
    vg_lite_translate(DEFAULT_WIDTH / -4.0f, DEFAULT_HEIGHT / -6.0f, &matrix);

    vg_lite_identity(&matPath);

    vg_lite_translate(DEFAULT_WIDTH / 2.0f, (3 * DEFAULT_HEIGHT) / 4, &matPath);
    
    vg_lite_scale(25, 25, &matPath);

    // Fill the path using an image with the VG_LITE_PATTERN_PAD mode.
    vg_lite_draw_pattern(&renderTarget, &path, VG_LITE_FILL_EVEN_ODD, &matPath, &image, &matrix, VG_LITE_BLEND_NONE, VG_LITE_PATTERN_PAD, 0xffaabbcc, mainFilter);
    vg_lite_finish();

    vg_lite_identity(&matrix);
  
    
    error = vg_lite_blit(rt, &renderTarget, &matrix, VG_LITE_BLEND_NONE, 0, mainFilter);
    if (error)
    {
        PRINTF("vg_lite_blit() returned error %d\n", error);
        cleanup();
        return;
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
