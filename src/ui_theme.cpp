/**
 * Player theme registry + application (issue #87).
 *
 * To add a theme: append ONE row to THEMES[] below. If it only changes the
 * backdrop, reuse buildClassicPlayer. If it needs a different layout, add a
 * builder and point at it. Nothing else in the project needs editing — the
 * settings dropdown builds its option list from this array, NVS values are
 * clamped against THEME_COUNT, and the backdrop switch is driven by `bg`.
 */
#include "ui_common.h"
#include "ui_theme.h"
#include "config.h"
#include "lyrics.h"

// ── Registry ────────────────────────────────────────────────────────────────
const ThemeDef THEMES[] = {
    { "Classic",
      "The original look - blurred album art fills the screen",
      THEME_BG_BLUR_ART,      THEME_ART_HERO,  buildClassicPlayer },

    { "Ambient",
      "Same layout, backdrop tinted from the album artwork",
      THEME_BG_AMBIENT_TINT,  THEME_ART_HERO,  buildClassicPlayer },

    { "Immersive",
      "Full-bleed colour, oversized title and large lyrics",
      THEME_BG_AMBIENT_SOLID, THEME_ART_THUMB, buildImmersivePlayer },
};
const uint8_t THEME_COUNT = (uint8_t)(sizeof(THEMES) / sizeof(THEMES[0]));

uint8_t active_theme = 0;

const ThemeDef* themeCurrent(void) {
    return &THEMES[active_theme < THEME_COUNT ? active_theme : 0];
}

bool themeUsesBlurBg(void) {
    return themeCurrent()->bg == THEME_BG_BLUR_ART;
}

// ── Backdrop colour maths ───────────────────────────────────────────────────
// `dominant_color` is already darkened to ~40% of the sampled average by the art
// task, which suits the transparent-panel Classic look. The ambient themes paint
// it as an actual visible surface, so it needs reshaping:
//   - saturate: push channels away from their mean so muted art still reads as a
//     colour rather than grey.
//   - scale:    tint stays dark enough for white text; solid is lifted to the
//     vivid fill in the mockup.
// A floor keeps near-black artwork from producing an unreadable void.
static uint32_t shade(uint32_t c, float scale, float sat, int floor_lum) {
    int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    int mean = (r + g + b) / 3;

    r = mean + (int)((r - mean) * sat);
    g = mean + (int)((g - mean) * sat);
    b = mean + (int)((b - mean) * sat);

    r = (int)(r * scale); g = (int)(g * scale); b = (int)(b * scale);

    // Lift very dark results so text/controls stay legible against the backdrop.
    int lum = (r * 30 + g * 59 + b * 11) / 100;
    if (lum < floor_lum && lum > 0) {
        int lift = (floor_lum * 100) / (lum > 0 ? lum : 1);
        r = (r * lift) / 100; g = (g * lift) / 100; b = (b * lift) / 100;
    } else if (lum == 0) {
        r = g = b = floor_lum;
    }

    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void themeApplyBackdrop(uint32_t rgb) {
    if (!scr_main) return;
    switch (themeCurrent()->bg) {
        case THEME_BG_BLUR_ART:
            // The blurred art image IS the backdrop — leave the screen colour alone.
            break;

        case THEME_BG_AMBIENT_TINT:
            // Deep and muted: sits behind the existing panels, white text on top.
            lv_obj_set_style_bg_color(scr_main, lv_color_hex(shade(rgb, 0.90f, 1.35f, 26)), 0);
            break;

        case THEME_BG_AMBIENT_SOLID:
            // Vivid full-bleed fill (mockup theme-03).
            lv_obj_set_style_bg_color(scr_main, lv_color_hex(shade(rgb, 2.30f, 1.60f, 70)), 0);
            // The play button is this layout's accent — lift it further so it still
            // separates from the saturated backdrop it sits on.
            if (btn_play)
                lv_obj_set_style_bg_color(btn_play, lv_color_hex(shade(rgb, 3.20f, 1.70f, 140)), 0);
            break;
    }
}

// Immersive header thumbnail size, in 800x480 design units.
#define THEME_THUMB_PX   56
#define THEME_THUMB_X    28
#define THEME_THUMB_Y    22

void themeApplyArtGeometry(lv_obj_t* img) {
    if (!img) return;
    switch (themeCurrent()->art) {
        case THEME_ART_THUMB:
            // The decoded source is ART_SIZE square; scale it down to the thumbnail
            // (LVGL zoom is 256 = 1:1) rather than re-decoding at another size.
            lv_obj_set_size(img, SMIN(THEME_THUMB_PX), SMIN(THEME_THUMB_PX));
            lv_image_set_scale(img, (256 * THEME_THUMB_PX) / ART_SIZE);
            lv_obj_set_pos(img, SX(THEME_THUMB_X), SY(THEME_THUMB_Y));
            break;

        case THEME_ART_HERO:
        default:
            // Unchanged Classic behaviour — full size, centred in the art panel.
            lv_image_set_scale(img, 256);
            lv_obj_set_size(img, ART_SIZE, ART_SIZE);
            lv_obj_center(img);
            break;
    }
}

// ── Persistence ─────────────────────────────────────────────────────────────
void themeLoad(void) {
    int idx = wifiPrefs.getInt(NVS_KEY_THEME, DEFAULT_THEME);
    if (idx < 0 || idx >= (int)THEME_COUNT) idx = 0;   // clamp stale/invalid
    active_theme = (uint8_t)idx;
    Serial.printf("[THEME] Active: %s (%d/%d)\n", THEMES[active_theme].name,
                  active_theme, THEME_COUNT);
}

void themeSet(uint8_t idx) {
    if (idx >= THEME_COUNT) idx = 0;
    if (idx == active_theme) return;

    active_theme = idx;
    wifiPrefs.putInt(NVS_KEY_THEME, (int)idx);
    Serial.printf("[THEME] Switched to: %s\n", THEMES[active_theme].name);

    // Rebuild the player with the new theme's builder. Safe here because this
    // runs on the main LVGL thread and the settings screen (not scr_main) is
    // loaded, so we are never deleting the screen currently being displayed.
    // Background tasks only set flags — they never touch LVGL objects.
    lv_obj_t* old = scr_main;
    scr_main = nullptr;
    themeCurrent()->build();      // reassigns scr_main + every player widget global
    if (old) {
        // Never delete the screen currently on display. In practice we're on the
        // settings screen here, but swap first if that ever changes.
        if (lv_screen_active() == old) lv_screen_load(scr_main);
        lv_obj_del(old);          // delete AFTER rebuild so nothing sees a dangling scr_main
    }

    // Re-publish the currently loaded artwork/colour into the fresh widgets.
    // displayCompletedArt() consumes these flags on the next UI tick and rebuilds
    // the image descriptors, so we don't duplicate that logic here.
    if (art_mutex && xSemaphoreTake(art_mutex, pdMS_TO_TICKS(50))) {
        if (art_buffer)     art_ready     = true;
        if (blur_bg_buf)    blur_bg_ready = true;
        color_ready = true;
        xSemaphoreGive(art_mutex);
    }
    themeApplyBackdrop(dominant_color);
    setLyricsVisible(lyrics_enabled && lyrics_ready);
}
