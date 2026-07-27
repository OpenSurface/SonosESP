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
//                                                                   art:  size   x                     y
const ThemeDef THEMES[] = {
    { "SonosESP",
      "The original - blurred album art fills the screen",
      THEME_BG_BLUR_ART,      ART_SIZE, THEME_ART_CENTRED, THEME_ART_CENTRED, buildClassicPlayer },

    { "Ambient",
      "Tinted backdrop, lyrics below the artwork",
      THEME_BG_AMBIENT_TINT,  308,      39,                36,                buildAmbientPlayer },

    { "Immersive",
      "Full-bleed colour, oversized title and large lyrics",
      THEME_BG_AMBIENT_SOLID, 112,      32,                24,                buildImmersivePlayer },
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
// `ceil_lum` is what keeps a bright or washed-out album from producing a glaring
// backdrop that white lyrics disappear into — the brightness is clamped into a
// band rather than tracking the artwork upward without limit. Pass 0 to skip it
// (used for the accent colour, which is meant to be bright).
static uint32_t shade(uint32_t c, float scale, float sat, int floor_lum, int ceil_lum) {
    int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    int mean = (r + g + b) / 3;

    r = mean + (int)((r - mean) * sat);
    g = mean + (int)((g - mean) * sat);
    b = mean + (int)((b - mean) * sat);

    r = (int)(r * scale); g = (int)(g * scale); b = (int)(b * scale);
    if (r < 0) r = 0; if (g < 0) g = 0; if (b < 0) b = 0;

    // Lift very dark results so the screen doesn't read as black.
    int lum = (r * 30 + g * 59 + b * 11) / 100;
    if (lum == 0) {
        r = g = b = floor_lum;
    } else if (lum < floor_lum) {
        int lift = (floor_lum * 100) / lum;
        r = (r * lift) / 100; g = (g * lift) / 100; b = (b * lift) / 100;
    }

    // Pull bright results back down so the backdrop never overpowers the text.
    if (ceil_lum > 0) {
        lum = (r * 30 + g * 59 + b * 11) / 100;
        if (lum > ceil_lum) {
            int cut = (ceil_lum * 100) / lum;
            r = (r * cut) / 100; g = (g * cut) / 100; b = (b * cut) / 100;
        }
    }

    if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void themeApplyBackdrop(uint32_t rgb) {
    if (!scr_main) return;
    switch (themeCurrent()->bg) {
        case THEME_BG_BLUR_ART:
            // The blurred art image IS the backdrop — leave the screen colour alone.
            break;

        case THEME_BG_AMBIENT_TINT:
            // Deep and muted, painted flat: a gradient bands badly in RGB565.
            lv_obj_set_style_bg_color(scr_main, lv_color_hex(shade(rgb, 0.90f, 1.35f, 26, 64)), 0);
            break;

        case THEME_BG_AMBIENT_SOLID:
            // Rich full-bleed fill, deliberately held in a mid-dark band: bright
            // artwork used to wash this out until the white lyrics were unreadable.
            lv_obj_set_style_bg_color(scr_main, lv_color_hex(shade(rgb, 1.55f, 1.30f, 42, 100)), 0);
            // The play button is this layout's accent — kept bright (no ceiling) so
            // it still separates from the backdrop it sits on.
            if (btn_play)
                lv_obj_set_style_bg_color(btn_play, lv_color_hex(shade(rgb, 3.20f, 1.70f, 140, 0)), 0);
            break;
    }
}

void themeApplyArtGeometry(lv_obj_t* img) {
    if (!img) return;
    const ThemeDef* t = themeCurrent();

    if (t->art_x == THEME_ART_CENTRED && t->art_y == THEME_ART_CENTRED &&
        t->art_size == ART_SIZE) {
        // Unchanged Classic behaviour — full size, centred in the art panel.
        lv_image_set_scale(img, 256);
        lv_obj_set_size(img, ART_SIZE, ART_SIZE);
        lv_obj_center(img);
        return;
    }

    // The decoded source is always ART_SIZE square; scale it to the theme's box
    // (LVGL zoom is 256 = 1:1) rather than re-decoding at another size.
    lv_obj_set_size(img, SMIN(t->art_size), SMIN(t->art_size));
    lv_image_set_scale(img, (256 * t->art_size) / ART_SIZE);
    if (t->art_x == THEME_ART_CENTRED) lv_obj_center(img);
    else                               lv_obj_set_pos(img, SX(t->art_x), SY(t->art_y));
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

    // Invalidate the UI caches. updateUI() only writes a label when the value
    // differs from these, so after a rebuild the new (empty) widgets would keep
    // showing "Not Playing" until the track actually changed. Strings get a value
    // no track can match; the bools are inverted so the next tick corrects them.
    ui_title  = "\x01"; ui_artist = "\x01"; ui_repeat = "\x01";
    ui_vol    = -1;
    ui_playing = !ui_playing;
    ui_shuffle = !ui_shuffle;
    ui_muted   = !ui_muted;
    // The remaining caches (device name, album, next-up) are function-local statics
    // that can't be reached from here — this makes updateUI() re-push them once.
    ui_force_refresh = true;

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
