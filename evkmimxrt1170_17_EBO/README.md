# evkmimxrt1170_17_EBO

Clear an offscreen with white color and draw a rotating tiger to it, then blit it to the center of display that has a green background.

## Hardware Preparation

If using the **MIPI** interface, connect the LCD displayer to **J48** on the MIMXRT1170-EVK board. Connect 5V power to **J43**, set **J38** to **1-2**, and turn on the power switch **SW5**.

## Software Preparation

Now three LCD displayers are supported, which are defined in [**display_support.h**](../common/board/display_support.h):

``` C
#define DEMO_PANEL_RK055AHD091 0 /* 720 * 1280, RK055AHD091-CTG(RK055HDMIPI4M) */
#define DEMO_PANEL_RK055IQH091 1 /* 540 * 960,  RK055IQH091-CTG */
#define DEMO_PANEL_RK055MHD091 2 /* 720 * 1280, RK055MHD091A0-CTG(RK055HDMIPI4MA0) */
```

Use the macro **DEMO_PANEL** to select the LCD panel you are using, the default panel is **RK055AHD091-CTG** configured in the display_support.h:

``` C
#define DEMO_PANEL DEMO_PANEL_RK055AHD091
```

For example, if your LCD panel is **RK055MHD091A0-CTG**, change the macro **DEMO_PANEL** definition as following:

``` C
#define DEMO_PANEL DEMO_PANEL_RK055MHD091
```

The source code is in [**EBOLoading.c**](./source/EBOLoading.c) (EBO means **Elementary Bitmap Object**), where the *main* function first configures clocks, pins, etc. **freerots** is deployed in the example. **vglite_task** is created and scheduled to execute VGLite initialization and drawing task.

In addition, the [**tiger.h**](./source/tiger.h) file includes an array storing the bitmap data:

``` C
#define IMAGE_WIDTH     320
#define IMAGE_HEIGHT    240
unsigned char tiger_ebo[] = {
    ......
    ......
    ......
};
unsigned int tiger_ebo_len = 307240;
```

## VGLite Initialization

Similar to [*evkmimxrt1170_15_EVOLoading*](../evkmimxrt1170_15_EVOLoading),
This project uses **Elementary** to load bitmaps, so there are some different functions in the initialization compared to previous VGLite projects.

* **`ElmInitialize`** initializes and configures the tessellation buffer size, similar to the `vg_lite_init` function.

* **`ElmWrapBuffer`** maps the **vg_lite_buffer_t** structure to the elementary buffer **ElmBuffer**, make elementary rendered to the screen directly.

* **`ElmCreateObjectFromData`** loads the data with specified type defined by **ELM_OBJECT_TYPE** enumeration, and return a **ElmHandle** structure for subsequent transformation and drawing. 
The **ELM_OBJECT_TYPE** enumeration includes six values:

    * **ELM_OBJECT_TYPE_EVO**: elementary vector object, representing a path object.
    * **ELM_OBJECT_TYPE_EGO**: elementary group object, containing multiple path objects.
    * **ELM_OBJECT_TYPE_EBO**: elementary bitmap object, representing image data.
    * **ELM_OBJECT_TYPE_BUF**: rendering buffer object, created by application.
    * **ELM_OBJECT_TYPE_FONT**: elementary font object, representing character data.
    * **ELM_OBJECT_TYPE_TEXT**: elementary text object, representing text data.
    
    Different from previous two projects loading vector data, in this project, `ElmCreateObjectFromData` is used to load the bitmap data with the type of **ELM_OBJECT_TYPE_EBO** instead of **ELM_OBJECT_TYPE_EVO**:
    
    ``` C
    handle = ElmCreateObjectFromData(ELM_OBJECT_TYPE_EBO, (void *)tiger_ebo, tiger_ebo_len);
    ```

And some generic VGLite functions are also used to do initialization:

* **`vg_lite_set_command_buffer_size`** sets the GPU command buffer size (optional).

* **`vg_lite_allocate`** allocates the render buffer, whose the input parameter is **vg_lite_buffer_t** structure defining width, height, and color format, etc.

The key code of initialization is shown below:

``` C
    status = ElmInitialize(OFFSCREEN_BUFFER_WIDTH, OFFSCREEN_BUFFER_HEIGHT);

    /* Set GPU command buffer size for this drawing task. */
    error = vg_lite_set_command_buffer_size(VGLITE_COMMAND_BUFFER_SZ);

    /* Allocate an offscreen buffer where the path will be rendered */
    renderTarget.width = OFFSCREEN_BUFFER_WIDTH;
    renderTarget.height = OFFSCREEN_BUFFER_HEIGHT;
    renderTarget.format = VG_LITE_RGBA8888;
    error = vg_lite_allocate(&renderTarget);

    /* Map the vg_lite_buffer to an elementary buffer where rendering is to be done */
    elmRenderTarget = ElmWrapBuffer(renderTarget.width, renderTarget.height, renderTarget.stride, 
                                    renderTarget.memory, renderTarget.address, ELM_BUFFER_FORMAT_RGBA8888);
  

    /* Load the EBO file from the buffer */
    handle = ElmCreateObjectFromData(ELM_OBJECT_TYPE_EBO, (void *)tiger_ebo, tiger_ebo_len);
```
## Drawing Task

Since this project renders the elementary bitmap data stored in [*tiger.h*](./source/tiger.h), the path array *pathData* and *vg_lite_path_t* structure is not used in this project.

