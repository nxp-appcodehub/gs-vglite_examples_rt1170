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

#define OFFSCREEN_BUFFER_WIDTH    400
#define OFFSCREEN_BUFFER_HEIGHT   400
/*******************************************************************************
 * Prototypes
 ******************************************************************************/
static void vglite_task(void *pvParameters);

/*******************************************************************************
 * Variables
 ******************************************************************************/
static vg_lite_display_t display;
static vg_lite_window_t  window;

vg_lite_radial_gradient_spreadmode_t spreadmode[] = {
    VG_LITE_RADIAL_GRADIENT_SPREAD_FILL,
    VG_LITE_RADIAL_GRADIENT_SPREAD_PAD,
    VG_LITE_RADIAL_GRADIENT_SPREAD_REPEAT,
    VG_LITE_RADIAL_GRADIENT_SPREAD_REFLECT,
};

static int16_t path_data[] = {
    2, 0, 0,
    4, 160, 0,
    4, 160, 160,
    4, 0, 160,
    0,
};

static vg_lite_path_t path = {
    { 0,   0,
      160, 160 },
    VG_LITE_HIGH,
    VG_LITE_S16,
    {0},
    sizeof(path_data),
    path_data,
    1
};
/*******************************************************************************
 * Code
 ******************************************************************************/
static vg_lite_error_t render_linear_gradient(vg_lite_buffer_t * fb,uint32_t fcount)
{
    uint32_t x, y;
    static vg_lite_linear_gradient_ext_t grad;

    static vg_lite_color_ramp_t vgColorRamp[] =
    {
        {
            0.0f,
            0.4f, 0.0f, 0.6f, 1.0f
        },
        {
            0.25f,
            0.9f, 0.5f, 0.1f, 1.0f
        },
        {
            0.5f,
            0.8f, 0.8f, 0.0f, 1.0f
        },
        {
            0.75f,
            0.0f, 0.3f, 0.5f, 1.0f
        },
        {
            1.00f,
            0.4f, 0.0f, 0.6f, 1.0f
        }
    };
    vg_lite_linear_gradient_parameter_t radialGradient = {160.0f, 100.0f, 480.0f, 100.0f};
    vg_lite_matrix_t *matGrad;
    vg_lite_matrix_t matPath;
    vg_lite_error_t error = VG_LITE_SUCCESS;

    if(fcount >=3)
        fcount = 3;

    memset(&grad, 0, sizeof(grad));
    error = vg_lite_set_linear_grad(&grad, 5, vgColorRamp, radialGradient, spreadmode[fcount], 1);
    error = vg_lite_update_linear_grad(&grad);
    matGrad = vg_lite_get_linear_grad_matrix(&grad);
    vg_lite_identity(matGrad);

    vg_lite_identity(&matPath);
    vg_lite_scale(2.0f, 2.0f, &matPath);
  
    switch(fcount)
    {
    case 0:
        x = y =0;
        break;
    case 1:
        x = 1; y = 0;
        break;
    case 2:
        x = 0; y = 1;
        break;
    case 3:
        x = 1; y = 1;
        break;
    default:
        x = y =0;
        break;        
    }
    vg_lite_translate(10.0f + x * 160, 100.0f + y * 160, &matPath);

    error = vg_lite_draw_linear_gradient(fb, &path, VG_LITE_FILL_EVEN_ODD, &matPath, &grad, 0, VG_LITE_BLEND_NONE, VG_LITE_FILTER_LINEAR);
    error = vg_lite_finish();
    error = vg_lite_clear_linear_grad(&grad);

    return error;
}
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

static vg_lite_error_t init_vg_lite(void)
{
    vg_lite_error_t error = VG_LITE_SUCCESS;

    error = VGLITE_CreateDisplay(&display);
    if (error)
    {
        PRINTF("VGLITE_CreateDisplay failed: VGLITE_CreateDisplay() returned error %d\n", error);
        return error;
    }
    /* Initialize the window */
    error = VGLITE_CreateWindow(&display, &window);
    if (error)
    {
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
    vg_lite_clear(rt, NULL, 0xFFFFFFFF);
   
    for(int i = 0; i < sizeof(spreadmode)/sizeof(spreadmode[0]); i++) {
        error = render_linear_gradient(rt, i);
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
    if (error)
    {
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
