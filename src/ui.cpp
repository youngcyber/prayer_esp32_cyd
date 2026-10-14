#include "ui.h"

#include <lvgl.h>
#include <time.h>

#include "app_types.h"
#include "fonts/fonts.h"
#include "net_task.h"

// ---- Islamic Modern palette: emerald + gold ----
#define COL_BG          lv_color_hex(0x062A22)
#define COL_BAR         lv_color_hex(0x04211A)
#define COL_CARD        lv_color_hex(0x0B3D30)
#define COL_CARD_HI     lv_color_hex(0x135A45)
#define COL_EMERALD     lv_color_hex(0x1E8C6E)
#define COL_GOLD        lv_color_hex(0xD4AF37)
#define COL_GOLD_LIGHT  lv_color_hex(0xF2D27A)
#define COL_TEXT        lv_color_hex(0xEFF7F2)
#define COL_MUTED       lv_color_hex(0x9FC3B5)
#define COL_OK          lv_color_hex(0x5EE0A8)
#define COL_WARN        lv_color_hex(0xF5A524)
#define COL_ERR         lv_color_hex(0xE5484D)

static const char* const HIJRI_MONTHS[12] = {
    "มุฮัรรอม", "ศอฟัร", "รอบีอุลเอาวัล", "รอบีอุลอาคิร", "ญุมาดัลอูลา", "ญุมาดัลอาคิเราะฮ์",
    "รอญับ", "ชะอ์บาน", "รอมฎอน", "เชาวาล", "ซุลกิอ์ดะฮ์", "ซุลฮิจญะฮ์"};
static const char* const TH_DAYS[7] = {"อา.", "จ.", "อ.", "พ.", "พฤ.", "ศ.", "ส."};
static const char* const TH_MONTHS[12] = {"ม.ค.", "ก.พ.", "มี.ค.", "เม.ย.", "พ.ค.", "มิ.ย.",
                                          "ก.ค.", "ส.ค.", "ก.ย.", "ต.ค.", "พ.ย.", "ธ.ค."};

// ---- widgets ----
static lv_obj_t* s_dropdown;
static lv_obj_t* s_clock;
static lv_obj_t* s_wifi;
static lv_obj_t* s_date;
static lv_obj_t* s_hijri;
static lv_obj_t* s_nextName;
static lv_obj_t* s_nextAt;
static lv_obj_t* s_countdown;
static lv_obj_t* s_cells[PRAYER_COUNT];
static lv_obj_t* s_cellName[PRAYER_COUNT];
static lv_obj_t* s_cellTime[PRAYER_COUNT];
static lv_obj_t* s_spinner;
static lv_obj_t* s_status;
static lv_obj_t* s_location;

// ---- UI state (LVGL context only) ----
static uint8_t s_selected = 0xFF;  // 0xFF = nothing requested yet
static uint32_t s_seq = 0;         // id of the latest request; older results are ignored
static bool s_loading = false;
static bool s_haveData = false;
static DataSource s_source = DataSource::None;
static PrayerData s_data;
static int s_highlighted = -1;

// ---------------------------------------------------------------------------

static lv_obj_t* makeLabel(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_label_set_text(l, text);
  return l;
}

static lv_obj_t* makePanel(lv_obj_t* parent, int x, int y, int w, int h, lv_color_t bg) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, bg, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

static void fmtHHMM(char* buf, size_t n, uint16_t minutes) {
  snprintf(buf, n, "%02u:%02u", minutes / 60, minutes % 60);
}

static bool localNow(struct tm& t) {
  time_t now = time(nullptr);
  localtime_r(&now, &t);
  return t.tm_year + 1900 >= 2024;
}

// ---------------------------------------------------------------------------
// Rendering

static void setHighlight(int idx) {
  if (idx == s_highlighted) return;
  for (int i = 0; i < PRAYER_COUNT; i++) {
    bool hi = i == idx;
    lv_obj_set_style_bg_color(s_cells[i], hi ? COL_CARD_HI : COL_CARD, 0);
    lv_obj_set_style_border_color(s_cells[i], hi ? COL_GOLD : COL_EMERALD, 0);
    lv_obj_set_style_border_width(s_cells[i], hi ? 2 : 1, 0);
    lv_obj_set_style_text_color(s_cellName[i], hi ? COL_GOLD_LIGHT : COL_MUTED, 0);
  }
  s_highlighted = idx;
}