As **Elementary** is used, there are also elementary functions replacing some VGLite functions:

* **`ElmReset`** restores the specified transformation prosperity or prosperities for the provided **ElmHandle** elementary object.

    The input mask property is defined by **ELM_EVO_PROP_BIT** enumeration, including
    * **ELM_PROP_ROTATE_BIT**: rotate bit of evo/ego/ebo transformation property.
    * **ELM_PROP_TRANSFER_BIT**: transfer bit of evo/ego/ebo transformation property.
    * **ELM_PROP_SCALE_BIT**: scale bit of evo/ego/ebo transformation property.
    * **ELM_PROP_BLEND_BIT**: blending bit of evo/ebo rendering property.
    * **ELM_PROP_QUALITY_BIT**: quality bit of evo/ebo rendering property.
    * **ELM_PROP_FILL_BIT**: fill rule bit of evo rendering property.
    * **ELM_PROP_COLOR_BIT**: fill color bit of evo rendering property.
    * **ELM_PROP_PAINT_BIT**: paint type bit of evo.
    * **ELM_PROP_ALL_BIT**: all transformation property bits of evo.
    
    In this project, *ELM_PROP_TRANSFER_BIT* and *ELM_PROP_SCALE_BIT* is reset to transfer and scale the bitmap subsequently:
    
    ``` C
    ElmReset(handle, ELM_PROP_TRANSFER_BIT | ELM_PROP_SCALE_BIT);
    ```

* **`ElmTransfer`** puts an evo/ebo/ego away at different directions. The setting will be accumulated until `ElmReset` is called.

* **`ElmRotate`** sets an evo/ebo/ego object rotated with specified angle. The setting will be accumulated until `ElmReset` is called.

* **`ElmScale`** scales up or down an evo/ego/ebo object at different directions. The setting will be accumulated until `ElmReset` is called.

In this project, the scaled bitmap rotates by 0.5 degrees each time with the following code:

    ``` C
    /* Below functions change the object matrix */
    ElmTransfer(handle, (OFFSCREEN_BUFFER_WIDTH - IMAGE_WIDTH)/2, (OFFSCREEN_BUFFER_HEIGHT - IMAGE_HEIGHT)/2);
    ElmTransfer(handle, IMAGE_WIDTH / 2, IMAGE_HEIGHT / 2);
    ElmRotate(handle, angle);
    ElmScale(handle, 0.8f, 0.8f);
    ElmTransfer(handle, - IMAGE_WIDTH / 2, - IMAGE_HEIGHT / 2);
    angle+=0.5;
    ```

* **`ElmSetBlend`** sets the blend mode of an evo/ebo object, which is not applied to group object (ebo). The bled mode is defined by **ELM_BLEND** enumeration, including

    * **ELM_BLEND_NONE**: D = S.
    * **ELM_BLEND_SRC_OVER**: D = S + (1 - Sa) * D
    * **ELM_BLEND_DST_OVER**: D = (1 - Da) * S + D
    * **ELM_BLEND_SRC_IN**: D = Da * S
    * **ELM_BLEND_DST_IN**: D = Sa * D
    * **ELM_BLEND_SCR**: D = S + D - S * D
    * **ELM_BLEND_MUL**: D = S * (1 - Da) + D * (1 - Sa) + S * D
    * **ELM_BLEND_ADD**: S + D
    * **ELM_BLEND_SUB**: D * (1 - S)

    In this project, *ELM_BLEND_SRC_OVER* mode is selected by
    ``` C
    ElmSetBlend(handle, ELM_BLEND_SRC_OVER);
    ```

* **`ElmDraw`** renders the object to the elementary buffer, taking all current setting of the elementary object, such as 

    ``` C
    ElmDraw(elmRenderTarget, handle);
    ```

And this project's drawing part also includes VGLite functions like `vg_lite_clear`, `vg_lite_identity`, `vg_lite_translate`, `vg_lite_blit`, etc:

* **`vg_lite_clear`** clears the render buffer with a solid color (**ABGR format**). 

* **`vg_lite_identity`** resets the specified transformation matrix, which is uninitialized or previously modified by functions of `vg_lite_translate`, `vg_lite_rotate`, `vg_lite_scale`.

* **`vg_lite_translate`** translates draw result by input coordinates with transformation matrix. 

* **`vg_lite_blit`** finally copies the source image to the destination window with the specified blend mode and filter mode, determining the showing of objects.

Once an error occurs, cleaning work is needed including the following functions:

* **`ElmDestroyObject`** releases all internal elementary resource of the input object.
    ``` C
    ElmDestroyObject(handle);
    ```

* **`vg_lite_free`** frees the allocated render buffer.

    ``` C
    vg_lite_free(&renderTarget);
    ```

* **`vg_lite_close`** finally frees up the entire memory initialized earlier by the `vg_lite_init` function.

    ``` C
    vg_lite_close();
    ```

## Run

Compile firstly, and use a Micro-USB cable to connect PC to **J86** on MIMXRT1170-EVK board, then download the firmware and run. 

If it's successful, the correct image will show on the displayer:

![evkmimxrt1170_17_EBO](../images/evkmimxrt1170_17_EBO.png)

And FPS information will be sent through UART serial port continuously. The correct UART configuration is

* 115200 baud rate
* 8 data bits
* No parity
* One stop bit
* No flow control