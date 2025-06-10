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
#include "hardware/pio.h"
#include "ws2812.pio.h"
 
static void Widgets_Init(lvgl_data_struct *dat);
static void LCD_1IN47_KEY_Init(void);
static uint16_t LCD_1IN47_Read_KEY(void);

#define IS_RGBW false
#define NUM_PIXELS 1

#ifdef PICO_DEFAULT_WS2812_PIN
#define WS2812_PIN PICO_DEFAULT_WS2812_PIN
#else
// default to pin 2 if the board doesn't have a default WS2812 pin defined
#define WS2812_PIN 22
#endif
 
static inline void put_pixel(PIO pio, uint sm, uint32_t pixel_grb) {
    pio_sm_put_blocking(pio, sm, pixel_grb << 8u);
}

static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    return
            ((uint32_t) (r) << 8) |
            ((uint32_t) (g) << 16) |
            (uint32_t) (b);
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
    return ((uint32_t)r << 8) | ((uint32_t)g << 16) | (uint32_t)b;
}

void pattern(PIO pio, uint sm, uint len, uint t) {
    for (uint i = 0; i < len; ++i) {
        put_pixel(pio, sm, hsl2rgb(t, 0xff, 0x7f));
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

    while(1)
    {
        pattern(pio, sm, NUM_PIXELS, t);
        
        t++;
        if (t % 256 == 0) {
            t = 0;
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
    
    /*Declare and load the image resource*/
    LV_IMG_DECLARE(julian);
    lv_obj_t *img1 = lv_img_create(dat->scr[0]);
    lv_img_set_src(img1, &julian);

    /*Align the image to the center of the screen*/
    lv_obj_align(img1, LV_ALIGN_CENTER, 0, 0);

    /*Load the first screen as the current active screen*/
    lv_scr_load(dat->scr[0]);
}