static void renderPrayerTimes() {
  char buf[8];
  for (int i = 0; i < PRAYER_COUNT; i++) {
    if (s_haveData) fmtHHMM(buf, sizeof(buf), s_data.minutes[i]);
    lv_label_set_text(s_cellTime[i], s_haveData ? buf : "--:--");
  }
  if (s_haveData && s_data.hijriMonth >= 1 && s_data.hijriMonth <= 12) {
    lv_label_set_text_fmt(s_hijri, "%u %s %u ฮ.ศ.", s_data.hijriDay,
                          HIJRI_MONTHS[s_data.hijriMonth - 1], s_data.hijriYear);
  } else {
    lv_label_set_text(s_hijri, "");
  }
}

static void renderStatus() {
  const Province& p = PROVINCES[s_selected];
  char loc[80];  // LVGL's own printf has no %f, so format with libc
  snprintf(loc, sizeof(loc), "พิกัด %s  %.2f°N %.2f°E", p.nameTh, p.lat, p.lon);
  lv_label_set_text(s_location, loc);

  if (s_loading) {
    lv_obj_clear_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(s_status, COL_GOLD_LIGHT, 0);
    lv_label_set_text_fmt(s_status, "กำลังโหลดข้อมูล %s...", p.nameTh);
    return;
  }
  lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);

  switch (s_source) {
    case DataSource::Live:
      lv_obj_set_style_text_color(s_status, COL_OK, 0);
      lv_label_set_text_fmt(s_status, LV_SYMBOL_OK " ข้อมูลล่าสุด %s", s_data.gregDate);
      break;
    case DataSource::Cache: {
      struct tm t;
      char today[11] = "";
      if (localNow(t)) snprintf(today, sizeof(today), "%02d-%02d-%04d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
      bool stale = strcmp(today, s_data.gregDate) != 0;
      lv_obj_set_style_text_color(s_status, COL_WARN, 0);
      lv_label_set_text_fmt(s_status, LV_SYMBOL_WARNING " ออฟไลน์ - ข้อมูลสำรอง %s%s", s_data.gregDate,
                            stale ? " (เก่า)" : "");
      break;
    }
    case DataSource::None:
      lv_obj_set_style_text_color(s_status, COL_ERR, 0);
      lv_label_set_text(s_status, LV_SYMBOL_CLOSE " ไม่มีข้อมูลของจังหวัดนี้ (ออฟไลน์)");
      break;
  }
}

