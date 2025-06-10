/*****************************************************************************
* | File      	:   LCD_1IN47_LVGL_test.c
* | Author      :   Waveshare team
* | Function    :   1.47inch LCD LVGL demo
* | Info        :
*----------------
* |	This version:   V1.0
* | Date        :   2025-01-07
* | Info        :
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documnetation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to  whom the Software is
# furished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
******************************************************************************/
#include "LCD_Test.h"
#include "LCD_1in47.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#include "hardware/pio.h"
#include "ws2812.pio.h"
 
static void Widgets_Init(lvgl_data_struct *dat);
static void LCD_1IN47_KEY_Init(void);
static uint16_t LCD_1IN47_Read_KEY(void);

#define IS_RGBW false
#define NUM_PIXELS 1
#define WS2812_PIN 22

bool __no_inline_not_in_flash_func(get_bootsel_button)() {
    const uint CS_PIN_INDEX = 1;

    // Must disable interrupts, as interrupt handlers may be in flash, and we
    // are about to temporarily disable flash access!
    uint32_t flags = save_and_disable_interrupts();

    // Set chip select to Hi-Z
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    // Note we can't call into any sleep functions in flash right now
    for (volatile int i = 0; i < 1000; ++i);

    // The HI GPIO registers in SIO can observe and control the 6 QSPI pins.
    // Note the button pulls the pin *low* when pressed.
    #define CS_BIT SIO_GPIO_HI_IN_QSPI_CSN_BITS

    bool button_state = !(sio_hw->gpio_hi_in & CS_BIT);

    // Need to restore the state of chip select, else we are going to have a
    // bad time when we return to code in flash!
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    restore_interrupts(flags);

    return button_state;
}
 
static inline void put_pixel(PIO pio, uint sm, uint32_t pixel_grb) {
    pio_sm_put_blocking(pio, sm, pixel_grb << 8u);
}

static inline uint8_t hue_to_rgb(uint16_t temp1, uint16_t temp2, uint16_t hue) {
    // The hue is divided into 6 sectors of approx 42.5 units each (255 / 6).
    // The thresholds are 43, 128, and 171.
    if (hue < 43) {
        // First sector: rising slope
        return temp2 + ((uint32_t)(temp1 - temp2) * hue * 6) / 255;
    }
    if (hue < 128) {
        // Second sector: flat top
        return temp1;
    }
    if (hue < 171) {
        // Third sector: falling slope
        // (171 - hue) maps the hue from 170-128 back to 0-42
        return temp2 + ((uint32_t)(temp1 - temp2) * (171 - hue) * 6) / 255;
    }
    // Remaining sectors: flat bottom
    return temp2;
}

static inline uint32_t hsl2rgb(uint8_t h, uint8_t s, uint8_t l) {
    uint8_t r, g, b;

    if (s == 0) {
        // Achromatic (grayscale)
        r = g = b = l;
    } else {
        uint16_t temp1, temp2;
        uint16_t lum = l;
        uint16_t sat = s;

        // Calculate temporary variables, scaling calculations by 255
        if (lum < 128) {
            temp1 = (lum * (255 + sat)) / 255;
        } else {
            // Using a 32-bit intermediate for the multiplication to prevent overflow
            temp1 = lum + sat - ((uint32_t)lum * sat / 255);
        }

        temp2 = 2 * lum - temp1;

        // Adjust hue for each channel. The hue circle (0-255) is offset by 1/3 (85)
        // for red and blue. We handle the wraparound.
        uint16_t h_r = h + 85;
        if (h_r > 255) h_r -= 255;

        uint16_t h_g = h;

        uint16_t h_b = h;
        if (h_b < 85) {
            h_b += 170;
        } else {
            h_b -= 85;
        }
        
        r = hue_to_rgb(temp1, temp2, h_r);
        g = hue_to_rgb(temp1, temp2, h_g);
        b = hue_to_rgb(temp1, temp2, h_b);
    }

    // Pack the RGB values into a uint32_t with the specified bit shifts
    return ((uint32_t)g << 8) | ((uint32_t)r << 16) | (uint32_t)b;
}

