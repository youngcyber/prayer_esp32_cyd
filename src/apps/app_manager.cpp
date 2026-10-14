// Screen switching for the mini-apps: home (prayer dashboard) -> menu -> app.
//
// The home screen is created once and never deleted, so the dashboard keeps running in
// the background. The menu and app screens are built on open and deleted on leave, so
// only one of them uses LVGL memory at a time.

#include <time.h>

#include "app.h"
#include "../net_task.h"

static constexpr uint8_t MAX_TIMERS = 8;
static constexpr lv_coord_t HEADER_H = 240 - APP_CONTENT_H;

static lv_obj_t* s_home;
static lv_obj_t* s_current;  // menu or app screen; nullptr while home is shown
static const AppDef* s_app;  // open app, nullptr on home / menu
static lv_timer_t* s_timers[MAX_TIMERS];
static uint8_t s_timerCount;

// Bumped on every screen change. HTTP results carry the token they were requested under
// and are dropped if it no longer matches (the app that asked has been closed).
static volatile uint32_t s_token = 1;
uint32_t appCurrentToken() { return s_token; }

void appHttpInit();
void appHttpPoll();

// ---------------------------------------------------------------------------
// Widget helpers

lv_obj_t* appLabel(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_label_set_text(l, text);
  return l;
}

