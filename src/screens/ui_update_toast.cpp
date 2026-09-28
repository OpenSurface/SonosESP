/**
 * Update toast — "Version 2.2.1 is available", top centre, tap to open Updates.
 *
 * ── Why lv_layer_top() and not a child of scr_main ──────────────────────────
 * The toast has to outlive whatever is on screen. themeSet() deletes and
 * rebuilds the entire player, every settings screen is its own lv_obj, and the
 * clock screensaver loads a different screen again — a toast parented to any of
 * them would either vanish mid-animation or leave a dangling pointer behind,
 * which is the bug class the sb_docks registry exists to work around.
 *
 * lv_layer_top() is per-display, not per-screen: it draws above every screen and
 * nothing deletes it. That also means the toast survives the user navigating
 * while it is up, which is the correct behaviour — the notice is about the
 * device, not about the page.
 *
 * ── Why it is click-through when hidden ─────────────────────────────────────
 * The top layer covers the whole display. Left clickable it would swallow every
 * touch on the player underneath, so the layer itself never takes input and only
 * the pill is clickable, and only while it is shown.
 */

#include "ui_common.h"
#include "ui_theme.h"
#include "ui_fonts.h"
#include "amber.h"
#include "amber_icons.h"

// Lives here, not in config.h: SY() comes from ui_scale.h, which includes
// config.h and not the other way round, so this was the one macro in
// config.h that only compiled because every user happened to pull in
// ui_scale.h first. It is layout, not configuration.
#define UPDATE_TOAST_Y  SY(14)   // resting distance from the top edge

static lv_obj_t*  toast     = nullptr;
static lv_timer_t* hide_tmr = nullptr;

// Remembers which version was already announced, so a background check that
// keeps finding the same release does not re-toast every few hours. Cleared
// only by a newer version appearing.
static String announced_version = "";

static void toastDismiss(bool animate);

// ── Animation ───────────────────────────────────────────────────────────────
// Slides down from behind the top edge and fades in together; the reverse on
// the way out. Two properties on one object, so they share a timeline.
static void toastYCb(void* obj, int32_t v) {
    lv_obj_set_y((lv_obj_t*)obj, v);
}
static void toastOpaCb(void* obj, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t*)obj, (lv_opa_t)v, 0);
}
// Deliberately a second, identical function rather than reusing toastOpaCb:
// lv_anim_get() matches on (var, exec_cb), so sharing one callback makes the
// dismiss guard below unable to tell an entrance fade from an exit fade.
static void toastOpaOutCb(void* obj, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t*)obj, (lv_opa_t)v, 0);
}
static void toastDeleteCb(lv_anim_t* a) {
    lv_obj_t* o = (lv_obj_t*)a->var;
    if (o == toast) toast = nullptr;
    if (o) lv_obj_del(o);
}

static void hideTimerCb(lv_timer_t* t) {
    LV_UNUSED(t);
    hide_tmr = nullptr;      // one-shot; LVGL deletes it for us below
    toastDismiss(true);
}

static void toastDismiss(bool animate) {
    if (hide_tmr) { lv_timer_del(hide_tmr); hide_tmr = nullptr; }
    if (!toast) return;

    if (!animate) {
        lv_obj_del(toast);
        toast = nullptr;
        return;
    }

    // Guard against a second dismiss landing mid-flight: an in-progress EXIT
    // fade already owns the object and will delete it. Matching on the exit
    // callback specifically - the previous version matched toastOpaCb, which
    // the entrance fade also uses, so if UPDATE_TOAST_HOLD_MS were ever dropped
    // below UPDATE_TOAST_FADE_MS the dismiss would no-op against its own
    // entrance animation, with hide_tmr already deleted, and the toast would
    // stay on screen forever.
    if (lv_anim_get(toast, toastOpaOutCb)) return;
    // Splitting the callbacks also means lv_anim_start() no longer implicitly
    // replaces an in-flight entrance fade, so kill it explicitly or the two
    // opacity animations fight.
    lv_anim_delete(toast, toastOpaCb);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, toast);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, UPDATE_TOAST_FADE_MS);
    lv_anim_set_exec_cb(&a, toastOpaOutCb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&a, toastDeleteCb);   // frees the object, clears `toast`
    lv_anim_start(&a);

    lv_anim_t b;
    lv_anim_init(&b);
    lv_anim_set_var(&b, toast);
    lv_anim_set_values(&b, UPDATE_TOAST_Y, -SY(64));
    lv_anim_set_duration(&b, UPDATE_TOAST_FADE_MS);
    lv_anim_set_exec_cb(&b, toastYCb);
    lv_anim_set_path_cb(&b, lv_anim_path_ease_in);
    lv_anim_start(&b);
}