void pattern(PIO pio, uint sm, uint len, uint t) {
    uint32_t pixel_color = hsl2rgb(t, 0xff, 0x7f);

    for (uint i = 0; i < len; ++i) {
        put_pixel(pio, sm, pixel_color);
    }
}

/********************************************************************************
function:   Main function
parameter:
********************************************************************************/
int LCD_1in47_test(void)
{
    if(DEV_Module_Init()!=0){
        return -1;
    }

    // todo get free sm
    PIO pio;
    uint sm;
    uint offset;

    // This will find a free pio and state machine for our program and load it for us
    // We use pio_claim_free_sm_and_add_program_for_gpio_range (for_gpio_range variant)
    // so we will get a PIO instance suitable for addressing gpios >= 32 if needed and supported by the hardware
    bool success = pio_claim_free_sm_and_add_program_for_gpio_range(&ws2812_program, &pio, &sm, &offset, WS2812_PIN, 1, true);
    hard_assert(success);

    ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000, IS_RGBW);

    /* LCD Init */
    printf("1.47inch LCD LVGL demo...\r\n");
    LCD_1IN47_Init(VERTICAL);
    LCD_1IN47_Clear(WHITE);
   
    /*Config parameters*/
    LCD_SetWindows = LCD_1IN47_SetWindows;
    DISP_HOR_RES = LCD_1IN47_HEIGHT;
    DISP_VER_RES = LCD_1IN47_WIDTH;

    /*Init LVGL data structure*/    
    lvgl_data_struct *dat = (lvgl_data_struct *)malloc(sizeof(lvgl_data_struct));
    memset(dat->scr, 0, sizeof(dat->scr));
    dat->click_num = 0;
    
    /*Init LVGL*/
    LVGL_Init();
    Widgets_Init(dat);

    int t = 0;
    int screen_index = 0;
    bool is_pressed = false;
    bool was_pressed = false;

    while(1)
    {
        pattern(pio, sm, NUM_PIXELS, t);
        
        t++;
        if (t % 256 == 0) {
            t = 0;
        }

        is_pressed = get_bootsel_button();
        if (is_pressed && !was_pressed) {
            was_pressed = true;
            screen_index = (screen_index + 1) % 3;
            lv_scr_load(dat->scr[screen_index]);
        } else
        if (was_pressed && !is_pressed) {
            was_pressed = false;
        }

        lv_task_handler();
        DEV_Delay_ms(5); 
    }
    
    DEV_Module_Exit();
    pio_remove_program_and_unclaim_sm(&ws2812_program, pio, sm, offset);

    return 0;
}

/********************************************************************************
function:   Initialize Widgets
parameter:
********************************************************************************/
static void Widgets_Init(lvgl_data_struct *dat)
{
    static lv_style_t style_label;
    lv_style_init(&style_label);
    lv_style_set_text_font(&style_label, &lv_font_montserrat_16);

    /*Screen1: Just a picture*/
    dat->scr[0] = lv_obj_create(NULL);
    dat->scr[1] = lv_obj_create(NULL);
    dat->scr[2] = lv_obj_create(NULL);
    
    /*Declare and load the image resource*/
    LV_IMG_DECLARE(julian);
    LV_IMG_DECLARE(julian2);
    LV_IMG_DECLARE(julian3);

    lv_obj_t *img1 = lv_img_create(dat->scr[0]);
    lv_img_set_src(img1, &julian);
    lv_obj_align(img1, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *img2 = lv_img_create(dat->scr[1]);
    lv_img_set_src(img2, &julian2);
    lv_obj_align(img2, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *img3 = lv_img_create(dat->scr[2]);
    lv_img_set_src(img3, &julian3);
    lv_obj_align(img3, LV_ALIGN_CENTER, 0, 0);

    /*Load the first screen as the current active screen*/
    lv_scr_load(dat->scr[0]);
}
