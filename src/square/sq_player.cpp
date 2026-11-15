// sq_player.cpp — the Lecture screen (pink).
#include "sq_ui.h"
#include "sq_work.h"
#include "ui_common.h"

static lv_obj_t* s_scr;
static lv_obj_t* s_star;
static lv_obj_t* s_star_icon;
static lv_obj_t* s_cover_box;
static lv_obj_t* s_cover_img;
static lv_obj_t* s_cover_ph;      // placeholder shown until the art lands
static lv_obj_t* s_lyr_badge;     // "Paroles", only when there are some
static lv_obj_t* s_chip;
static lv_obj_t* s_chip_lbl;
static lv_obj_t* s_title;
static lv_obj_t* s_artist;
static lv_obj_t* s_bar_fill;
static lv_obj_t* s_t_now;
static lv_obj_t* s_t_total;
static lv_obj_t* s_play_icon;
static lv_obj_t* s_vol_fill;

static String   s_art_url;
static uint32_t s_art_seen = 0;
static bool     s_playing  = false;
static int      s_volume   = 0;
static uint32_t s_vol_touched = 0;   // ignore polled volume briefly after a tap
static String   s_uri;
static String   s_track;
static String   s_artist_now;
static uint32_t s_lyr_seen = 0;
static uint32_t s_play_ms = 0;      // when Play was tapped, to catch a refusal
static uint32_t s_toggle_ms = 0;    // last Play/Pause tap: polls before it lands are stale

#define COVER 188

static void playClicked(lv_event_t*) {
    if (!s_playing && !s_track.length() && !s_uri.length()) {
        // Nothing loaded on this speaker: Play would only draw a UPnP fault.
        sqToast("Rien à reprendre ici : choisis un favori");
        sqShow(SQ_SCR_FAVS);
        return;
    }
    if (s_playing) sonos.pause(); else { sonos.play(); s_play_ms = millis(); }
    s_toggle_ms = millis();
    s_playing = !s_playing;
    lv_label_set_text(s_play_icon, s_playing ? IC_PAUSE : IC_PLAY);
}
static void prevClicked(lv_event_t*) { sonos.previous(); }
static void nextClicked(lv_event_t*) { sonos.next(); }

static void volumeShow(void) {
    lv_obj_set_width(s_vol_fill, s_volume <= 0 ? 0 : (s_volume >= 100 ? 92 : 92 * s_volume / 100));
}
static void volStep(int step) {
    s_volume = constrain(s_volume + step, 0, 100);
    s_vol_touched = millis();
    sonos.setVolume(s_volume);
    volumeShow();
}
static void volDown(lv_event_t*) { volStep(-5); }
static void volUp(lv_event_t*)   { volStep(+5); }

static void starClicked(lv_event_t*) {
    int f = sqFavsFind(s_uri.c_str());
    if (f >= 0) {
        SqFav fav;
        if (sqFavsGet(f, &fav)) sqWorkPost(SQ_JOB_FAV_REMOVE, 0, fav.id);
    } else {
        sqWorkPost(SQ_JOB_FAV_ADD_CURRENT);
    }
    sqToast(f >= 0 ? "Je retire des favoris…" : "J'ajoute aux favoris…");
}

static void starShow(void) {
    bool fav = s_uri.length() && sqFavsFind(s_uri.c_str()) >= 0;
    lv_obj_set_style_bg_color(s_star, lv_color_hex(fav ? SQ_YELLOW : SQ_RAISED), 0);
    lv_label_set_text(s_star_icon, fav ? IC_STAR : IC_STAR_O);
    lv_obj_set_style_text_color(s_star_icon, lv_color_hex(fav ? SQ_INK : SQ_YELLOW), 0);
}

static bool lyricsAvailable(void) {
    SqLyrState st = sqLyricsState();
    return st == SQ_LYR_SYNCED || st == SQ_LYR_PLAIN;
}

static void coverClicked(lv_event_t*) {
    SqLyrState st = sqLyricsState();
    if (st == SQ_LYR_SYNCED || st == SQ_LYR_PLAIN || st == SQ_LYR_LOADING) sqShow(SQ_SCR_LYRICS);
}

