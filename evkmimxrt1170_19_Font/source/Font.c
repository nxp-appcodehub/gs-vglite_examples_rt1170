/*
 * Copyright 2019, 2021, 2023 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* FreeRTOS kernel includes. */
#include "FreeRTOS.h"
#include "task.h"

#include "fsl_debug_console.h"
#include "pin_mux.h"
#include "board.h"

#include "vglite_support.h"
#include "vglite_window.h"
/*-----------------------------------------------------------*/
#include "vg_lite.h"
#include "vg_lite_text.h"
#include "fsl_soc_src.h"
#include "freesans.h"
/*******************************************************************************
 * Definitions
 ******************************************************************************/
/*******************************************************************************
 * Prototypes
 ******************************************************************************/
static void vglite_task(void *pvParameters);

/*******************************************************************************
 * Variables
 ******************************************************************************/
static vg_lite_display_t display;
static vg_lite_window_t window;

static vg_lite_matrix_t matrix;

static vg_lite_font_params_t font_params = {
        .font_type      = eFontTypeVector,
        .font_weight    = eFontWeightRegular,
        .font_stretch   = eFontStretchNormal,
        .font_style     = eFontStyleNormal,
        .font_height    = 35
};
static vg_lite_font_attributes_t font_attribs = {
        .justify        = 0,
        .alignment      = eTextAlignLeft,
        .width          = 1200,
        .height         = 50,
        .text_color     = OPAQUE_VGLITE_COLOUR(0, 0, 0), /* black */
        .bg_color       = OPAQUE_VGLITE_COLOUR(0xff, 0xff, 0xff), /* white */
        .tspan_has_dx_dy = 0,
        .margin         = 5,
        .anchor         = 0,
        .scale          = 1,
        .font_height    = 34,
        .last_x         = 0,
        .last_y         = 0,
        .last_dx        = 0
};

static vg_lite_font_t font = VG_LITE_INVALID_FONT;

extern unsigned char freesans_vft[];

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
    vg_lite_unregister_font(font);
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
    // Initialize the window.
    error = VGLITE_CreateWindow(&display, &window);
    if (error)
    {
        PRINTF("VGLITE_CreateWindow failed: VGLITE_CreateWindow() returned error %d\n", error);
        return error;
    }
    // Initialize the draw.
    error = vg_lite_init(DEFAULT_VG_LITE_TW_WIDTH, DEFAULT_VG_LITE_TW_HEIGHT);
    if (error)
    {
        PRINTF("vg_lite engine init failed: vg_lite_init() returned error %d\n", error);
        cleanup();
        return error;
    }
    // Set GPU command buffer size for this drawing task.
    error = vg_lite_set_command_buffer_size(VG_LITE_COMMAND_BUFFER_SIZE);
    if (error)
    {
        PRINTF("vg_lite_set_command_buffer_size() returned error %d\n", error);
        cleanup();
        return error;
    }

    // Register font
    strcpy(font_params.name, FONT_NAME);
    
    font_params.data        = freesans_vft;
    font_params.data_len    = FONT_VFT_LEN;

    error = vg_lite_register_font(&font, &font_params);
    if (error != VG_LITE_SUCCESS) {
        PRINTF("ERROR: Failed to load vector font (err = %d)!\r\n\r\n", error);
        cleanup();
        return error;
    }

    return error;
}