lv_obj_t* appCard(lv_obj_t* parent, lv_coord_t w, lv_coord_t h) {
  lv_obj_t* c = lv_obj_create(parent);
  lv_obj_remove_style_all(c);
  lv_obj_set_size(c, w, h);
  lv_obj_set_style_bg_color(c, COL_CARD, 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(c, 10, 0);
  lv_obj_set_style_border_color(c, COL_GOLD, 0);
  lv_obj_set_style_border_width(c, 1, 0);
  lv_obj_set_style_border_opa(c, LV_OPA_70, 0);
  lv_obj_set_style_pad_all(c, 8, 0);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

lv_obj_t* appButton(lv_obj_t* parent, const char* text, lv_event_cb_t onClick, void* userData) {
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_set_style_bg_color(b, COL_EMERALD, 0);
  lv_obj_set_style_bg_color(b, COL_CARD_HI, LV_STATE_PRESSED);
  lv_obj_set_style_radius(b, 8, 0);
  lv_obj_set_style_pad_hor(b, 14, 0);
  lv_obj_set_style_pad_ver(b, 8, 0);
  lv_obj_set_ext_click_area(b, 4);
  if (onClick) lv_obj_add_event_cb(b, onClick, LV_EVENT_CLICKED, userData);
  lv_obj_t* l = appLabel(b, &prompt_16, COL_TEXT, text);
  lv_obj_center(l);
  return b;
}

// ---------------------------------------------------------------------------
// Context

const Province& appProvince() { return PROVINCES[netLoadSavedProvince()]; }

bool appLocalTime(struct tm& out) {
  time_t now = time(nullptr);
  localtime_r(&now, &out);
  return out.tm_year + 1900 >= 2024;
}

// ---------------------------------------------------------------------------
// Timers

lv_timer_t* appTimerCreate(lv_timer_cb_t cb, uint32_t periodMs, void* userData) {
  if (s_timerCount >= MAX_TIMERS) {
    Serial.println("[apps] appTimerCreate: too many timers");
    return nullptr;
  }
  lv_timer_t* t = lv_timer_create(cb, periodMs, userData);
  s_timers[s_timerCount++] = t;
  return t;
}

// ---------------------------------------------------------------------------
// Screen switching

static void closeCurrentApp() {
  if (s_app && s_app->destroy) s_app->destroy();
  s_app = nullptr;
  for (uint8_t i = 0; i < s_timerCount; i++) lv_timer_del(s_timers[i]);
  s_timerCount = 0;
  s_token = s_token + 1;
}

static void showScreen(lv_obj_t* scr) {
  lv_obj_t* old = s_current;
  lv_scr_load(scr);
  s_current = scr == s_home ? nullptr : scr;
  // Async: we are usually inside an event of a button that lives on `old`.
  if (old) lv_obj_del_async(old);
}

static lv_obj_t* newScreen() {
  lv_obj_t* scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, COL_BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  return scr;
}

// Header bar with a back button; returns the content area below it.
static lv_obj_t* addHeader(lv_obj_t* scr, const char* title, lv_event_cb_t onBack) {
  lv_obj_t* bar = lv_obj_create(scr);
  lv_obj_remove_style_all(bar);
  lv_obj_set_size(bar, 320, HEADER_H);
  lv_obj_set_style_bg_color(bar, COL_BAR, 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(bar, COL_GOLD, 0);
  lv_obj_set_style_border_width(bar, 1, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* back = lv_btn_create(bar);
  lv_obj_set_size(back, 56, HEADER_H - 6);
  lv_obj_align(back, LV_ALIGN_LEFT_MID, 3, 0);
  lv_obj_set_style_bg_color(back, COL_CARD, 0);
  lv_obj_set_style_bg_color(back, COL_CARD_HI, LV_STATE_PRESSED);
  lv_obj_set_style_radius(back, 8, 0);
  lv_obj_set_ext_click_area(back, 6);
  lv_obj_add_event_cb(back, onBack, LV_EVENT_CLICKED, nullptr);
  lv_obj_center(appLabel(back, &lv_font_montserrat_16, COL_GOLD, LV_SYMBOL_LEFT));

  lv_obj_t* t = appLabel(bar, &prompt_16, COL_GOLD_LIGHT, title);
  lv_obj_align(t, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t* content = lv_obj_create(scr);
  lv_obj_remove_style_all(content);
  lv_obj_set_pos(content, 0, HEADER_H);
  lv_obj_set_size(content, APP_CONTENT_W, APP_CONTENT_H);
  lv_obj_set_scroll_dir(content, LV_DIR_VER);
  return content;
}

static void onBackToHome(lv_event_t*) {
  closeCurrentApp();
  showScreen(s_home);
}

static void onBackToMenu(lv_event_t*) { appsOpenMenu(); }

static void openApp(const AppDef* app) {
  closeCurrentApp();
  lv_obj_t* scr = newScreen();
  lv_obj_t* content = addHeader(scr, app->name, onBackToMenu);
  s_app = app;
  Serial.printf("[apps] open %s\n", app->name);
  app->create(content);
  showScreen(scr);
}

static void onTileClicked(lv_event_t* e) { openApp((const AppDef*)lv_event_get_user_data(e)); }

void appsOpenMenu() {
  closeCurrentApp();
  lv_obj_t* scr = newScreen();
  lv_obj_t* content = addHeader(scr, "แอปทั้งหมด", onBackToHome);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_all(content, 8, 0);
  lv_obj_set_style_pad_gap(content, 8, 0);

  for (uint8_t i = 0; i < APP_COUNT; i++) {
    const AppDef* app = APPS[i];
    lv_obj_t* tile = lv_btn_create(content);
    lv_obj_set_size(tile, 96, 88);
    lv_obj_set_style_bg_color(tile, COL_CARD, 0);
    lv_obj_set_style_bg_color(tile, COL_CARD_HI, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(tile, COL_EMERALD, 0);
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_radius(tile, 10, 0);
    lv_obj_add_event_cb(tile, onTileClicked, LV_EVENT_CLICKED, (void*)app);

    lv_obj_t* icon = appLabel(tile, &lv_font_montserrat_28, COL_GOLD, app->symbol ? app->symbol : LV_SYMBOL_FILE);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_t* name = appLabel(tile, &prompt_14, COL_TEXT, app->name);
    lv_obj_set_width(name, 88);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, 0);
  }
  if (APP_COUNT == 0) appLabel(content, &prompt_16, COL_MUTED, "ยังไม่มีแอป เพิ่มได้ที่ src/apps/apps.cpp");

  showScreen(scr);
}

void appsInit(lv_obj_t* homeScreen) {
  s_home = homeScreen;
  appHttpInit();
}

void appsPoll() { appHttpPoll(); }