static void toastClicked(lv_event_t* e) {
    LV_UNUSED(e);
    // Dismiss without animating: the screen is about to change underneath it,
    // and a pill fading out over a screen it was never placed on looks wrong.
    toastDismiss(false);
    // The Updates screen clears its last result on load, so that it never shows
    // a stale version as current. We are carrying a fresh one — the whole point
    // of the tap — so suppress that reset exactly once.
    //
    // Only when the load will actually happen. lv_display.c returns early if
    // the requested screen is already active, so LV_EVENT_SCREEN_LOADED never
    // fires and nothing consumes the flag — it would survive to the NEXT visit
    // and suppress a reset that was genuinely wanted, showing a stale version
    // as current with Install armed. The toast can be up on any screen,
    // including Settings -> Updates itself, so this is reachable by tapping it
    // where it already points.
    if (scr_ota && lv_screen_active() != scr_ota) {
        ota_skip_load_reset = true;
        lv_screen_load(scr_ota);
    }
}

void updateToastShow(const char* version) {
    if (!version || !version[0]) return;
    if (announced_version == version) return;   // already told them about this one
    announced_version = version;

    // Replace any toast still up, rather than stacking two.
    if (toast) toastDismiss(false);

    lv_obj_t* layer = lv_layer_top();
    // The layer spans the display. Without this it eats every touch meant for
    // the player sitting underneath it.
    lv_obj_set_clickable(layer, false);
    lv_obj_set_style_bg_opa(layer, LV_OPA_TRANSP, 0);

    toast = lv_button_create(layer);
    lv_obj_set_height(toast, SY(56));
    lv_obj_set_width(toast, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(toast, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(toast, AMB_CARD, 0);
    lv_obj_set_style_bg_color(toast, AMB_RAISED, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(toast, 1, 0);
    lv_obj_set_style_border_color(toast, AMB_ACCENT_DIM, 0);
    lv_obj_set_style_pad_hor(toast, SX(24), 0);
    lv_obj_set_style_pad_ver(toast, 0, 0);
    // A real shadow, because this floats over arbitrary artwork and needs to
    // separate from it. Everything else in the UI is flat by design.
    lv_obj_set_style_shadow_width(toast, SMIN(24), 0);
    lv_obj_set_style_shadow_opa(toast, LV_OPA_50, 0);
    lv_obj_set_style_shadow_color(toast, lv_color_black(), 0);
    lv_obj_set_style_shadow_ofs_y(toast, SY(6), 0);
    lv_obj_add_event_cb(toast, toastClicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t* row = lv_obj_create(toast);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, SX(12), 0);
    lv_obj_set_clickable(row, false);   // let taps reach the button
    lv_obj_center(row);

    lv_obj_t* ico = lv_label_create(row);
    lv_label_set_text(ico, AMB_IC_DOWNLOAD);
    lv_obj_set_style_text_font(ico, &font_icon_24, 0);
    lv_obj_set_style_text_color(ico, AMB_ACCENT, 0);

    lv_obj_t* txt = lv_label_create(row);
    lv_label_set_text_fmt(txt, "Version %s is available", version);
    lv_obj_set_style_text_font(txt, &font_text_16, 0);
    lv_obj_set_style_text_color(txt, AMB_TEXT, 0);

    lv_obj_t* cta = lv_label_create(row);
    lv_label_set_text(cta, "Update");
    lv_obj_set_style_text_font(cta, &font_text_16, 0);
    lv_obj_set_style_text_color(cta, AMB_ACCENT, 0);

    // Position before the entrance animation so the first painted frame is
    // already correct, rather than a flash at the centre of the layer.
    lv_obj_update_layout(toast);
    lv_obj_align(toast, LV_ALIGN_TOP_MID, 0, -SY(64));
    lv_obj_set_style_opa(toast, LV_OPA_TRANSP, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, toast);
    lv_anim_set_values(&a, -SY(64), UPDATE_TOAST_Y);
    lv_anim_set_duration(&a, UPDATE_TOAST_FADE_MS);
    lv_anim_set_exec_cb(&a, toastYCb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    lv_anim_t b;
    lv_anim_init(&b);
    lv_anim_set_var(&b, toast);
    lv_anim_set_values(&b, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&b, UPDATE_TOAST_FADE_MS);
    lv_anim_set_exec_cb(&b, toastOpaCb);
    lv_anim_start(&b);

    hide_tmr = lv_timer_create(hideTimerCb, UPDATE_TOAST_HOLD_MS, NULL);
    lv_timer_set_repeat_count(hide_tmr, 1);

    Serial.printf("[UPDATE] Toast: v%s available\n", version);
}

void updateToastHide(void) {
    toastDismiss(false);
}