// Clock, date, next prayer and countdown. Runs every 250 ms, redraws once per second.
static void tickTimer(lv_timer_t*) {
  static time_t lastShown = 0;
  time_t now = time(nullptr);
  if (now == lastShown) return;
  lastShown = now;

  struct tm t;
  if (!localNow(t)) {
    lv_label_set_text(s_clock, "--:--:--");
    lv_label_set_text(s_date, "กำลังซิงค์เวลา...");
    lv_label_set_text(s_nextName, s_haveData ? "รอซิงค์นาฬิกา" : "--");
    lv_label_set_text(s_nextAt, "");
    lv_label_set_text(s_countdown, "--:--:--");
    setHighlight(-1);
    return;
  }

  lv_label_set_text_fmt(s_clock, "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
  lv_label_set_text_fmt(s_date, "%s %d %s %d", TH_DAYS[t.tm_wday], t.tm_mday, TH_MONTHS[t.tm_mon],
                        t.tm_year + 1900 + 543);  // Buddhist Era

  if (!s_haveData) {
    lv_label_set_text(s_nextName, s_loading ? "กำลังโหลด..." : "--");
    lv_label_set_text(s_nextAt, "");
    lv_label_set_text(s_countdown, "--:--:--");
    setHighlight(-1);
    return;
  }

  int32_t nowSec = t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
  int next = -1;
  int32_t remain = 0;
  for (int i = 0; i < PRAYER_COUNT; i++) {
    int32_t at = s_data.minutes[i] * 60;
    if (at > nowSec) {
      next = i;
      remain = at - nowSec;
      break;
    }
  }
  bool tomorrow = next < 0;
  if (tomorrow) {  // after Isha: next is tomorrow's Fajr (today's time used as approximation)
    next = 0;
    remain = 86400 - nowSec + s_data.minutes[0] * 60;
  }

  char hhmm[8];
  fmtHHMM(hhmm, sizeof(hhmm), s_data.minutes[next]);
  lv_label_set_text(s_nextName, PRAYER_NAMES_TH[next]);
  lv_label_set_text_fmt(s_nextAt, tomorrow ? "พรุ่งนี้ %s น." : "เวลา %s น.", hhmm);
  lv_label_set_text_fmt(s_countdown, "%02ld:%02ld:%02ld", (long)(remain / 3600), (long)(remain / 60 % 60),
                        (long)(remain % 60));
  setHighlight(next);
}

// ---------------------------------------------------------------------------
// Province selection

static void selectProvince(uint8_t province) {
  s_selected = province;
  s_seq++;
  s_loading = true;
  s_haveData = false;  // never keep showing another province's times
  s_source = DataSource::None;
  renderPrayerTimes();
  renderStatus();
  lv_obj_invalidate(lv_scr_act());
  netRequestFetch(province, s_seq);  // non-blocking hand-off to the network task
}

static void onProvinceChanged(lv_event_t* e) {
  lv_obj_t* dd = lv_event_get_target(e);
  uint16_t sel = lv_dropdown_get_selected(dd);
  if (sel >= PROVINCE_COUNT) return;
  if (sel == s_selected) return;  // same province: no new API call
  selectProvince((uint8_t)sel);
}

void uiProcessNetMessages() {
  NetMsg m;
  while (netPollMessage(m)) {
    switch (m.type) {
      case NetMsgType::WifiStatus:
        lv_obj_set_style_text_color(s_wifi, m.wifiOk ? COL_OK : COL_ERR, 0);
        break;

      case NetMsgType::Loading:
        if (m.seq != s_seq) break;
        s_loading = true;
        renderStatus();
        break;

      case NetMsgType::Result:
        if (m.seq != s_seq) break;  // result of a selection the user already changed
        s_loading = false;
        s_source = m.source;
        // Data must belong to the selected province, otherwise treat it as missing.
        s_haveData = m.source != DataSource::None && m.data.province == s_selected;
        if (!s_haveData) s_source = DataSource::None;
        if (s_haveData) s_data = m.data;
        s_highlighted = -2;  // force cell restyle
        renderPrayerTimes();
        renderStatus();
        break;
    }
  }
}

// ---------------------------------------------------------------------------
// Layout (320 x 240)

static void createTopBar(lv_obj_t* scr) {
  lv_obj_t* bar = makePanel(scr, 0, 0, 320, 40, COL_BAR);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(bar, COL_GOLD, 0);
  lv_obj_set_style_border_width(bar, 1, 0);

  s_dropdown = lv_dropdown_create(bar);
  static char options[PROVINCE_COUNT * 32];
  options[0] = '\0';
  for (uint8_t i = 0; i < PROVINCE_COUNT; i++) {
    if (i) strlcat(options, "\n", sizeof(options));
    strlcat(options, PROVINCES[i].nameTh, sizeof(options));
  }
  lv_dropdown_set_options_static(s_dropdown, options);
  lv_dropdown_set_dir(s_dropdown, LV_DIR_BOTTOM);
  lv_dropdown_set_symbol(s_dropdown, LV_SYMBOL_DOWN);
  lv_obj_set_pos(s_dropdown, 5, 4);
  lv_obj_set_size(s_dropdown, 140, 32);
  lv_obj_set_style_text_font(s_dropdown, &prompt_16, 0);
  lv_obj_set_style_text_color(s_dropdown, COL_GOLD_LIGHT, 0);
  lv_obj_set_style_bg_color(s_dropdown, COL_CARD, 0);
  lv_obj_set_style_bg_color(s_dropdown, COL_CARD_HI, LV_STATE_PRESSED);
  lv_obj_set_style_border_color(s_dropdown, COL_GOLD, 0);
  lv_obj_set_style_border_width(s_dropdown, 1, 0);
  lv_obj_set_style_radius(s_dropdown, 8, 0);
  lv_obj_set_style_pad_ver(s_dropdown, 6, 0);
  lv_obj_set_style_pad_hor(s_dropdown, 10, 0);
  lv_obj_set_ext_click_area(s_dropdown, 4);
  lv_obj_add_event_cb(s_dropdown, onProvinceChanged, LV_EVENT_VALUE_CHANGED, nullptr);

  // On open LVGL re-parents the list to the screen as its last child,
  // so it is drawn above every dashboard widget and never hidden.
  lv_obj_t* list = lv_dropdown_get_list(s_dropdown);
  lv_obj_set_style_text_font(list, &prompt_16, 0);
  lv_obj_set_style_text_line_space(list, 16, 0);  // tall rows = easy finger targets
  lv_obj_set_style_bg_color(list, COL_BAR, 0);
  lv_obj_set_style_text_color(list, COL_TEXT, 0);
  lv_obj_set_style_border_color(list, COL_GOLD, 0);
  lv_obj_set_style_border_width(list, 1, 0);
  lv_obj_set_style_radius(list, 8, 0);
  lv_obj_set_style_max_height(list, 196, 0);
  lv_obj_set_style_shadow_width(list, 16, 0);
  lv_obj_set_style_shadow_color(list, lv_color_black(), 0);
  lv_obj_set_style_shadow_opa(list, LV_OPA_60, 0);
  lv_obj_set_style_bg_color(list, COL_GOLD, LV_PART_SELECTED | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(list, COL_BAR, LV_PART_SELECTED | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(list, COL_EMERALD, LV_PART_SELECTED | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(list, COL_TEXT, LV_PART_SELECTED | LV_STATE_PRESSED);

  s_wifi = makeLabel(bar, &lv_font_montserrat_16, COL_MUTED, LV_SYMBOL_WIFI);
  lv_obj_align(s_wifi, LV_ALIGN_RIGHT_MID, -8, 0);

  s_clock = makeLabel(bar, &lv_font_montserrat_20, COL_GOLD, "--:--:--");
  lv_obj_align(s_clock, LV_ALIGN_RIGHT_MID, -34, 0);
}

static void createNextCard(lv_obj_t* scr) {
  lv_obj_t* card = makePanel(scr, 6, 64, 308, 62, COL_CARD);
  lv_obj_set_style_radius(card, 10, 0);
  lv_obj_set_style_border_color(card, COL_GOLD, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_opa(card, LV_OPA_70, 0);

  lv_obj_t* title = makeLabel(card, &prompt_14, COL_GOLD_LIGHT, "ละหมาดถัดไป");
  lv_obj_set_pos(title, 12, 5);
  s_nextName = makeLabel(card, &prompt_20, COL_TEXT, "--");
  lv_obj_set_pos(s_nextName, 12, 28);

  s_countdown = makeLabel(card, &lv_font_montserrat_28, COL_GOLD, "--:--:--");
  lv_obj_align(s_countdown, LV_ALIGN_TOP_RIGHT, -12, 4);
  s_nextAt = makeLabel(card, &prompt_14, COL_MUTED, "");
  lv_obj_align(s_nextAt, LV_ALIGN_BOTTOM_RIGHT, -12, -4);
}

static void createPrayerGrid(lv_obj_t* scr) {
  const int w = 58, h = 66, gap = 4, x0 = 7, y0 = 130;
  for (int i = 0; i < PRAYER_COUNT; i++) {
    lv_obj_t* c = makePanel(scr, x0 + i * (w + gap), y0, w, h, COL_CARD);
    lv_obj_set_style_radius(c, 8, 0);
    lv_obj_set_style_border_color(c, COL_EMERALD, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    s_cells[i] = c;

    s_cellName[i] = makeLabel(c, &prompt_14, COL_MUTED, PRAYER_NAMES_TH[i]);
    lv_obj_align(s_cellName[i], LV_ALIGN_TOP_MID, 0, 8);
    s_cellTime[i] = makeLabel(c, &lv_font_montserrat_16, COL_TEXT, "--:--");
    lv_obj_align(s_cellTime[i], LV_ALIGN_BOTTOM_MID, 0, -10);
  }
}

static void createStatusBar(lv_obj_t* scr) {
  s_spinner = lv_spinner_create(scr, 1000, 60);
  lv_obj_set_size(s_spinner, 16, 16);
  lv_obj_set_pos(s_spinner, 8, 202);
  lv_obj_set_style_arc_width(s_spinner, 3, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_spinner, 3, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_spinner, COL_CARD_HI, LV_PART_MAIN);
  lv_obj_set_style_arc_color(s_spinner, COL_GOLD, LV_PART_INDICATOR);
  lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);

  s_status = makeLabel(scr, &prompt_14, COL_MUTED, "");
  lv_obj_set_pos(s_status, 30, 199);
  lv_obj_set_width(s_status, 284);
  lv_label_set_long_mode(s_status, LV_LABEL_LONG_DOT);

  s_location = makeLabel(scr, &prompt_14, COL_MUTED, "");
  lv_obj_set_pos(s_location, 30, 219);
  lv_obj_set_width(s_location, 284);
  lv_label_set_long_mode(s_location, LV_LABEL_LONG_DOT);
}

void uiCreate(uint8_t initialProvince) {
  if (initialProvince >= PROVINCE_COUNT) initialProvince = DEFAULT_PROVINCE;

  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, COL_BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  createTopBar(scr);

  s_date = makeLabel(scr, &prompt_14, COL_TEXT, "");
  lv_obj_set_pos(s_date, 8, 43);
  s_hijri = makeLabel(scr, &prompt_14, COL_GOLD_LIGHT, "");
  lv_obj_align(s_hijri, LV_ALIGN_TOP_RIGHT, -8, 43);

  createNextCard(scr);
  createPrayerGrid(scr);
  createStatusBar(scr);

  lv_timer_create(tickTimer, 250, nullptr);

  // Restore the saved province without firing VALUE_CHANGED, then load it.
  lv_dropdown_set_selected(s_dropdown, initialProvince);
  selectProvince(initialProvince);
}
