#pragma once
/**
 * Material Design Icons (MDI 7.4) — full UI icon set
 *
 * Font files: src/fonts/lv_font_mdi_16.c  (16px, bpp4)  — small labels/status/button text (includes Montserrat ASCII)
 *             src/fonts/lv_font_mdi_24.c  (24px, bpp4)  — sidebar icons + large buttons (includes Montserrat ASCII)
 *             src/fonts/lv_font_mdi_32.c  (32px, bpp4)  — shuffle/repeat/volume
 *             src/fonts/lv_font_mdi_40.c  (40px, bpp4)  — play/pause + prev/next
 *             src/fonts/lv_font_mdi_80.c  (80px, bpp4)  — line-in waveform + TV hero icons
 *
 * Generated with:
 *   npx lv_font_conv --font node_modules/@mdi/font/fonts/materialdesignicons-webfont.ttf
 *     --size 32|40 --format lvgl --bpp 4 --no-compress
 *     --range 0xF040A,0xF03E4,0xF04AE,0xF04AD,0xF049D,0xF0456,0xF0458,0xF057E,
 *             0xF0387,0xF0411,0xF0493,0xF004D,0xF1720,0xF0439,0xF0384
 *
 * Each macro is the UTF-8 byte sequence for the MDI codepoint.
 * Usage:  lv_label_set_text(lbl, MDI_PLAY);
 *         lv_obj_set_style_text_font(lbl, &lv_font_mdi_40, 0);
 */

// MDI codepoint → UTF-8 string literal
// ── Player controls (lv_font_mdi_32 / lv_font_mdi_40) ──────────────────────
#define MDI_PLAY               "\xF3\xB0\x90\x8A"   // U+F040A  mdi-play
#define MDI_PAUSE              "\xF3\xB0\x8F\xA4"   // U+F03E4  mdi-pause
#define MDI_SKIP_PREV          "\xF3\xB0\x92\xAE"   // U+F04AE  mdi-skip-previous
#define MDI_SKIP_NEXT          "\xF3\xB0\x92\xAD"   // U+F04AD  mdi-skip-next
#define MDI_SHUFFLE            "\xF3\xB0\x92\x9D"   // U+F049D  mdi-shuffle
#define MDI_REPEAT             "\xF3\xB0\x91\x96"   // U+F0456  mdi-repeat
#define MDI_REPEAT_ONCE        "\xF3\xB0\x91\x98"   // U+F0458  mdi-repeat-once
#define MDI_VOLUME_HIGH        "\xF3\xB0\x95\xBE"   // U+F057E  mdi-volume-high
#define MDI_VOLUME_OFF         "\xF3\xB0\x96\x81"   // U+F0581  mdi-volume-off  (muted)
#define MDI_MUSIC_NOTE         "\xF3\xB0\x8E\x87"   // U+F0387  mdi-music-note
#define MDI_MUSIC_BOX          "\xF3\xB0\x8E\x84"   // U+F0384  mdi-music-box
#define MDI_PLAYLIST           "\xF3\xB0\x90\x91"   // U+F0411  mdi-playlist-play
#define MDI_COG                "\xF3\xB0\x92\x93"   // U+F0493  mdi-cog
#define MDI_ARROW_LEFT         "\xF3\xB0\x81\x8D"   // U+F004D  mdi-arrow-left
#define MDI_BROADCAST          "\xF3\xB1\x9C\xA0"   // U+F1720  mdi-broadcast
#define MDI_RADIO              "\xF3\xB0\x90\xB9"   // U+F0439  mdi-radio
// HERO GLYPHS — 40 and 80 ONLY. These two are not generated into lv_font_mdi_16,
// _24 or _32, so using them at those sizes draws a tofu box rather than an icon
// and nothing warns you. Genres shipped with MDI_WAVEFORM at 16 and looked like a
// blank square. If you need one of these in a list row, either pick a different
// icon or regenerate the smaller fonts to include the codepoint.
#define MDI_WAVEFORM           "\xF3\xB1\xA1\xAC"   // U+F186C  mdi-audio-input-stereo-minijack  (line-in hero, 40/80 only)
#define MDI_TELEVISION         "\xF3\xB0\x94\x82"   // U+F0502  mdi-television  (TV audio hero, 40/80 only)

// ── Settings / navigation (lv_font_mdi_24, includes Montserrat ASCII) ───────
#define MDI_SPEAKER            "\xF3\xB0\x93\x83"   // U+F04C3  mdi-speaker
#define MDI_SPEAKER_MULTIPLE   "\xF3\xB0\x8F\x9B"   // U+F03DB  mdi-speaker-multiple  (groups)
#define MDI_CLOSE              "\xF3\xB0\x85\x96"   // U+F0156  mdi-close
#define MDI_FOLDER             "\xF3\xB0\x89\x8B"   // U+F024B  mdi-folder
#define MDI_DOWNLOAD           "\xF3\xB0\x87\x9A"   // U+F01DA  mdi-download
#define MDI_MONITOR            "\xF3\xB0\x8E\xA8"   // U+F03A8  mdi-monitor  (display settings)
#define MDI_CHECK              "\xF3\xB0\x84\xAC"   // U+F012C  mdi-check
#define MDI_PLUS               "\xF3\xB0\x90\x95"   // U+F0415  mdi-plus
#define MDI_REFRESH            "\xF3\xB0\x91\x90"   // U+F0450  mdi-refresh
#define MDI_CHEVRON_RIGHT      "\xF3\xB0\x85\x82"   // U+F0142  mdi-chevron-right
#define MDI_ALERT              "\xF3\xB0\x80\xA6"   // U+F0026  mdi-alert
#define MDI_WIFI               "\xF3\xB0\x96\xA9"   // U+F05A9  mdi-wifi
#define MDI_CLOCK_OUTLINE      "\xF3\xB0\x85\x90"   // U+F0150  mdi-clock-outline

LV_FONT_DECLARE(lv_font_mdi_16);
LV_FONT_DECLARE(lv_font_mdi_24);
LV_FONT_DECLARE(lv_font_mdi_32);
LV_FONT_DECLARE(lv_font_mdi_40);
LV_FONT_DECLARE(lv_font_mdi_80);
