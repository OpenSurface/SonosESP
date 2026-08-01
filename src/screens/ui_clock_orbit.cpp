/**
 * "Orbit" screensaver face — Nocturne visual system (3a in the design handoff).
 *
 * Clock left, live sun-path arc right, 6-hour forecast as a temperature curve
 * along the bottom.
 *
 * The design draws the arc and curve as SVG. LVGL has no SVG, so both become
 * lv_line polylines: the arc is subdivided into ORB_ARC_SEG segments (smooth at
 * this size), and the curve is a straight-segment polyline rather than a cubic
 * spline. The design's gradient area-fill under the curve has no clean LVGL
 * equivalent in the RGB565 path and is dropped — the dots and stroke carry it.
 *
 * The dashed arc stroke is also not native; it is drawn solid at reduced opacity,
 * which reads closer to the design than evenly-spaced gaps at this scale.
 */

#include "ui_common.h"
#include "config.h"
#include "clock_screen.h"
#include "clock_face.h"
#include "ui_fonts.h"
#include "nocturne.h"
#include <math.h>

const char* wmoCondition(int code);            // ui_clock_screen.cpp
const char* wmoGlyph(int code, bool night);    // ui_clock_screen.cpp

LV_FONT_DECLARE(lv_font_weathericons_32);

// Layout, 800x480 design units.
#define ORB_PAD_X    46
#define ORB_HEAD_Y   20
#define ORB_CLOCK_Y  78
// Clear of the arc: the arc bottom (horizon) is ORB_ARC_CY and its sunrise/
// sunset labels run to ORB_ARC_CY+22, so the weather line starts below that.
#define ORB_META_Y   250
#define ORB_ARC_CX   612          // arc centre
#define ORB_ARC_CY   196
#define ORB_ARC_R    100
#define ORB_ARC_SEG  24           // polyline segments across the half arc
#define ORB_DOT      15
#define ORB_CURVE_Y  330
#define ORB_CURVE_H  46
#define ORB_LBL_Y    392

static lv_obj_t*      orb_root = nullptr;
static lv_obj_t*      orb_city = nullptr;
static lv_obj_t*      orb_date = nullptr;
static lv_obj_t*      orb_time = nullptr;
static lv_obj_t*      orb_icon = nullptr;
static lv_obj_t*      orb_temp = nullptr;
static lv_obj_t*      orb_meta = nullptr;
static lv_obj_t*      orb_sun  = nullptr;   // the travelling dot
static lv_obj_t*      orb_rise = nullptr;
static lv_obj_t*      orb_set  = nullptr;
static lv_obj_t*      orb_curve = nullptr;
static lv_obj_t*      orb_pt[6]     = {};
static lv_obj_t*      orb_lbl_hr[6] = {};
static lv_obj_t*      orb_lbl_ic[6] = {};
static lv_obj_t*      orb_lbl_tmp[6]= {};

// lv_line does not copy its point array — it must outlive the widget.
static lv_point_precise_t orb_arc_pts[ORB_ARC_SEG + 1];
static lv_point_precise_t orb_curve_pts[6];

static int hhmmToMinutes(const char* s) {
    if (!s || s[0] < '0' || s[0] > '9') return -1;
    return (s[0] - '0') * 600 + (s[1] - '0') * 60 + (s[3] - '0') * 10 + (s[4] - '0');
}