static void lyricsBadgeShow(void) {
    s_lyr_seen = sq_lyrics_version;
    sqSetHidden(s_lyr_badge, !lyricsAvailable());
}

const char* sqPlayerTitle(void)  { return s_track.c_str(); }
const char* sqPlayerArtist(void) { return s_artist_now.c_str(); }
const char* sqPlayerArtUrl(void) { return s_art_url.c_str(); }

static void fmtTime(char* out, size_t n, int s) {
    if (s < 0) s = 0;
    if (s >= 3600) snprintf(out, n, "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
    else           snprintf(out, n, "%d:%02d", s / 60, s % 60);
}

void sqPlayerCreate(void) {
    s_scr = sqScreenCreate();

    // Header: speaker pill + star
    lv_obj_t* hdr = sqBox(s_scr, SQ_W, SQ_HEADER_H, SQ_BG, 0);
    lv_obj_set_style_pad_hor(hdr, 16, 0);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    sqRoomPill(hdr, false);
    s_star = sqRoundButton(hdr, 52, SQ_RAISED, IC_STAR_O, &font_sqi_32, SQ_YELLOW);
    s_star_icon = lv_obj_get_child(s_star, 0);
    lv_obj_add_event_cb(s_star, starClicked, LV_EVENT_CLICKED, nullptr);

    // Cover + info
    s_cover_box = sqBox(s_scr, COVER, COVER, SQ_SURFACE, 28);
    lv_obj_set_pos(s_cover_box, 20, 76);
    s_cover_ph = sqCoverPlaceholder(s_cover_box, COVER, 28, "sonos");
    s_cover_img = lv_image_create(s_cover_box);
    lv_obj_set_size(s_cover_img, COVER, COVER);
    lv_obj_set_style_radius(s_cover_img, 28, 0);
    sqSetHidden(s_cover_img, true);

    // The whole cover opens the lyrics; a badge says when there are some.
    // A transparent button on top, because the image and the placeholder's
    // shapes would otherwise take the tap.
    lv_obj_t* hit = lv_button_create(s_cover_box);
    lv_obj_remove_style_all(hit);
    lv_obj_set_size(hit, COVER, COVER);
    lv_obj_add_event_cb(hit, coverClicked, LV_EVENT_CLICKED, nullptr);
    s_lyr_badge = sqBox(hit, LV_SIZE_CONTENT, 38, SQ_BG, 19);
    lv_obj_remove_flag(s_lyr_badge, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_left(s_lyr_badge, 10, 0);
    lv_obj_set_style_pad_right(s_lyr_badge, 14, 0);
    lv_obj_set_flex_flow(s_lyr_badge, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_lyr_badge, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_lyr_badge, 6, 0);
    lv_obj_align(s_lyr_badge, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    sqIcon(s_lyr_badge, IC_MIC, &font_sqi_20, SQ_PINK);
    sqLabel(s_lyr_badge, "Paroles", &font_sq_16, SQ_TEXT);
    sqSetHidden(s_lyr_badge, true);

    lv_obj_t* info = lv_obj_create(s_scr);
    lv_obj_remove_style_all(info);
    lv_obj_set_pos(info, 20 + COVER + 18, 76);
    lv_obj_set_size(info, SQ_W - (20 + COVER + 18) - 20, COVER);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(info, 6, 0);
    lv_obj_set_scrollable(info, false);

    s_chip = sqBox(info, LV_SIZE_CONTENT, 28, 0x173F3A, 14);
    lv_obj_set_style_pad_hor(s_chip, 12, 0);
    lv_obj_set_style_max_width(s_chip, 234, 0);
    s_chip_lbl = sqLabel(s_chip, "", &font_sq_14, 0x7FF0C4);
    lv_label_set_long_mode(s_chip_lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_max_width(s_chip_lbl, 210, 0);
    lv_obj_set_height(s_chip_lbl, 19);
    lv_obj_center(s_chip_lbl);
    sqSetHidden(s_chip, true);

    s_title = sqLabel(info, "Rien en cours", &font_sq_28, SQ_TEXT);
    lv_obj_set_width(s_title, LV_PCT(100));
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_max_height(s_title, 72, 0);
    s_artist = sqLabel(info, "Choisis un favori ou cherche une chanson", &font_sq_20, SQ_MUTED);
    lv_obj_set_width(s_artist, LV_PCT(100));
    lv_label_set_long_mode(s_artist, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_max_height(s_artist, 58, 0);

    lv_obj_t* spacer = lv_obj_create(info);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_flex_grow(spacer, 1);

    lv_obj_t* bar = sqBox(info, LV_PCT(100), 12, SQ_RAISED, 6);
    s_bar_fill = sqBox(bar, 0, 12, SQ_PINK, 6);
    lv_obj_t* times = lv_obj_create(info);
    lv_obj_remove_style_all(times);
    lv_obj_set_size(times, LV_PCT(100), LV_SIZE_CONTENT);
    s_t_now = sqLabel(times, "", &font_sq_14, SQ_MUTED);
    s_t_total = sqLabel(times, "", &font_sq_14, SQ_MUTED);
    lv_obj_align(s_t_total, LV_ALIGN_TOP_RIGHT, 0, 0);

    // Controls
    lv_obj_t* ctl = lv_obj_create(s_scr);
    lv_obj_remove_style_all(ctl);
    lv_obj_set_pos(ctl, 20, 76 + COVER + 22);
    lv_obj_set_size(ctl, SQ_W - 40, 92);
    lv_obj_set_flex_flow(ctl, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctl, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ctl, 12, 0);
    lv_obj_set_scrollable(ctl, false);

    lv_obj_t* b = sqRoundButton(ctl, 60, SQ_RAISED, IC_PREV, &font_sqi_32, SQ_TEXT);
    lv_obj_add_event_cb(b, prevClicked, LV_EVENT_CLICKED, nullptr);
    b = sqRoundButton(ctl, 84, SQ_PINK, IC_PLAY, &font_sqi_44, SQ_INK);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_shadow_offset_y(b, 6, 0);
    lv_obj_set_style_shadow_spread(b, 0, 0);
    lv_obj_set_style_shadow_color(b, lv_color_hex(0xB84A80), 0);
    lv_obj_set_style_shadow_opa(b, LV_OPA_COVER, 0);
    s_play_icon = lv_obj_get_child(b, 0);
    lv_obj_add_event_cb(b, playClicked, LV_EVENT_CLICKED, nullptr);
    b = sqRoundButton(ctl, 60, SQ_RAISED, IC_NEXT, &font_sqi_32, SQ_TEXT);
    lv_obj_add_event_cb(b, nextClicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* vol = lv_obj_create(ctl);
    lv_obj_remove_style_all(vol);
    lv_obj_set_size(vol, LV_SIZE_CONTENT, 48);
    lv_obj_set_style_margin_left(vol, 4, 0);
    lv_obj_set_flex_flow(vol, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(vol, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(vol, 8, 0);
    b = sqRoundButton(vol, 44, SQ_RAISED, IC_MINUS, &font_sqi_26, SQ_SKY);
    lv_obj_add_event_cb(b, volDown, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* vbar = sqBox(vol, 92, 16, SQ_RAISED, 8);
    s_vol_fill = sqBox(vbar, 0, 16, SQ_SKY, 8);
    b = sqRoundButton(vol, 44, SQ_RAISED, IC_PLUS, &font_sqi_26, SQ_SKY);
    lv_obj_add_event_cb(b, volUp, LV_EVENT_CLICKED, nullptr);

    sqRegisterScreen(SQ_SCR_PLAYER, s_scr, SQ_TAB_PLAYER);
}

static void artShow(void) {
    const lv_image_dsc_t* img = s_art_url.length() ? sqArtGet(s_art_url.c_str(), COVER) : nullptr;
    if (img) {
        lv_image_set_src(s_cover_img, img);
        sqSetHidden(s_cover_img, false);
        sqSetHidden(s_cover_ph, true);
    } else {
        sqSetHidden(s_cover_img, true);
        sqSetHidden(s_cover_ph, false);
    }
}

void sqPlayerUpdate(void) {
    if (sonos.isDiscovering()) return;   // the device list is being rebuilt
    SonosDevice* d = sonos.getCurrentDevice();
    if (!d) return;
    String track, artist, album, art, uri, station, stream;
    bool radio = false, linein = false, tv = false;
    if (!xSemaphoreTake(sonos.getDeviceMutex(), pdMS_TO_TICKS(30))) return;
    track   = d->currentTrack;
    artist  = d->currentArtist;
    album   = d->currentAlbum;
    art     = d->albumArtURL.length() ? d->albumArtURL : d->radioStationArtURL;
    uri     = d->currentURI;
    station = d->radioStationName;
    stream  = d->streamContent;
    radio   = d->isRadioStation;
    linein  = d->isLineIn;
    tv      = d->isTvAudio;
    int dur_s = d->durationSeconds;
    xSemaphoreGive(sonos.getDeviceMutex());
    const String song = track;   // before a radio stream's text replaces it

    // A poll answered before the speaker acted on the tap would flip the
    // button back, and the next tap would undo the first.
    if (millis() - s_toggle_ms > 3000) s_playing = d->isPlaying;
    lv_label_set_text(s_play_icon, s_playing ? IC_PAUSE : IC_PLAY);
    if (millis() - s_vol_touched > 2500) { s_volume = d->volume; volumeShow(); }

    const char* chip = nullptr;
    String chipText;
    if (tv)          chip = "Télévision";
    else if (linein) chip = "Entrée audio";
    else if (radio)  { chipText = station.length() ? "Radio · " + station : String("Radio"); chip = chipText.c_str(); }
    else if (album.length()) chip = album.c_str();
    lv_label_set_text(s_chip_lbl, chip ? chip : "");
    sqSetHidden(s_chip, !chip);

    if (radio && stream.length()) { track = stream; }
    if (radio && !track.length()) track = station;
    lv_label_set_text(s_title, track.length() ? track.c_str() : "Rien en cours");
    lv_label_set_text(s_artist, track.length() ? artist.c_str() : "Choisis un favori ou cherche une chanson");

    s_uri = uri;
    s_track = track;
    s_artist_now = artist;
    starShow();
    // Lyrics: songs only, not a radio stream's "now playing" text.
    sqLyricsWant(artist.c_str(), song.c_str(), dur_s,
                 !radio && !linein && !tv && artist.length());
    lyricsBadgeShow();
    if (art != s_art_url) {
        s_art_url = art;
        lv_obj_t* old = s_cover_ph;
        s_cover_ph = sqCoverPlaceholder(s_cover_box, COVER, 28, (track + artist).c_str());
        lv_obj_move_background(s_cover_ph);
        lv_obj_delete(old);
        artShow();
    }
    sqPlayerTick();
}

void sqPlayerTick(void) {
    if (sonos.isDiscovering()) return;
    SonosDevice* d = sonos.getCurrentDevice();
    if (!d) return;
    if (sq_art_version != s_art_seen) { s_art_seen = sq_art_version; artShow(); }
    if (sq_lyrics_version != s_lyr_seen) lyricsBadgeShow();
    if (s_play_ms) {
        if (sonos.lastUpnpErrorMs > s_play_ms && !strcmp(sonos.lastUpnpAction, "Play")) {
            s_play_ms = 0;
            sqToast(sonos.lastUpnpError == 701 ? "Rien à reprendre ici : choisis un favori"
                                               : "L'enceinte refuse de jouer");
        } else if (millis() - s_play_ms > 4000) {
            s_play_ms = 0;
        }
    }
    int dur = d->durationSeconds;
    int pos = d->relTimeSeconds;
    if (d->isPlaying && d->posReadMs) pos += (int)((millis() - d->posReadMs) / 1000);
    if (dur > 0 && pos > dur) pos = dur;
    lv_obj_t* bar = lv_obj_get_parent(s_bar_fill);
    int w = lv_obj_get_width(bar);
    lv_obj_set_width(s_bar_fill, dur > 0 ? w * pos / dur : 0);
    char a[16], b[16];
    fmtTime(a, sizeof(a), pos);
    fmtTime(b, sizeof(b), dur);
    lv_label_set_text(s_t_now, dur > 0 ? a : "");
    lv_label_set_text(s_t_total, dur > 0 ? b : "");
    starShow();
}
