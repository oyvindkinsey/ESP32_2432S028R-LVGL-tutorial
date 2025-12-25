#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "touch_xpt2046.h"

static const char *TAG = "main";

// Pin Definitions
#define LCD_HOST  SPI2_HOST
#define PIN_NUM_MISO 12
#define PIN_NUM_MOSI 13
#define PIN_NUM_CLK  14
#define PIN_NUM_CS   15
#define PIN_NUM_DC   2
#define PIN_NUM_RST  -1
#define PIN_NUM_BCKL 21

#define TOUCH_CS 33
#define TOUCH_IRQ 36

// PWM for Backlight
#define LCD_BCKL_ON_LEVEL  1
#define LCD_BCKL_OFF_LEVEL !LCD_BCKL_ON_LEVEL

// LVGL
#define LVGL_DRAW_BUF_SIZE (240 * 10 * 2) // 10 lines, 2 bytes per pixel

// Global objects
Xpt2046 *touch_driver = nullptr;
lv_obj_t *led1;
lv_obj_t *led3;
lv_obj_t * btn_label;
static lv_obj_t * slider_label;
bool ledsOff = false;
bool rightLedOn = true;

// -------------------------------------------------------------------------
// LVGL Callbacks & UI
// -------------------------------------------------------------------------

static void event_handler_btn2(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t * obj = (lv_obj_t*) lv_event_get_target(e);
  if(code == LV_EVENT_VALUE_CHANGED) {
    if(ledsOff==true){
      if(lv_obj_has_state(obj, LV_STATE_CHECKED)==true) 
      {
        lv_led_off(led1);
        lv_led_on(led3);
        lv_label_set_text(btn_label, "Left");
        rightLedOn = true;
      }
      else
      {
        lv_led_on(led1);
        lv_led_off(led3);
        lv_label_set_text(btn_label, "Right");
        rightLedOn = false;
      }
    }
  }
}

static void event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = (lv_obj_t*)lv_event_get_target(e);
    if(code == LV_EVENT_VALUE_CHANGED) {
        if(lv_obj_has_state(obj, LV_STATE_CHECKED)==true)
        {
          if(rightLedOn==true)
          {
            lv_led_off(led1);
            lv_led_on(led3);
          }
          else
          {
            lv_led_off(led3);
            lv_led_on(led1);
          }
          ledsOff = true;
        }
        else
        {
          lv_led_off(led1);
          lv_led_off(led3);
          ledsOff = false;
        }
    }
}