void buildOrbitFace(lv_obj_t* parent) {
    if (orb_root) return;

    orb_root = nocFaceRoot(parent);

    // ── Header ──────────────────────────────────────────────────────────────
    orb_city = nocLabel(orb_root, &font_text_14, NOC_N400, "");
    lv_obj_set_style_text_letter_space(orb_city, 2, 0);
    lv_obj_set_pos(orb_city, SX(ORB_PAD_X), SY(ORB_HEAD_Y));

    orb_date = nocLabel(orb_root, &font_text_14, NOC_N400, "");
    lv_obj_set_style_text_letter_space(orb_date, 2, 0);
    lv_obj_align(orb_date, LV_ALIGN_TOP_RIGHT, -SX(ORB_PAD_X), SY(ORB_HEAD_Y));

    // ── Clock + one-line summary (left) ─────────────────────────────────────
    orb_time = nocLabel(orb_root, &NOC_FONT_SM, NOC_TEXT, "--:--");
    lv_obj_set_style_text_letter_space(orb_time, -7, 0);
    lv_obj_set_pos(orb_time, SX(ORB_PAD_X - 6), SY(ORB_CLOCK_Y));

    orb_icon = nocLabel(orb_root, &lv_font_weathericons_32, NOC_N300, "");
    lv_obj_set_pos(orb_icon, SX(ORB_PAD_X), SY(ORB_META_Y));

    orb_temp = nocLabel(orb_root, &font_text_32, NOC_TEXT, "--°");
    lv_obj_set_pos(orb_temp, SX(ORB_PAD_X + 46), SY(ORB_META_Y - 4));

    orb_meta = nocLabel(orb_root, &font_text_14, NOC_N400, "");
    lv_obj_set_pos(orb_meta, SX(ORB_PAD_X + 116), SY(ORB_META_Y + 8));

    // ── Sun-path arc (right) ────────────────────────────────────────────────
    // Semicircle from due-east to due-west, subdivided into a polyline.
    for (int i = 0; i <= ORB_ARC_SEG; i++) {
        const float a = (float)M_PI * (1.0f - (float)i / ORB_ARC_SEG);
        orb_arc_pts[i].x = SX(ORB_ARC_CX + (int)lroundf(ORB_ARC_R * cosf(a)));
        orb_arc_pts[i].y = SY(ORB_ARC_CY - (int)lroundf(ORB_ARC_R * sinf(a)));
    }
    lv_obj_t* arc = lv_line_create(orb_root);
    lv_line_set_points(arc, orb_arc_pts, ORB_ARC_SEG + 1);
    lv_obj_set_style_line_color(arc, NOC_ACCENT, 0);
    lv_obj_set_style_line_width(arc, 2, 0);
    lv_obj_set_style_line_opa(arc, 120, 0);          // stands in for the dashed stroke
    lv_obj_set_style_line_rounded(arc, true, 0);

    // Horizon line under the arc.
    lv_obj_t* hz = lv_obj_create(orb_root);
    lv_obj_set_size(hz, SX(ORB_ARC_R * 2 + 30), 1);
    lv_obj_set_pos(hz, SX(ORB_ARC_CX - ORB_ARC_R - 15), SY(ORB_ARC_CY));
    lv_obj_set_style_bg_color(hz, NOC_N500, 0);
    lv_obj_set_style_bg_opa(hz, 120, 0);
    lv_obj_set_style_border_width(hz, 0, 0);
    lv_obj_set_style_radius(hz, 0, 0);

    lv_obj_t* cap = nocLabel(orb_root, &font_text_12, NOC_N500, "SUN");
    lv_obj_set_style_text_letter_space(cap, 3, 0);
    lv_obj_set_pos(cap, SX(ORB_ARC_CX - 12), SY(ORB_ARC_CY - ORB_ARC_R - 26));

    orb_rise = nocLabel(orb_root, &font_text_12, NOC_N400, "--:--");
    lv_obj_set_pos(orb_rise, SX(ORB_ARC_CX - ORB_ARC_R - 14), SY(ORB_ARC_CY + 8));
    orb_set = nocLabel(orb_root, &font_text_12, NOC_N400, "--:--");
    lv_obj_set_pos(orb_set, SX(ORB_ARC_CX + ORB_ARC_R - 20), SY(ORB_ARC_CY + 8));

    // Travelling sun dot — glow approximated with a shadow, which LVGL does
    // support on objects (unlike text).
    orb_sun = lv_obj_create(orb_root);
    lv_obj_set_size(orb_sun, SMIN(ORB_DOT), SMIN(ORB_DOT));
    lv_obj_set_style_radius(orb_sun, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(orb_sun, NOC_ACCENT, 0);
    lv_obj_set_style_border_width(orb_sun, 0, 0);
    lv_obj_set_style_shadow_color(orb_sun, NOC_ACCENT, 0);
    lv_obj_set_style_shadow_width(orb_sun, SMIN(18), 0);
    lv_obj_set_style_shadow_opa(orb_sun, 180, 0);
    // Park it at the arc apex. Before this it defaulted to (0,0) — the top-left
    // corner — and visibly jumped onto the arc once the first weather fetch
    // landed. Now it starts somewhere sensible and merely slides into place.
    lv_obj_set_pos(orb_sun, SX(ORB_ARC_CX) - SMIN(ORB_DOT) / 2,
                            SY(ORB_ARC_CY - ORB_ARC_R) - SMIN(ORB_DOT) / 2);

    // ── Temperature curve ───────────────────────────────────────────────────
    const int step = (800 - ORB_PAD_X * 2) / 6;
    for (int i = 0; i < 6; i++) {
        const int cx = ORB_PAD_X + step / 2 + i * step;
        orb_curve_pts[i].x = SX(cx);
        orb_curve_pts[i].y = SY(ORB_CURVE_Y + ORB_CURVE_H / 2);   // flat until data

        orb_pt[i] = lv_obj_create(orb_root);
        lv_obj_set_size(orb_pt[i], SMIN(8), SMIN(8));
        lv_obj_set_style_radius(orb_pt[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(orb_pt[i], i ? NOC_ACCENT_D : NOC_ACCENT, 0);
        lv_obj_set_style_border_color(orb_pt[i], NOC_ACCENT, 0);
        lv_obj_set_style_border_width(orb_pt[i], 1, 0);

        // hour · icon · temp, centred under the curve point.
        lv_obj_t* col = lv_obj_create(orb_root);
        lv_obj_remove_style_all(col);
        lv_obj_set_size(col, SX(120), SY(40));
        lv_obj_set_pos(col, SX(cx - 60), SY(ORB_LBL_Y));
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(col, SX(6), 0);
        orb_lbl_hr[i]  = nocLabel(col, &font_text_12, NOC_N500, "");
        orb_lbl_ic[i]  = nocLabel(col, &lv_font_weathericons_32, NOC_N400, "");
        orb_lbl_tmp[i] = nocLabel(col, &font_text_16, NOC_TEXT, "");
    }
    orb_curve = lv_line_create(orb_root);
    lv_line_set_points(orb_curve, orb_curve_pts, 6);
    lv_obj_set_style_line_color(orb_curve, NOC_ACCENT, 0);
    lv_obj_set_style_line_width(orb_curve, 2, 0);
    lv_obj_set_style_line_rounded(orb_curve, true, 0);

    lv_obj_t* kick = nocLabel(orb_root, &font_text_12, NOC_N500, "NEXT 6 HOURS");
    lv_obj_set_style_text_letter_space(kick, 3, 0);
    lv_obj_set_pos(kick, SX(ORB_PAD_X), SY(ORB_CURVE_Y - 24));

    // Taps must reach scr_clock so exitClockScreen() can fire.
    nocMakeInert(orb_root);
}

lv_obj_t* orbitRoot(void) { return orb_root; }

void orbitTick(const struct tm* now) {
    if (!orb_root || !now) return;

    char buf[64];
    strftime(buf, sizeof(buf), clock_12h ? "%I:%M" : "%H:%M", now);
    const char* t = (clock_12h && buf[0] == '0') ? buf + 1 : buf;
    lv_label_set_text(orb_time, t);
    strftime(buf, sizeof(buf), "%A %d %B", now);
    lv_label_set_text(orb_date, buf);
    lv_label_set_text(orb_city, clock_wx_city_name[0] ? clock_wx_city_name : "");

    if (!clock_wx_valid) return;

    lv_label_set_text(orb_icon, wmoGlyph(clock_wx_wmo, false));
    lv_label_set_text_fmt(orb_temp, "%d°", clock_wx_temp);
    lv_label_set_text_fmt(orb_meta, "%s · feels %d° · humidity %d%% · wind %d km/h · UV %d",
                          wmoCondition(clock_wx_wmo), clock_wx_apparent,
                          clock_wx_humidity, clock_wx_wind, clock_wx_uv);
    lv_label_set_text(orb_rise, clock_wx_sunrise);
    lv_label_set_text(orb_set,  clock_wx_sunset);

    // Sun position: linear along the arc between sunrise and sunset, clamped.
    const int rise = hhmmToMinutes(clock_wx_sunrise);
    const int set  = hhmmToMinutes(clock_wx_sunset);
    if (rise >= 0 && set > rise) {
        const int mins = now->tm_hour * 60 + now->tm_min;
        float f = (float)(mins - rise) / (float)(set - rise);
        if (f < 0.0f) f = 0.0f;
        if (f > 1.0f) f = 1.0f;
        const float a = (float)M_PI * (1.0f - f);
        const int cx = ORB_ARC_CX + (int)lroundf(ORB_ARC_R * cosf(a));
        const int cy = ORB_ARC_CY - (int)lroundf(ORB_ARC_R * sinf(a));
        lv_obj_set_pos(orb_sun, SX(cx) - SMIN(ORB_DOT) / 2, SY(cy) - SMIN(ORB_DOT) / 2);
    }

    // Curve: map the hourly min..max onto the band, then move the points and dots.
    int lo = clock_wx_hourly[0].temp, hi = lo;
    for (int i = 1; i < 6; i++) {
        if (clock_wx_hourly[i].temp < lo) lo = clock_wx_hourly[i].temp;
        if (clock_wx_hourly[i].temp > hi) hi = clock_wx_hourly[i].temp;
    }
    const int span = (hi > lo) ? (hi - lo) : 1;
    for (int i = 0; i < 6; i++) {
        const int y = ORB_CURVE_Y + ORB_CURVE_H
                    - (clock_wx_hourly[i].temp - lo) * ORB_CURVE_H / span;
        orb_curve_pts[i].y = SY(y);
        lv_obj_set_pos(orb_pt[i], (int)orb_curve_pts[i].x - SMIN(8) / 2,
                                   SY(y) - SMIN(8) / 2);
        lv_label_set_text_fmt(orb_lbl_hr[i],  "%dh", i + 1);
        lv_label_set_text(orb_lbl_ic[i], wmoGlyph(clock_wx_hourly[i].wmo, false));
        lv_label_set_text_fmt(orb_lbl_tmp[i], "%d°", clock_wx_hourly[i].temp);
    }
    // Re-point the polyline so LVGL picks up the new Y values.
    lv_line_set_points(orb_curve, orb_curve_pts, 6);
    lv_obj_invalidate(orb_curve);
}