static void redraw()
{
    vg_lite_error_t error = VG_LITE_SUCCESS;


    vg_lite_buffer_t *rt = VGLITE_GetRenderTarget(&window);
    if (rt == NULL)
    {
        PRINTF("vg_lite_get_renderTarget error\r\n");
        while (1)
            ;
    }

    font_attribs.is_vector_font = vg_lite_is_vector_font(font);
    
    /* Clear the screen with a bluish colour */
    vg_lite_clear(rt, NULL, OPAQUE_VGLITE_COLOUR(0xff, 0xa4, 0x48));
    
    vg_lite_identity(&matrix);
    vg_lite_translate(50, 100, &matrix);
    error = vg_lite_draw_text(rt, "I truly believe NXP will be at the", font,
                               0, 0, &matrix, VG_LITE_BLEND_SRC_OVER, &font_attribs);
    if (error != VG_LITE_SUCCESS)
      goto text_draw_error;
    
    vg_lite_translate(0, 50, &matrix);
    error = vg_lite_draw_text(rt, "forefront of technological solutions", font,
                               0, 0, &matrix, VG_LITE_BLEND_SRC_OVER, &font_attribs);
    if (error != VG_LITE_SUCCESS)
      goto text_draw_error;
    
    vg_lite_translate(0, 50, &matrix);
    error = vg_lite_draw_text(rt, "that will leave their mark on society.",
                               font, 0, 0, &matrix, VG_LITE_BLEND_SRC_OVER, &font_attribs);
    if (error != VG_LITE_SUCCESS)
      goto text_draw_error;

    vg_lite_translate(0, 50, &matrix);
    error = vg_lite_draw_text(rt, "Now more than ever, we have a key",
                               font, 0, 0, &matrix, VG_LITE_BLEND_SRC_OVER, &font_attribs);
    if (error != VG_LITE_SUCCESS)
      goto text_draw_error;

    vg_lite_translate(0, 50, &matrix);
    error = vg_lite_draw_text(rt, "role to play in our increasingly digital",
                               font, 0, 0, &matrix, VG_LITE_BLEND_SRC_OVER, &font_attribs);
    if (error != VG_LITE_SUCCESS)
      goto text_draw_error;

    vg_lite_translate(0, 50, &matrix);
    error = vg_lite_draw_text(rt, "world as we enable a safer society.",
                               font, 0, 0, &matrix, VG_LITE_BLEND_SRC_OVER, &font_attribs);
    if (error != VG_LITE_SUCCESS)
      goto text_draw_error;

    vg_lite_translate(0, 50, &matrix);
    error = vg_lite_draw_text(rt, "through secure contactless interactions,",
                               font, 0, 0, &matrix, VG_LITE_BLEND_SRC_OVER, &font_attribs);
    if (error != VG_LITE_SUCCESS)
      goto text_draw_error;

    vg_lite_translate(0, 50, &matrix);
    error = vg_lite_draw_text(rt, "more AI and ML-based sensors for",
                               font, 0, 0, &matrix, VG_LITE_BLEND_SRC_OVER, &font_attribs);
    if (error != VG_LITE_SUCCESS)
      goto text_draw_error;

    vg_lite_translate(0, 50, &matrix);
    error = vg_lite_draw_text(rt, "medical equipment and treatments as well",
                               font, 0, 0, &matrix, VG_LITE_BLEND_SRC_OVER, &font_attribs);
    if (error != VG_LITE_SUCCESS)
      goto text_draw_error;

    vg_lite_translate(0, 50, &matrix);
    error = vg_lite_draw_text(rt, "as innovations for sustainable and",
                               font, 0, 0, &matrix, VG_LITE_BLEND_SRC_OVER, &font_attribs);
    if (error != VG_LITE_SUCCESS)
      goto text_draw_error;

    vg_lite_translate(0, 50, &matrix);
    error = vg_lite_draw_text(rt, "environmental developments, like",
                               font, 0, 0, &matrix, VG_LITE_BLEND_SRC_OVER, &font_attribs);
    if (error != VG_LITE_SUCCESS)
      goto text_draw_error;

    vg_lite_translate(0, 50, &matrix);
    error = vg_lite_draw_text(rt, "automotive electrification and safer",
                               font, 0, 0, &matrix, VG_LITE_BLEND_SRC_OVER, &font_attribs);
    if (error != VG_LITE_SUCCESS)
      goto text_draw_error;

    vg_lite_translate(0, 50, &matrix);
    error = vg_lite_draw_text(rt, "driving.",
                               font, 0, 0, &matrix, VG_LITE_BLEND_SRC_OVER, &font_attribs);
    if (error != VG_LITE_SUCCESS)
      goto text_draw_error;
    
    vg_lite_translate(0, 50, &matrix);
    error = vg_lite_draw_text(rt, "-Kurt Sievers, President and CEO, NXP", font, 0, 0,
                               &matrix, VG_LITE_BLEND_SRC_OVER, &font_attribs);
    if (error != VG_LITE_SUCCESS)
      goto text_draw_error;
    
    vg_lite_translate(0, 50, &matrix);
    error = vg_lite_draw_text(rt, "Using font: "FONT_NAME, font, 0, 0,
                               &matrix, VG_LITE_BLEND_SRC_OVER, &font_attribs);
    if (error != VG_LITE_SUCCESS)
      goto text_draw_error;
    
    VGLITE_SwapBuffers(&window);

    return;
    
text_draw_error:
    PRINTF("vg_lite_draw_text() returned error %d\n", error);
    cleanup();
    return;
}

uint32_t getTime()
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
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
