// Mini-app framework: everything an app needs comes from this one header.
//
// An app is one .cpp file in src/apps/ that defines an AppDef and is listed in apps.cpp.
// Start from app_sun.cpp. The rules (threads, memory, fonts) are in AGENTS.md.
//
// Every function here must be called from LVGL context: an app's create(), an LVGL
// event callback, an appTimerCreate() callback or an appHttpGet() callback.
#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include "../app_types.h"
#include "../fonts/fonts.h"
#include "../theme.h"

// ---------------------------------------------------------------------------
// App definition

struct AppDef {
  const char* name;    // Thai name shown in the menu and the header, e.g. "สภาพอากาศ"
  const char* symbol;  // menu icon, one of LVGL's LV_SYMBOL_* (e.g. LV_SYMBOL_GPS)
  // Build the UI inside `content` (320 x 204 px, below the header). Called each time the app opens.
  void (*create)(lv_obj_t* content);
  // Optional (may be nullptr): called just before the app's screen is deleted.
  // Widgets, appTimerCreate() timers and pending appHttpGet() callbacks are cleaned up automatically.
  void (*destroy)();
};

// Registry, defined in apps.cpp
extern const AppDef* const APPS[];
extern const uint8_t APP_COUNT;

// Size of the `content` area passed to create()
static constexpr lv_coord_t APP_CONTENT_W = 320;
static constexpr lv_coord_t APP_CONTENT_H = 204;

// ---------------------------------------------------------------------------
// Timers: use this instead of lv_timer_create(). Deleted automatically when the app closes.
// Returns nullptr if the app already has 8 timers.
lv_timer_t* appTimerCreate(lv_timer_cb_t cb, uint32_t periodMs, void* userData = nullptr);

// ---------------------------------------------------------------------------
// HTTP: non-blocking GET (http:// or https://). The request runs on a background task;
// `cb` is called later from LVGL context, so it may update widgets directly.
// The callback is dropped if the user leaves the app before the response arrives.
//
//   status: HTTP status (200 = OK), or a negative value on failure (see appHttpErrorText)
//   body:   response text ("" on failure), at most APP_HTTP_MAX_BODY bytes, freed after cb returns
//
// Returns false if the request could not be queued (URL too long or 4 requests already pending).
typedef void (*AppHttpCb)(int status, const char* body);
bool appHttpGet(const char* url, AppHttpCb cb);

static constexpr size_t APP_HTTP_MAX_BODY = 16 * 1024;
static constexpr int APP_HTTP_NO_WIFI = -100;
static constexpr int APP_HTTP_TOO_LARGE = -101;
static constexpr int APP_HTTP_NO_MEMORY = -102;

// Short Thai description of a status code, for showing errors on screen.
const char* appHttpErrorText(int status);

// ---------------------------------------------------------------------------
// Context

// Province selected on the prayer-times dashboard (name + lat/lon), handy for location-based APIs.
const Province& appProvince();

// Current local time. Returns false until the clock has been synced from the internet.
bool appLocalTime(struct tm& out);

// ---------------------------------------------------------------------------
// Widget helpers in the shared theme

lv_obj_t* appLabel(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text);
// Rounded card with a gold border. Position it with lv_obj_set_pos() / lv_obj_align().
lv_obj_t* appCard(lv_obj_t* parent, lv_coord_t w, lv_coord_t h);
// Button with Thai text (prompt_16). `onClick` receives LV_EVENT_CLICKED; userData via lv_event_get_user_data().
lv_obj_t* appButton(lv_obj_t* parent, const char* text, lv_event_cb_t onClick, void* userData = nullptr);

// ---------------------------------------------------------------------------
// Framework internals (called by main.cpp / ui.cpp, not by apps)

void appsInit(lv_obj_t* homeScreen);
void appsOpenMenu();
void appsPoll();  // call from loop()
