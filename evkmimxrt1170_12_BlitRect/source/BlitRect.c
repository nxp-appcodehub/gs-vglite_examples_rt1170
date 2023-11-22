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
#include "Numbas.h"
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

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
static void vglite_task(void *pvParameters);

/*******************************************************************************
 * Variables
 ******************************************************************************/
static vg_lite_display_t display;
static vg_lite_window_t  window;
vg_lite_buffer_t glyphBuffer;

static vg_lite_matrix_t matrix;

vg_lite_filter_t mainFilter;

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
    vg_lite_close();
}

static vg_lite_error_t load_glyphatlas()
{
  vg_lite_error_t error = VG_LITE_SUCCESS;
  glyphBuffer.width = 160;
  glyphBuffer.height = 92;
  glyphBuffer.format = VG_LITE_RGBA8888;
  error = vg_lite_allocate(&glyphBuffer);
  if (VG_LITE_SUCCESS == error) 
  {
    uint8_t * buffer_memory = (uint8_t *) glyphBuffer.memory;
    uint8_t  *pdata = (uint8_t *) Numbas_Bitmap0;
    for (int j = 0; j < glyphBuffer.height; j++) 
    {
      memcpy(buffer_memory, pdata, glyphBuffer.stride);
      buffer_memory += glyphBuffer.stride;
      pdata += glyphBuffer.stride;
    }  
  }
  return(error);
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
    error = vg_lite_init(64, 64);
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
    
    error = load_glyphatlas();
    if (error)
    {
        PRINTF("load_bubble failed: load_bubble() returned error %d\n", error);
        cleanup();
        return error;
    }
    
    return error;
}

static void redraw()
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    int status = 0;
    uint32_t rect[4];

    vg_lite_buffer_t *rt = VGLITE_GetRenderTarget(&window);
    if(rt == NULL)
    {
        PRINTF("vg_lite_get_renderTarget error\r\n");
        while (1)
            ;
    }

    /* Clear the buffer with a solid color, the color format is ABGR  */
    vg_lite_clear(rt, NULL, 0xFFFFFFFF);
   
    //We are going to Render the 2986 string.
    //2 is located in: 32,0
    //9 is located in: 96,46
    //8 is located in: 64,46
    //6 is located in: 0,46
    //Draw the first glyph
    /* Define the transformation matrix that will be applied */
    vg_lite_identity(&matrix);
    rect[0] = 32; rect[1] = 0; rect[2] = 32; rect[3] = 46;
    error = vg_lite_blit_rect(rt, &glyphBuffer, rect, &matrix, VG_LITE_BLEND_SRC_OVER, 0, mainFilter);
    if (error!= VG_LITE_SUCCESS)
    {
        PRINTF("vg_lite_blit_rect() returned error %d\n", error);
        cleanup();
        return;
    }
    
    vg_lite_translate(34,0,&matrix);
    rect[0] = 96; rect[1] = 46; rect[2] = 32; rect[3] = 46;
    error = vg_lite_blit_rect(rt, &glyphBuffer, rect, &matrix, VG_LITE_BLEND_SRC_OVER, 0, mainFilter);
    if (error!= VG_LITE_SUCCESS)
    {
        PRINTF("vg_lite_blit_rect() returned error %d\n", error);
        cleanup();
        return;
    }

    vg_lite_translate(34,0,&matrix);
    rect[0] = 64; rect[1] = 46; rect[2] = 32; rect[3] = 46;
    error = vg_lite_blit_rect(rt, &glyphBuffer, rect, &matrix, VG_LITE_BLEND_SRC_OVER, 0, mainFilter);
    if (error!= VG_LITE_SUCCESS)
    {
        PRINTF("vg_lite_blit_rect() returned error %d\n", error);
        cleanup();
        return;
    }

    vg_lite_translate(34,0,&matrix);
    rect[0] = 0; rect[1] = 46; rect[2] = 32; rect[3] = 46;
    error = vg_lite_blit_rect(rt, &glyphBuffer, rect, &matrix, VG_LITE_BLEND_SRC_OVER, 0, mainFilter);
    if (error!= VG_LITE_SUCCESS)
    {
        PRINTF("vg_lite_blit_rect() returned error %d\n", error);
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
