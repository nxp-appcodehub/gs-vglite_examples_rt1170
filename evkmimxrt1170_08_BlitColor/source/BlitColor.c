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
#define DROPPER565       0
#define DROPPER4444      1
#define DROPPER8888      2
#if defined(CPU_MIMXRT595SFFOC_cm33) // only RT500 supports this format, RT1170 and RT1160 are not suitable
#define DROPPER2222      3
#endif
#define DROPPER5551      4
#define DROPPERL8        5
#define DROPPERA4        6
#define DROPPERA8        7
#define DROPPERYUYV      8
#define DROPPER_FORMAT   DROPPER8888

#if(DROPPER_FORMAT==DROPPER565)
#include "DropperMini565.h"
#elif (DROPPER_FORMAT==DROPPER4444)
#include "DropperMini4444.h"
#elif (DROPPER_FORMAT==DROPPER8888)
#include "DropperMini8888.h"
#elif defined(DROPPER2222) && (DROPPER_FORMAT==DROPPER2222)
#include "DropperMini2222.h"
#elif (DROPPER_FORMAT==DROPPER5551)
#include "DropperMini5551.h"
#elif (DROPPER_FORMAT==DROPPERL8)
#include "DropperMiniL8.h"
#elif (DROPPER_FORMAT==DROPPERA4)
#include "DropperMiniA4.h"
#elif (DROPPER_FORMAT==DROPPERA8)
#include "DropperMiniA8.h"
#else
#include "DropperMiniYUYV.h"
#endif

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

#define OFFSCREEN_BUFFER_WIDTH    520
#define OFFSCREEN_BUFFER_HEIGHT   260

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
static void vglite_task(void *pvParameters);

/*******************************************************************************
 * Variables
 ******************************************************************************/
static vg_lite_display_t display;
static vg_lite_window_t  window;

vg_lite_buffer_t renderTarget;
vg_lite_filter_t mainFilter;
vg_lite_matrix_t matrix;
vg_lite_buffer_t dropper;


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
    vg_lite_free(&dropper);
    vg_lite_close();
}

static vg_lite_error_t init_vg_lite(void)
{
    int j, i;
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
    error = vg_lite_init(OFFSCREEN_BUFFER_WIDTH, OFFSCREEN_BUFFER_HEIGHT);
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


    /* Allocate an offscreen buffer where the path will be rendered */
    renderTarget.width  = OFFSCREEN_BUFFER_WIDTH;
    renderTarget.height = OFFSCREEN_BUFFER_HEIGHT;
    renderTarget.format = VG_LITE_RGBA8888;
    renderTarget.tiled  = VG_LITE_TILED;
    error = vg_lite_allocate(&renderTarget);
    if (VG_LITE_SUCCESS != error) 
    {
        PRINTF("Could not allocate Main Screen Render Target\n\r");
        cleanup();
        return error;
    }

    /* Load the image data to a vg_lite_buffer */
    dropper.width  = IMG_WIDTH;
    dropper.height = IMG_HEIGHT;
    dropper.stride = IMG_STRIDE;
    dropper.format = IMG_FORMAT;

    error = vg_lite_allocate(&dropper);
    if (VG_LITE_SUCCESS != error) 
    {
        PRINTF("Could not allocate Dropper Buffer\n\r");
        cleanup();
        return error;
    }

    PRINTF("Dropper Buffer: Width:%d; Height:%d; Stride:%d\n\r", dropper.width, dropper.height, dropper.stride);

    /* Copy the data to the buffer */
    uint8_t * buffer_memory = (uint8_t *) dropper.memory;
#if(DROPPER_FORMAT==DROPPER565)
    uint8_t  *pdata = (uint8_t *) BGR565_Data;
#elif (DROPPER_FORMAT==DROPPER4444)
    uint8_t  *pdata = (uint8_t *) BGRA4444_Data;
#elif (DROPPER_FORMAT==DROPPER8888)
    uint8_t  *pdata = (uint8_t *) BGRA8888_Data;
#elif defined(DROPPER2222) && (DROPPER_FORMAT==DROPPER2222)
    uint8_t  *pdata = (uint8_t *) BGRA2222_Data;
#elif (DROPPER_FORMAT==DROPPER5551)
    uint8_t  *pdata = (uint8_t *) BGRA5551_Data;
#elif (DROPPER_FORMAT==DROPPERL8)
    uint8_t  *pdata = (uint8_t *) L8_Data;
#elif (DROPPER_FORMAT==DROPPERA4)
    uint8_t  *pdata = (uint8_t *) A4_Data;
#elif (DROPPER_FORMAT==DROPPERA8)
    uint8_t  *pdata = (uint8_t *) A8_Data;
#else
    uint8_t  *pdata = (uint8_t *) YUYV_Data;
#endif

    for (j = 0; j < dropper.height; j++)
    {
        memcpy(buffer_memory, pdata, dropper.stride);
        buffer_memory += dropper.stride;
        pdata += dropper.stride;
    }
    
    return error;
}

