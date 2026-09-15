/*
 * SPDX-FileCopyrightText: 2022-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/lock.h>
#include <sys/param.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "esp_lcd_panel_io.h"
#include "esp_check.h"
/* #include "project/xc200-tear/build/config/sdkconfig.h" */

#include "xf_private_device.h"
#include "xf_lvgl_port.h"
#include "xf_packet_handle.h"

static const char *TAG = "rgb lcd";

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your LCD spec //////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Refresh Rate = 18000000/(1+40+20+800)/(1+10+5+480) = 42Hz
#define LCD_PIXEL_CLOCK_HZ              (10 * 1000 * 1000)
#define LCD_H_RES                       222
#define LCD_V_RES                       480
#define LCD_H_OFFSET                    0

#define LCD_HSYNC                       26
#define LCD_HBP                         29
#define LCD_HFP                         38
#define LCD_VSYNC                       3
#define LCD_VBP                         3
#define LCD_VFP                         6

#define PIN_NUM_DISP_EN                 -1
#define LCD_NUM_FB                      2

#define DATA_BUS_WIDTH                  16
#define PIXEL_SIZE                      2

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your Application ///////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define LVGL_TICK_PERIOD_MS             2
#define LVGL_TASK_STACK_SIZE            (6 * 1024)
#define LVGL_TASK_PRIORITY              2
#define LVGL_TASK_MAX_DELAY_MS          500
#define LVGL_TASK_MIN_DELAY_MS          5

static TaskHandle_t lvgl_task_handle = NULL;

extern void example_lvgl_demo_ui(lv_display_t *disp);

IRAM_ATTR static bool rgb_lcd_on_vsync_event(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *edata, void *user_ctx)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (lvgl_task_handle == NULL) {
        return false;
    }

    xTaskNotifyFromISR(lvgl_task_handle, 0x01, eSetBits, &xHigherPriorityTaskWoken);

    return xHigherPriorityTaskWoken == pdTRUE;
}

IRAM_ATTR static bool rgb_lcd_on_color_trans_done_event(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *edata, void *user_ctx)
{
    lv_display_flush_ready((lv_display_t *)user_ctx);
    return true;
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    // pass the draw buffer to the driver

    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);
}

static void increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t notify_value;
    uint32_t tick1 = 0, tick2 = 0;

    while (1) {
        xTaskNotifyWait(0, ULONG_MAX, &notify_value, portMAX_DELAY);
        xf_lvgl_port_lock(0);
        lv_timer_handler();
        xf_lvgl_port_unlock();
    }
}

// 软件SPI函数（因为硬件SPI可能不支持9位模式）
typedef struct {
    gpio_num_t sclk_pin;
    gpio_num_t mosi_pin;
    gpio_num_t cs_pin;
    uint32_t delay_us;  // 位之间的延迟
} soft_spi_t;

// 初始化软件SPI
static void soft_spi_init(soft_spi_t *spi)
{
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << spi->sclk_pin) |
        (1ULL << spi->mosi_pin) |
        (1ULL << spi->cs_pin),
    };
    gpio_config(&io_conf);

    // 初始状态
    gpio_set_level(spi->cs_pin, 1);   // CS高电平（空闲）
    gpio_set_level(spi->sclk_pin, 0); // SCLK低电平
    gpio_set_level(spi->mosi_pin, 0); // MOSI低电平
}

// 软件SPI发送9位数据
static void soft_spi_send_9bit(soft_spi_t *spi, uint16_t data_9bit)
{
    // 拉低CS，开始传输
    gpio_set_level(spi->cs_pin, 0);
    esp_rom_delay_us(spi->delay_us);

    // 发送9位数据，最高位（第9位）先发送
    for (int i = 8; i >= 0; i--) {
        // 设置数据位
        gpio_set_level(spi->mosi_pin, (data_9bit >> i) & 0x01);
        esp_rom_delay_us(spi->delay_us);

        // 产生时钟上升沿
        gpio_set_level(spi->sclk_pin, 1);
        esp_rom_delay_us(spi->delay_us);

        // 产生时钟下降沿
        gpio_set_level(spi->sclk_pin, 0);
        esp_rom_delay_us(spi->delay_us);
    }

    // 拉高CS，结束传输
    gpio_set_level(spi->cs_pin, 1);
    esp_rom_delay_us(spi->delay_us);
}

