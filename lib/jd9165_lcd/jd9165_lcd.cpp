#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_ldo_regulator.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "Arduino.h"
#include "esp_lcd_jd9165.h"
#include "jd9165_lcd.h"
#include "driver/ledc.h"

#define BACKLIGHT_PIN 23
#define BACKLIGHT_CHANNEL LEDC_CHANNEL_0
#define BACKLIGHT_TIMER LEDC_TIMER_0
#define BACKLIGHT_MODE LEDC_LOW_SPEED_MODE
#define BACKLIGHT_DUTY_RES LEDC_TIMER_13_BIT
#define BACKLIGHT_FREQ 1000


#define LCD_H_RES 1024
#define LCD_V_RES 600

#define MIPI_DPI_PX_FORMAT (LCD_COLOR_PIXEL_FORMAT_RGB565)
#define LCD_BIT_PER_PIXEL (16)

// “VDD_MIPI_DPHY”应供电 2.5V，可从内部 LDO 稳压器或外部 LDO 芯片获取电源
#define EXAMPLE_MIPI_DSI_PHY_PWR_LDO_CHAN 3 // LDO_VO3 连接至 VDD_MIPI_DPHY
#define EXAMPLE_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV 2500

static const char *TAG = "example";
esp_lcd_panel_handle_t panel_handle = NULL;
esp_lcd_panel_io_handle_t io_handle = NULL;

jd9165_lcd::jd9165_lcd(int8_t lcd_rst)
{
    _lcd_rst = lcd_rst;
}

void jd9165_lcd::example_bsp_enable_dsi_phy_power()
{
    // 打开 MIPI DSI PHY 的电源，使其从“无电”状态进入“关机”状态
    esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
#ifdef EXAMPLE_MIPI_DSI_PHY_PWR_LDO_CHAN
    esp_ldo_channel_config_t ldo_mipi_phy_config = {
        .chan_id = EXAMPLE_MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = EXAMPLE_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_mipi_phy_config, &ldo_mipi_phy));
    ESP_LOGI(TAG, "MIPI DSI PHY Powered on");
#endif
}



bool pwm_initialized = false;

void jd9165_lcd::initBacklightPWM() {
    if (pwm_initialized) return;
    
    ledc_timer_config_t ledc_timer = {
        .speed_mode = BACKLIGHT_MODE,
        .duty_resolution = BACKLIGHT_DUTY_RES,
        .timer_num = BACKLIGHT_TIMER,
        .freq_hz = BACKLIGHT_FREQ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);
    
    ledc_channel_config_t ledc_channel = {
        .gpio_num = BACKLIGHT_PIN,
        .speed_mode = BACKLIGHT_MODE,
        .channel = BACKLIGHT_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BACKLIGHT_TIMER,
        .duty = 8191,  // 100%
        .hpoint = 0
    };
    ledc_channel_config(&ledc_channel);
    
    pwm_initialized = true;
}

void jd9165_lcd::example_bsp_set_lcd_backlight(uint32_t level)
{
    if (!pwm_initialized) {
        initBacklightPWM();
    }
    
    // 'level' is 0-100 percentage
    level = constrain(level, 0, 100);
    
    // Convert percentage to PWM duty (0-8191)
    // Keep minimum 5% for visibility (409 duty)
    uint32_t duty = map(level, 0, 100, 0, 8191);
    if (level > 0 && duty < 409) duty = 409;  // Minimum 5%
    
    ledc_set_duty(BACKLIGHT_MODE, BACKLIGHT_CHANNEL, duty);
    ledc_update_duty(BACKLIGHT_MODE, BACKLIGHT_CHANNEL);
    
    Serial.printf("[Backlight] Set to %d%% (duty: %d)\n", level, duty);
}