static void redraw()
{
    static vg_lite_float_t rAngle = 0;
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
    vg_lite_clear(&renderTarget, NULL, 0xFFF0F0F0);
   
    /* Define the transformation matrix that will be applied */
    vg_lite_identity(&matrix);
    vg_lite_translate(2, 2, &matrix);

    /* Blit the buffer to the render target */
#if (DROPPER_FORMAT==DROPPERA4 || DROPPER_FORMAT==DROPPERA8)
    dropper.image_mode = VG_LITE_MULTIPLY_IMAGE_MODE;
#else
    dropper.image_mode = VG_LITE_NORMAL_IMAGE_MODE;
#endif
    error = vg_lite_blit(&renderTarget, &dropper, &matrix, VG_LITE_BLEND_SRC_OVER, 0xFF000000, mainFilter);
    if (error) {
        PRINTF("vg_lite_draw() returned error %d\n", error);
        cleanup();
        return;
    }

    /* Blit the buffer, and multiply it by a color */
    vg_lite_translate(260, 0, &matrix);
    dropper.image_mode = VG_LITE_MULTIPLY_IMAGE_MODE;
    error = vg_lite_blit(&renderTarget, &dropper, &matrix, VG_LITE_BLEND_SRC_OVER, 0xFF00FF00, mainFilter);
     if (error) {
        PRINTF("vg_lite_draw() returned error %d\n", error);
        cleanup();
        return;
    }

    /* Clear whole screen with solid blue */
    vg_lite_clear(rt, NULL, 0xFFFF0000);
    vg_lite_identity(&matrix);
    dropper.image_mode = VG_LITE_NORMAL_IMAGE_MODE;
    vg_lite_translate(DEMO_PANEL_WIDTH/2.0, DEMO_PANEL_HEIGHT/2.0, &matrix);
    vg_lite_rotate(rAngle, &matrix);
    error = vg_lite_blit(rt, &renderTarget, &matrix, VG_LITE_BLEND_SRC_OVER, 0, mainFilter);
    if (error)
    {
        PRINTF("vg_lite_blit() returned error %d\n", error);
        cleanup();
        return;
    }

    rAngle += 0.3;
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

    status = BOARD_PrepareVGLiteController();
    if (status != kStatus_Success)
    {
        PRINTF("Prepare VGlite contolor error\r\n");
        while (1)
            ;
    }

    error = init_vg_lite();
    if (error) {
        PRINTF("init_vg_lite failed: init_vg_lite() returned error %d\r\n", error);
        while (1)
            ;
    }

    uint32_t startTime, time, n = 0;
    startTime =  getTime();

    while (1)
    {
        redraw();
        if(n++ >= 59)
        {
          time = getTime() - startTime;
          PRINTF("%d frames in %d seconds: %d fps\r\n", n, time/1000, n*1000/time);
          n = 0;
          startTime =  getTime();
        }
    }
}