// 发送命令（9位SPI模式）
static void st7796u_send_cmd_9bit(soft_spi_t *spi, uint8_t cmd)
{
    // 9位数据格式：0 + 8位命令
    // 最高位（第9位）= 0 表示命令
    uint16_t data_9bit = (0 << 8) | cmd;
    soft_spi_send_9bit(spi, data_9bit);
}

// 发送数据（9位SPI模式）
static void st7796u_send_data_9bit(soft_spi_t *spi, uint8_t data)
{
    // 9位数据格式：1 + 8位数据
    // 最高位（第9位）= 1 表示数据
    uint16_t data_9bit = (1 << 8) | data;
    soft_spi_send_9bit(spi, data_9bit);
}

// 发送带参数的命令（9位SPI模式）
static void st7796u_send_cmd_with_params_9bit(soft_spi_t *spi, uint8_t cmd, uint8_t *params, uint32_t param_len)
{
    st7796u_send_cmd_9bit(spi, cmd);

    for (uint32_t i = 0; i < param_len; i++) {
        st7796u_send_data_9bit(spi, params[i]);
    }
}

// 初始化软件SPI
soft_spi_t spi = {
    .sclk_pin = 0,
    .mosi_pin = 0,
    .cs_pin = 0,
    .delay_us = 2,  // 2微秒延迟，约500kHz
};
// 针对3线SPI（9位模式）的ST7796U初始化
static esp_err_t st7796u_3wire_spi_init(gpio_num_t sclk, gpio_num_t mosi, gpio_num_t cs, gpio_num_t rst_pin)
{
    ESP_LOGI(TAG, "初始化3线SPI（9位模式）");

    spi.sclk_pin = sclk;
    spi.mosi_pin = mosi;
    spi.cs_pin = cs;
    soft_spi_init(&spi);

    // 硬件复位
    if (rst_pin >= 0) {
        gpio_config_t rst_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = (1ULL << rst_pin),
        };
        gpio_config(&rst_conf);

        gpio_set_level(rst_pin, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(rst_pin, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(rst_pin, 1);
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    ESP_LOGI(TAG, "发送ST7796U初始化命令（9位SPI模式）");

    st7796u_send_cmd_9bit(&spi, 0x01);
    vTaskDelay(pdMS_TO_TICKS(150));   // reset
    st7796u_send_cmd_9bit(&spi, 0x11);
    vTaskDelay(pdMS_TO_TICKS(120));   // sleep out

    // Enable CMD2
    st7796u_send_cmd_with_params_9bit(&spi, 0xF0, (uint8_t[]) {
        0xC3
    }, 1);
    st7796u_send_cmd_with_params_9bit(&spi, 0xF0, (uint8_t[]) {
        0x96
    }, 1);

    // MADCTL
    st7796u_send_cmd_with_params_9bit(&spi, 0x36, (uint8_t[]) {
        0x18
    }, 1);

    // COLMOD RGB565
    st7796u_send_cmd_with_params_9bit(&spi, 0x3A, (uint8_t[]) {
        0x55
    }, 1);

    // Inversion Control
    st7796u_send_cmd_with_params_9bit(&spi, 0xB4, (uint8_t[]) {
        0x01
    }, 1);

    // FPS
    /* st7796u_send_cmd_with_params_9bit(&spi, 0xB1, (uint8_t[]){0x40}, 1); */


    /* sync mode */
    st7796u_send_cmd_with_params_9bit(&spi, 0xB6, (uint8_t[]) {
        0xE0, 0x02, 0x3B
    }, 3);
    /* DE mode */
    /* st7796u_send_cmd_with_params_9bit(&spi, 0xB6, (uint8_t[]){0xA0, 0x00, 0x3B}, 3); */
    /* st7796u_send_cmd_with_params_9bit(&spi, 0xB6, (uint8_t[]){0x00, 0x82, 0x27}, 3); */

    // Entry Mode
    st7796u_send_cmd_with_params_9bit(&spi, 0xB7, (uint8_t[]) {
        0xC6
    }, 1);

    // Power Control
    st7796u_send_cmd_with_params_9bit(&spi, 0xB9,
    (uint8_t[]) {
        0x02, 0xE0
    }, 2);

    st7796u_send_cmd_with_params_9bit(&spi, 0xC0,
    (uint8_t[]) {
        0xC0, 0x64
    }, 2);

    st7796u_send_cmd_with_params_9bit(&spi, 0xC1,
    (uint8_t[]) {
        0x1D
    }, 1);

    st7796u_send_cmd_with_params_9bit(&spi, 0xC2,
    (uint8_t[]) {
        0xA7
    }, 1);

    st7796u_send_cmd_with_params_9bit(&spi, 0xC5,
    (uint8_t[]) {
        0x18
    }, 1);

    // Power Setting
    st7796u_send_cmd_with_params_9bit(&spi, 0xE8,
    (uint8_t[]) {
        0x40, 0x8A, 0x00, 0x00,
              0x29, 0x19, 0xA5, 0x33
    }, 8);

    // Gamma +
    st7796u_send_cmd_with_params_9bit(&spi, 0xE0,
    (uint8_t[]) {
        0xF0, 0x0B, 0x12, 0x09,
              0x0A, 0x26, 0x39, 0x54,
              0x4E, 0x38, 0x13, 0x13,
              0x2E, 0x34
    }, 14);

    // Gamma -
    st7796u_send_cmd_with_params_9bit(&spi, 0xE1,
    (uint8_t[]) {
        0xF0, 0x10, 0x15, 0x0D,
              0x0C, 0x07, 0x38, 0x43,
              0x4D, 0x3A, 0x16, 0x15,
              0x30, 0x35
    }, 14);

    // Disable CMD2
    st7796u_send_cmd_with_params_9bit(&spi, 0xF0, (uint8_t[]) {
        0x3C
    }, 1);
    st7796u_send_cmd_with_params_9bit(&spi, 0xF0, (uint8_t[]) {
        0x69
    }, 1);

    // Display ON
    st7796u_send_cmd_9bit(&spi, 0x29);

    // Inversion ON
    st7796u_send_cmd_9bit(&spi, 0x21);

    ESP_LOGI(TAG, "3线SPI初始化完成");
    return ESP_OK;
}

void xf_pri_device_start(void)
{
    st7796u_3wire_spi_init(CONFIG_SCK_GPIO, CONFIG_MOSI_GPIO, CONFIG_CS_GPIO, CONFIG_RST_GPIO);
    printf("Free heap: %u bytes\n", heap_caps_get_free_size(MALLOC_CAP_8BIT));
    printf("Internal SRAM: %u bytes\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    printf("PSRAM: %u bytes\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    //xf_packet_handle_init();
#if 1
    ESP_LOGI(TAG, "Install RGB LCD panel driver");
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_rgb_panel_config_t panel_config = {
        .data_width = DATA_BUS_WIDTH,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,
        .dma_burst_size = 64,
        .num_fbs = LCD_NUM_FB,
        .bounce_buffer_size_px = 20 * LCD_H_RES,
        .clk_src = LCD_CLK_SRC_PLL160M,
        .disp_gpio_num = PIN_NUM_DISP_EN,
        .pclk_gpio_num = CONFIG_LCD_PCLK_GPIO,
        .vsync_gpio_num = CONFIG_LCD_VSYNC_GPIO,
        .hsync_gpio_num = CONFIG_LCD_HSYNC_GPIO,
        .de_gpio_num = CONFIG_LCD_DE_GPIO,
        .data_gpio_nums = {
            CONFIG_LCD_DATA0_GPIO,
            CONFIG_LCD_DATA1_GPIO,
            CONFIG_LCD_DATA2_GPIO,
            CONFIG_LCD_DATA3_GPIO,
            CONFIG_LCD_DATA4_GPIO,
            CONFIG_LCD_DATA5_GPIO,
            CONFIG_LCD_DATA6_GPIO,
            CONFIG_LCD_DATA7_GPIO,
            CONFIG_LCD_DATA8_GPIO,
            CONFIG_LCD_DATA9_GPIO,
            CONFIG_LCD_DATA10_GPIO,
            CONFIG_LCD_DATA11_GPIO,
            CONFIG_LCD_DATA12_GPIO,
            CONFIG_LCD_DATA13_GPIO,
            CONFIG_LCD_DATA14_GPIO,
            CONFIG_LCD_DATA15_GPIO,
        },
        .timings = {
            .pclk_hz = LCD_PIXEL_CLOCK_HZ,
            .h_res = LCD_H_RES + LCD_H_OFFSET,
            .v_res = LCD_V_RES,
            .hsync_back_porch = LCD_HBP,
            .hsync_front_porch = LCD_HFP,
            .hsync_pulse_width = LCD_HSYNC,
            .vsync_back_porch = LCD_VBP,
            .vsync_front_porch = LCD_VFP,
            .vsync_pulse_width = LCD_VSYNC,
            .flags = {
                .pclk_active_neg = 0,
            },
        },
        .flags.fb_in_psram = true, // allocate frame buffer in PSRAM
    };
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    //esp_lcd_panel_set_gap(panel_handle, 48, 20);

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();
    // create a lvgl display
    lv_display_t *display = lv_display_create(LCD_H_RES, LCD_V_RES);
    // associate the rgb panel handle to the display
    lv_display_set_user_data(display, panel_handle);
    // set color depth
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);

    // create draw buffers
    void *buf1 = NULL;
    void *buf2 = NULL;
    ESP_LOGI(TAG, "Use frame buffers as LVGL draw buffers");
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, &buf1, &buf2));

    // set LVGL draw buffers and direct mode
    lv_display_set_buffers(display, buf1, buf2, LCD_H_RES * LCD_V_RES * PIXEL_SIZE, LV_DISPLAY_RENDER_MODE_DIRECT);

    // set the callback which can copy the rendered image to an area of the display
    lv_display_set_flush_cb(display, lvgl_flush_cb);

    ESP_LOGI(TAG, "Install LVGL tick timer");
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    xf_lvgl_port_init();
    ESP_LOGI(TAG, "Create LVGL task");
    BaseType_t task_result = xTaskCreatePinnedToCore(lvgl_port_task,
                             "LVGL",
                             LVGL_TASK_STACK_SIZE,
                             NULL,
                             LVGL_TASK_PRIORITY,
                             &lvgl_task_handle,
                             1);
    ESP_ERROR_CHECK(task_result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_LOGI(TAG, "Display LVGL UI");
    // Lock the mutex due to the LVGL APIs are not thread-safe
    xf_lvgl_port_lock(0);
    example_lvgl_demo_ui(display);
    xf_lvgl_port_unlock();

    /* 界面和任务准备就绪后，再注册回调并启动 RGB 扫描。 */
    ESP_LOGI(TAG, "Register event callbacks");
    esp_lcd_rgb_panel_event_callbacks_t cbs = {
        .on_vsync = rgb_lcd_on_vsync_event,
        .on_color_trans_done = rgb_lcd_on_color_trans_done_event,
    };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &cbs, display));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
#endif
}