// ---------------------------------------------------------------------------
// Old_Panel override.
//
// The driver's built-in table is GUITION's New_Panel sequence. Boards from the
// earlier batch need their own sequence and timings, so when the user selects
// PANEL_VARIANT_OLD we pass these through vendor_config.init_cmds instead.
// Timings are GUITION's Old_Panel demo: 52MHz, hsync 20/160/160, vsync 10/23/12.
// ---------------------------------------------------------------------------
static const jd9165_lcd_init_cmd_t jd9165_init_old[] = {
//  {cmd, { data }, data_size, delay_ms}
    //{0x11, (uint8_t []){0x00}, 1, 120},
    //{0x29, (uint8_t []){0x00}, 1, 20},
    
    {0x30, (uint8_t[]){0x00}, 1, 0},
    {0xF7, (uint8_t[]){0x49,0x61,0x02,0x00}, 4, 0},
    {0x30, (uint8_t[]){0x01}, 1, 0},
    {0x04, (uint8_t[]){0x0C}, 1, 0},
    {0x05, (uint8_t[]){0x00}, 1, 0},
    {0x06, (uint8_t[]){0x00}, 1, 0},
    {0x0B, (uint8_t[]){0x11}, 1, 0},
    {0x17, (uint8_t[]){0x00}, 1, 0},
    {0x20, (uint8_t[]){0x04}, 1, 0},
    {0x1F, (uint8_t[]){0x05}, 1, 0},
    {0x23, (uint8_t[]){0x00}, 1, 0},
    {0x25, (uint8_t[]){0x19}, 1, 0},
    {0x28, (uint8_t[]){0x18}, 1, 0},
    {0x29, (uint8_t[]){0x04}, 1, 0},
    {0x2A, (uint8_t[]){0x01}, 1, 0},
    {0x2B, (uint8_t[]){0x04}, 1, 0},
    {0x2C, (uint8_t[]){0x01}, 1, 0},
    {0x30, (uint8_t[]){0x02}, 1, 0},
    {0x01, (uint8_t[]){0x22}, 1, 0},
    {0x03, (uint8_t[]){0x12}, 1, 0},
    {0x04, (uint8_t[]){0x00}, 1, 0},
    {0x05, (uint8_t[]){0x64}, 1, 0},
    {0x0A, (uint8_t[]){0x08}, 1, 0},
    {0x0B, (uint8_t[]){0x0A,0x1A,0x0B,0x0D,0x0D,0x11,0x10,0x06,0x08,0x1F,0x1D}, 11, 0},
    {0x0C, (uint8_t[]){0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D}, 11, 0},
    {0x0D, (uint8_t[]){0x16,0x1B,0x0B,0x0D,0x0D,0x11,0x10,0x07,0x09,0x1E,0x1C}, 11, 0},
    {0x0E, (uint8_t[]){0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D}, 11, 0},
    {0x0F, (uint8_t[]){0x16,0x1B,0x0D,0x0B,0x0D,0x11,0x10,0x1C,0x1E,0x09,0x07}, 11, 0},
    {0x10, (uint8_t[]){0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D}, 11, 0},
    {0x11, (uint8_t[]){0x0A,0x1A,0x0D,0x0B,0x0D,0x11,0x10,0x1D,0x1F,0x08,0x06}, 11, 0},
    {0x12, (uint8_t[]){0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D}, 11, 0},
    {0x14, (uint8_t[]){0x00,0x00,0x11,0x11}, 4, 0},
    {0x18, (uint8_t[]){0x99}, 1, 0},
    {0x30, (uint8_t[]){0x06}, 1, 0},
    {0x12, (uint8_t[]){0x36,0x2C,0x2E,0x3C,0x38,0x35,0x35,0x32,0x2E,0x1D,0x2B,0x21,0x16,0x29}, 14, 0},
    {0x13, (uint8_t[]){0x36,0x2C,0x2E,0x3C,0x38,0x35,0x35,0x32,0x2E,0x1D,0x2B,0x21,0x16,0x29}, 14, 0},
    
    // {0x30, (uint8_t[]){0x08}, 1, 0},
    // {0x05, (uint8_t[]){0x01}, 1, 0},
    // {0x0C, (uint8_t[]){0x1A}, 1, 0},
    // {0x0D, (uint8_t[]){0x0E}, 1, 0},

    // {0x30, (uint8_t[]){0x07}, 1, 0},
    // {0x01, (uint8_t[]){0x04}, 1, 0},

    {0x30, (uint8_t[]){0x0A}, 1, 0},
    {0x02, (uint8_t[]){0x4F}, 1, 0},
    {0x0B, (uint8_t[]){0x40}, 1, 0},
    {0x12, (uint8_t[]){0x3E}, 1, 0},
    {0x13, (uint8_t[]){0x78}, 1, 0},
    {0x30, (uint8_t[]){0x0D}, 1, 0},
    {0x0D, (uint8_t[]){0x04}, 1, 0},
    {0x10, (uint8_t[]){0x0C}, 1, 0},
    {0x11, (uint8_t[]){0x0C}, 1, 0},
    {0x12, (uint8_t[]){0x0C}, 1, 0},
    {0x13, (uint8_t[]){0x0C}, 1, 0},
    {0x30, (uint8_t[]){0x00}, 1, 0},

        {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 1, 120},
    {0x29, (uint8_t[]){0x00}, 1, 50},
};

