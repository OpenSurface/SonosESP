/**
 * main_square.cpp — entry point of the square 480x480 build (UI_SQUARE).
 *
 * Bring-up stage: panel, touch and LVGL only, with a test card that shows the
 * four destination colours and where the last touch landed. The Sonos core and
 * the real screens are wired in on top of this.
 */
#include <Arduino.h>
#include "lvgl.h"
#include "config.h"
#include "display_driver.h"
#include "touch_driver.h"
#include "ui_fonts.h"



static uint32_t lvglTick(void) { return millis(); }

static lv_obj_t* s_touch_lbl = nullptr;
static lv_obj_t* s_dot       = nullptr;

static void touchProbe(lv_event_t* e) {
    lv_indev_t* in = lv_indev_active();
    if (!in) return;
    lv_point_t p;
    lv_indev_get_point(in, &p);
    lv_label_set_text_fmt(s_touch_lbl, "x=%d  y=%d", (int)p.x, (int)p.y);
    if (lv_event_get_code(e) == LV_EVENT_PRESSED) Serial.printf("[TOUCH] %d,%d\n", (int)p.x, (int)p.y);
    lv_obj_set_pos(s_dot, p.x - 12, p.y - 12);
}

static void buildTestCard(void) {
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x16133A), 0);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, touchProbe, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(scr, touchProbe, LV_EVENT_PRESSED, nullptr);

    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "SonosESP carré");
    lv_obj_set_style_text_font(title, &font_text_32, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFF7EA), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    // Corner markers: tells at a glance whether the panel is mirrored/rotated.
    static const char* corners[] = {"HG", "HD", "BG", "BD"};
    static const lv_align_t al[] = {LV_ALIGN_TOP_LEFT, LV_ALIGN_TOP_RIGHT,
                                    LV_ALIGN_BOTTOM_LEFT, LV_ALIGN_BOTTOM_RIGHT};
    for (int i = 0; i < 4; ++i) {
        lv_obj_t* c = lv_label_create(scr);
        lv_label_set_text(c, corners[i]);
        lv_obj_set_style_text_font(c, &font_text_24, 0);
        lv_obj_set_style_text_color(c, lv_color_hex(0xB7B1E3), 0);
        lv_obj_align(c, al[i], (i % 2) ? -8 : 8, (i < 2) ? 8 : -8);
    }

    // The four destination colours, in RGB order checks: pink, orange, yellow, mint.
    static const uint32_t cols[] = {0xFF7AB6, 0xFF9A3C, 0xFFD23F, 0x3BE0A0, 0x6CB8FF};
    for (int i = 0; i < 5; ++i) {
        lv_obj_t* b = lv_obj_create(scr);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 72, 72);
        lv_obj_set_style_radius(b, 36, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(cols[i]), 0);
        lv_obj_align(b, LV_ALIGN_CENTER, (i - 2) * 86, -20);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
    }

    s_touch_lbl = lv_label_create(scr);
    lv_label_set_text(s_touch_lbl, "Touchez l'écran");
    lv_obj_set_style_text_font(s_touch_lbl, &font_text_24, 0);
    lv_obj_set_style_text_color(s_touch_lbl, lv_color_hex(0xFFD23F), 0);
    lv_obj_align(s_touch_lbl, LV_ALIGN_CENTER, 0, 80);

    s_dot = lv_obj_create(scr);
    lv_obj_remove_style_all(s_dot);
    lv_obj_set_size(s_dot, 24, 24);
    lv_obj_set_style_radius(s_dot, 12, 0);
    lv_obj_set_style_bg_opa(s_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(0xFFF7EA), 0);
    lv_obj_remove_flag(s_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(s_dot, -40, -40);
}

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    delay(200);
    Serial.println("\n=== SONOS ESP — CARRÉ (ESP32-S3) ===");
    Serial.printf("Heap %u, PSRAM %u\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());

    uiFontsInit();
    lv_init();
    lv_tick_set_cb(lvglTick);
    if (!display_init()) { Serial.println("Display FAIL"); while (1) delay(1000); }
    if (!touch_init())   { Serial.println("Touch FAIL"); }

    buildTestCard();
    lv_timer_handler();
    display_set_brightness(80);
    Serial.println("Ready");
}

// Bench diagnostics over the UART: "bl <0..100>" (PWM), "blhi" / "bllo" (plain GPIO).
static void benchSerial(void) {
    static char line[48];
    static size_t n = 0;
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c != '\n') { if (n < sizeof(line) - 1) line[n++] = c; continue; }
        line[n] = 0; n = 0;
        if (!strncmp(line, "blf ", 4)) {
            extern void display_backlight_freq(uint32_t hz);
            display_backlight_freq((uint32_t)atoi(line + 4));
        } else if (!strncmp(line, "bl ", 3)) {
            display_set_brightness((uint8_t)atoi(line + 3));
            Serial.printf("[BENCH] backlight PWM %d%%\n", atoi(line + 3));
        } else if (!strcmp(line, "blhi") || !strcmp(line, "bllo")) {
            ledcDetach(LCD_BACKLIGHT);
            pinMode(LCD_BACKLIGHT, OUTPUT);
            digitalWrite(LCD_BACKLIGHT, line[2] == 'h' ? HIGH : LOW);
            Serial.printf("[BENCH] backlight GPIO%d %s\n", LCD_BACKLIGHT, line + 2);
        }
    }
}

void loop() {
    benchSerial();
    uint32_t wait = lv_timer_handler();
    if (wait > 10) wait = 10;
    delay(wait ? wait : 1);
}