static void slider_event_callback(lv_event_t * e) {
  lv_obj_t * slider = (lv_obj_t*) lv_event_get_target(e);
  char buf[8];
  lv_snprintf(buf, sizeof(buf), "%d%%", (int)lv_slider_get_value(slider));
  lv_label_set_text(slider_label, buf);
  lv_obj_align_to(slider_label, slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
}

void lv_create_main_gui(void) {
  /*Create a LED and switch it OFF*/
  led1  = lv_led_create(lv_screen_active());
  lv_obj_align(led1, LV_ALIGN_CENTER, -100, 0);
  lv_led_set_brightness(led1, 50);
  lv_led_set_color(led1, lv_palette_main(LV_PALETTE_LIGHT_GREEN));
  lv_led_off(led1);

  /*Copy the previous LED and switch it ON*/
  led3  = lv_led_create(lv_screen_active());
  lv_obj_align(led3, LV_ALIGN_CENTER, 100, 0);
  lv_led_set_brightness(led3, 250);
  lv_led_set_color(led3, lv_palette_main(LV_PALETTE_LIGHT_GREEN));
  lv_led_on(led3);

  // Create a text label aligned center on top ("Hello, Kafkar.com!")
  lv_obj_t * text_label = lv_label_create(lv_screen_active());
  lv_label_set_long_mode(text_label, LV_LABEL_LONG_WRAP);    // Breaks the long lines
  lv_label_set_text(text_label, "Hello, Kafkar.com!");
  lv_obj_set_width(text_label, 150);    // Set smaller width to make the lines wrap
  lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(text_label, LV_ALIGN_CENTER, 0, -90);

  lv_obj_t * sw;

  sw = lv_switch_create(lv_screen_active());
  lv_obj_add_event_cb(sw, event_handler, LV_EVENT_ALL, NULL);
  lv_obj_add_flag(sw, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_align(sw, LV_ALIGN_CENTER, 0, -60);
  lv_obj_add_state(sw, LV_STATE_CHECKED);

  // Create a Toggle button (btn2)
  lv_obj_t *btn2 = lv_button_create(lv_screen_active());
  lv_obj_add_event_cb(btn2, event_handler_btn2, LV_EVENT_ALL, NULL);
  lv_obj_align(btn2, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_flag(btn2, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_set_height(btn2, LV_SIZE_CONTENT);

  btn_label = lv_label_create(btn2);
  lv_label_set_text(btn_label, "Left");
  lv_obj_center(btn_label);
  
  // Create a slider aligned in the center bottom of the TFT display
  lv_obj_t * slider = lv_slider_create(lv_screen_active());
  lv_obj_align(slider, LV_ALIGN_CENTER, 0, 60);
  lv_obj_add_event_cb(slider, slider_event_callback, LV_EVENT_VALUE_CHANGED, NULL);
  lv_slider_set_range(slider, 0, 100);
  lv_obj_set_style_anim_duration(slider, 2000, 0);

  // Create a label below the slider to display the current slider value
  slider_label = lv_label_create(lv_screen_active());
  lv_label_set_text(slider_label, "0%");
  lv_obj_align_to(slider_label, slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
}

// -------------------------------------------------------------------------
// Display Flushing
// -------------------------------------------------------------------------

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    int x1 = area->x1;
    int x2 = area->x2;
    int y1 = area->y1;
    int y2 = area->y2;
    // Because SPI is LSB first but we want MSB first for 8-bit command, but data?
    // esp_lcd_panel_draw_bitmap handles endianness usually if configured correctly.
    // However, LVGL sends bytes. If color depth is 16, it's 2 bytes per pixel.
    esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2 + 1, y2 + 1, px_map);
}

// -------------------------------------------------------------------------
// Touch Read
// -------------------------------------------------------------------------
static void lvgl_touch_read(lv_indev_t * indev, lv_indev_data_t * data) {
    uint16_t x = 0, y = 0, z = 0;
    if (touch_driver && touch_driver->read_raw(&x, &y, &z)) {
        data->state = LV_INDEV_STATE_PRESSED;
        // Calibrate or map if needed.
        // Raw X: ~200-3700, Y: 240-3800 -> Screen 240x320
        
        // Simple linear map
        long lcd_x = (long)(3700 - x) * 240 / (3700 - 200);
        long lcd_y = (long)(y - 240) * 320 / (3800 - 240);

        if (lcd_x < 0) lcd_x = 0;
        if (lcd_y < 0) lcd_y = 0;
        if (lcd_x > 240) lcd_x = 240;
        if (lcd_y > 320) lcd_y = 320;
        
        data->point.x = (int16_t)lcd_x;
        data->point.y = (int16_t)lcd_y;
        
        // Debug: Print Mapped Coordinates
        // printf("Touch: Raw(%d, %d) -> Mapped(%d, %d)\n", x, y, (int)lcd_x, (int)lcd_y);
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}


// -------------------------------------------------------------------------
// Main
// -------------------------------------------------------------------------

// Tick timer callback
static void lv_tick_task(void *arg) {
    lv_tick_inc(1);
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Starting CYD LVGL ESP-IDF");

    // Initialize timer for LVGL
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &lv_tick_task,
        .name = "periodic_gui"
    };
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 1000)); // 1ms

    // Initialize LVGL first
    lv_init();

    lv_display_t *disp = lv_display_create(240, 320); // 240x320 physical
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);

    // -------------------------------------------------------------------------
    // LCD SPI (SPI2)
    // -------------------------------------------------------------------------
    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num = PIN_NUM_CLK;
    buscfg.mosi_io_num = PIN_NUM_MOSI;
    buscfg.miso_io_num = PIN_NUM_MISO;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = 240 * 320 * 2 + 8; 

    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // Initialize Control Pins
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.dc_gpio_num = PIN_NUM_DC;
    io_config.cs_gpio_num = PIN_NUM_CS;
    io_config.pclk_hz = 40 * 1000 * 1000; // 40 MHz
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.spi_mode = 0;
    io_config.trans_queue_depth = 10;
    io_config.on_color_trans_done = notify_lvgl_flush_ready; // Callback for LVGL
    io_config.user_ctx = disp; // Pass the display handle

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = PIN_NUM_RST;
    panel_config.rgb_endian = LCD_RGB_ENDIAN_BGR; // ST7789 often BGR
    panel_config.bits_per_pixel = 16;
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));
    
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    
    // Set rotation/mirror
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));
    
    // Invert Colors (Fixes "off" colors)
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // Backlight
    gpio_set_direction((gpio_num_t)PIN_NUM_BCKL, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)PIN_NUM_BCKL, LCD_BCKL_ON_LEVEL);

    // -------------------------------------------------------------------------
    // Touch SPI (SPI3)
    // -------------------------------------------------------------------------
    // CYD Touch Pins: CLK=25, MOSI=32, MISO=39, CS=33
    spi_bus_config_t touch_buscfg = {};
    touch_buscfg.sclk_io_num = 25;
    touch_buscfg.mosi_io_num = 32;
    touch_buscfg.miso_io_num = 39;
    touch_buscfg.quadwp_io_num = -1;
    touch_buscfg.quadhd_io_num = -1;
    touch_buscfg.max_transfer_sz = 100; // Small transfers

    // Use SPI3_HOST for Touch
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &touch_buscfg, SPI_DMA_CH_AUTO));

    // Initialize Touch
    touch_driver = new Xpt2046(SPI3_HOST, TOUCH_CS, TOUCH_IRQ);
    if (!touch_driver->init()) {
        ESP_LOGE(TAG, "Touch init failed");
    }

    // Allocate draw buffers
    uint8_t *buf1 = (uint8_t*)heap_caps_malloc(LVGL_DRAW_BUF_SIZE, MALLOC_CAP_DMA);
    uint8_t *buf2 = (uint8_t*)heap_caps_malloc(LVGL_DRAW_BUF_SIZE, MALLOC_CAP_DMA);
    lv_display_set_buffers(disp, buf1, buf2, LVGL_DRAW_BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
    
    lv_display_set_flush_cb(disp, lvgl_flush_cb);
    lv_display_set_user_data(disp, panel_handle);

    // Touch Input
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lvgl_touch_read);
    lv_indev_set_display(indev, disp); // Explicitly attach to display

    // Create GUI
    lv_create_main_gui();

    int loop_count = 0;
    while (1) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