#define JD9165_1024_600_OLD_PANEL_DPI_CONFIG(px_format)  \
    {                                                    \
        .virtual_channel = 0,                            \
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,     \
        .dpi_clock_freq_mhz = 52,                        \
        .pixel_format = px_format,                       \
        .num_fbs = 1,                                    \
        .video_timing = {                                \
            .h_size = 1024,                              \
            .v_size = 600,                               \
            .hsync_pulse_width = 20,                     \
            .hsync_back_porch = 160,                     \
            .hsync_front_porch = 160,                    \
            .vsync_pulse_width = 10,                     \
            .vsync_back_porch = 23,                      \
            .vsync_front_porch = 12,                     \
        },                                               \
        .flags = {                                       \
            .use_dma2d = true,                           \
        },                                               \
    }

void jd9165_lcd::begin(bool old_panel)
{   
    example_bsp_enable_dsi_phy_power();

    // 首先创建 MIPI DSI 总线，它还将初始化 DSI PHY
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus;
    esp_lcd_dsi_bus_config_t bus_config = JD9165_PANEL_BUS_DSI_2CH_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));

    ESP_LOGI(TAG, "Install MIPI DSI LCD control panel");
    // 我们使用DBI接口发送LCD命令和参数
    esp_lcd_dbi_io_config_t dbi_config = JD9165_PANEL_IO_DBI_CONFIG();

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io_handle));

    // 创建JD9165控制面板
    Serial.printf("[Display] JD9165 panel variant: %s\n", old_panel ? "Old" : "New");

    esp_lcd_dpi_panel_config_t dpi_config = old_panel
        ? (esp_lcd_dpi_panel_config_t)JD9165_1024_600_OLD_PANEL_DPI_CONFIG(MIPI_DPI_PX_FORMAT)
        : (esp_lcd_dpi_panel_config_t)JD9165_1024_600_PANEL_60HZ_DPI_CONFIG(MIPI_DPI_PX_FORMAT);

    jd9165_vendor_config_t vendor_config = {
        // NULL init_cmds -> the driver falls back to its built-in New_Panel table.
        .init_cmds      = old_panel ? jd9165_init_old : NULL,
        .init_cmds_size = old_panel ? (uint16_t)(sizeof(jd9165_init_old) / sizeof(jd9165_init_old[0])) : 0,
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = _lcd_rst,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_jd9165(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

}

void jd9165_lcd::lcd_draw_bitmap(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end, uint16_t *color_data)
{
    // ✅ Use the standard 6-argument version (NO STRIDE)
    esp_lcd_panel_draw_bitmap(panel_handle, x_start, y_start, x_end, y_end, color_data);
}

void jd9165_lcd::draw16bitbergbbitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t *color_data)
{
    uint16_t x_start = x;
    uint16_t y_start = y;
    uint16_t x_end = w + x;
    uint16_t y_end = h + y;

    esp_lcd_panel_draw_bitmap(panel_handle, x_start, y_start, x_end, y_end, color_data);
}

void jd9165_lcd::fillScreen(uint16_t color)
{
    uint16_t *color_data = (uint16_t *)heap_caps_malloc(480 * 272 * 2, MALLOC_CAP_INTERNAL);
    memset(color_data, color, 480 * 272 * 2);
    draw16bitbergbbitmap(0, 0, 480, 272, color_data);
    free(color_data);
}

void jd9165_lcd::te_on()
{
    esp_lcd_panel_io_tx_param(io_handle, 0x35,new (uint8_t[]){0x00}, 1);
}

void jd9165_lcd::te_off()
{
    esp_lcd_panel_io_tx_param(io_handle, 0x34,new (uint8_t[]){0x00}, 0);
}

uint16_t jd9165_lcd::width()
{
    return LCD_H_RES;
}

uint16_t jd9165_lcd::height()
{
    return LCD_V_RES;
}

bool jd9165_lcd::get_handle(bsp_lcd_handles_t *ret_handles) {
    if (!panel_handle || !io_handle) {
        return false; // Return false if handles are not initialized
    }
    
    ret_handles->io = io_handle;
    ret_handles->panel = panel_handle;
    ret_handles->control = NULL;
    ret_handles->mipi_dsi_bus = NULL;
    
    return true; //
}