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
#include "jd9165_panels.h"
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


void jd9165_lcd::begin(uint8_t variant)
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
    const JD9165PanelDef* p = &JD9165_PANELS[jd9165PanelClamp(variant)];
    Serial.printf("[Display] JD9165 panel variant %u/%u: %s\n",
                  (unsigned)variant, (unsigned)JD9165_PANEL_COUNT, p->name);

    // Built from the registry row rather than a per-variant macro, so a new
    // panel revision is one table entry and needs no code change here.
    esp_lcd_dpi_panel_config_t dpi_config = JD9165_1024_600_PANEL_60HZ_DPI_CONFIG(MIPI_DPI_PX_FORMAT);
    dpi_config.dpi_clock_freq_mhz            = p->pclk_mhz;
    dpi_config.video_timing.hsync_pulse_width = p->hsync_pulse;
    dpi_config.video_timing.hsync_back_porch  = p->hsync_back;
    dpi_config.video_timing.hsync_front_porch = p->hsync_front;
    dpi_config.video_timing.vsync_pulse_width = p->vsync_pulse;
    dpi_config.video_timing.vsync_back_porch  = p->vsync_back;
    dpi_config.video_timing.vsync_front_porch = p->vsync_front;

    jd9165_vendor_config_t vendor_config = {
        // NULL init_cmds -> the driver falls back to its built-in New_Panel table.
        .init_cmds      = p->init_cmds,       // NULL -> driver built-in (New_Panel)
        .init_cmds_size = p->init_cmds_size,
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