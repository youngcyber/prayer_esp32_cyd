// Built-in app: choose the Wi-Fi network and type its password on the touchscreen.
// The choice is saved in NVS (see netSetWifi) and survives reboots and re-flashing;
// include/secrets.h is only the default used until a network has been saved here.
//
// Part of the framework, not a template for student apps: it talks to the WiFi library
// directly, which AGENTS.md does not allow in normal apps.

#include <WiFi.h>

#include "app.h"
#include "../net_task.h"

static constexpr uint8_t MAX_NETWORKS = 15;
static constexpr uint32_t SCAN_TIMEOUT_MS = 15000;

static lv_obj_t* s_listPage;
static lv_obj_t* s_status;
static lv_obj_t* s_list;
static lv_obj_t* s_editPage;
static lv_obj_t* s_ssidTa;
static lv_obj_t* s_passTa;
static lv_obj_t* s_kb;

static char s_ssids[MAX_NETWORKS][33];
static bool s_scanning;
static uint32_t s_scanStart;
static bool s_connecting;
static bool s_failed;  // last save could not connect; keep the error visible
static char s_target[33];

// ---------------------------------------------------------------------------
// Status line

static void setStatus(lv_color_t color, const char* text) {
  lv_obj_set_style_text_color(s_status, color, 0);
  lv_label_set_text(s_status, text);
}

static void updateStatus() {
  bool connected = WiFi.status() == WL_CONNECTED;
  String current = WiFi.SSID();
  char buf[96];

  if (s_connecting) {
    if (!netWifiBusy()) {
      s_connecting = false;
      s_failed = !(connected && current == s_target);
    } else {
      snprintf(buf, sizeof(buf), "กำลังเชื่อมต่อ %s...", s_target);
      return setStatus(COL_WARN, buf);
    }
  }
  if (connected) s_failed = false;

  if (s_failed) {
    snprintf(buf, sizeof(buf), LV_SYMBOL_CLOSE " ต่อ %s ไม่ได้ ตรวจรหัสผ่าน", s_target);
    setStatus(COL_ERR, buf);
  } else if (connected) {
    snprintf(buf, sizeof(buf), LV_SYMBOL_OK " %s", current.c_str());
    setStatus(COL_OK, buf);
  } else {
    char saved[33];
    netGetWifiSsid(saved, sizeof(saved));
    if (saved[0]) {
      snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " ยังไม่ได้เชื่อมต่อ (%s)", saved);
    } else {
      snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " ยังไม่ได้ตั้งค่า Wi-Fi");
    }
    setStatus(COL_ERR, buf);
  }
}

// ---------------------------------------------------------------------------
// Pages

static void showEditPage(const char* ssid) {
  lv_textarea_set_text(s_ssidTa, ssid);
  lv_textarea_set_text(s_passTa, "");
  lv_textarea_set_password_mode(s_passTa, true);
  lv_obj_t* target = ssid[0] ? s_passTa : s_ssidTa;
  lv_obj_add_state(target, LV_STATE_FOCUSED);
  lv_obj_clear_state(target == s_passTa ? s_ssidTa : s_passTa, LV_STATE_FOCUSED);
  lv_keyboard_set_textarea(s_kb, target);
  lv_obj_add_flag(s_listPage, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(s_editPage, LV_OBJ_FLAG_HIDDEN);
}

static void showListPage() {
  lv_obj_add_flag(s_editPage, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(s_listPage, LV_OBJ_FLAG_HIDDEN);
}

// ---------------------------------------------------------------------------
// Scan

static void onNetworkClicked(lv_event_t* e) {
  uintptr_t i = (uintptr_t)lv_event_get_user_data(e);
  showEditPage(i < MAX_NETWORKS ? s_ssids[i] : "");
}

static lv_obj_t* addListItem(const char* symbol, const char* text, uintptr_t index) {
  lv_obj_t* b = lv_list_add_btn(s_list, symbol, text);
  lv_obj_set_style_bg_color(b, COL_CARD, 0);
  lv_obj_set_style_bg_color(b, COL_CARD_HI, LV_STATE_PRESSED);
  lv_obj_set_style_text_color(b, COL_TEXT, 0);
  lv_obj_set_style_text_font(b, &prompt_16, 0);
  lv_obj_set_style_border_color(b, COL_BAR, 0);
  lv_obj_set_style_pad_ver(b, 8, 0);
  // index > MAX_NETWORKS marks a status row ("searching...") that does nothing when tapped
  if (index <= MAX_NETWORKS) lv_obj_add_event_cb(b, onNetworkClicked, LV_EVENT_CLICKED, (void*)index);
  return b;
}

static void addManualItem() { addListItem(LV_SYMBOL_EDIT, "พิมพ์ชื่อ Wi-Fi เอง", MAX_NETWORKS); }

static const char* signalText(int rssi) {
  if (rssi >= -60) return "แรงมาก";
  if (rssi >= -70) return "แรง";
  if (rssi >= -80) return "พอใช้";
  return "อ่อน";
}

static void showScanResults(int n) {
  lv_obj_clean(s_list);
  uint8_t count = 0;
  // Strongest first; skip hidden networks and repeated names (several access points).
  bool used[64] = {};
  if (n > 64) n = 64;
  while (count < MAX_NETWORKS) {
    int best = -1;
    for (int i = 0; i < n; i++) {
      if (used[i]) continue;
      if (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best)) best = i;
    }
    if (best < 0) break;
    used[best] = true;
    String ssid = WiFi.SSID(best);
    if (ssid.isEmpty() || ssid.length() > 32) continue;
    bool dup = false;
    for (uint8_t k = 0; k < count && !dup; k++) dup = ssid == s_ssids[k];
    if (dup) continue;

    strlcpy(s_ssids[count], ssid.c_str(), sizeof(s_ssids[count]));
    char text[64];
    snprintf(text, sizeof(text), "%s  (%s%s)", s_ssids[count], signalText(WiFi.RSSI(best)),
             WiFi.encryptionType(best) == WIFI_AUTH_OPEN ? ", ไม่มีรหัส" : "");
    addListItem(LV_SYMBOL_WIFI, text, count);
    count++;
  }
  WiFi.scanDelete();
  if (count == 0) addListItem(LV_SYMBOL_WARNING, "ไม่พบ Wi-Fi", MAX_NETWORKS + 1);
  addManualItem();
}

static void startScan() {
  lv_obj_clean(s_list);
  addListItem(LV_SYMBOL_REFRESH, "กำลังค้นหา Wi-Fi...", MAX_NETWORKS + 1);
  WiFi.scanDelete();
  s_scanning = WiFi.scanNetworks(true) == WIFI_SCAN_RUNNING;
  s_scanStart = millis();
  if (!s_scanning) {
    lv_obj_clean(s_list);
    addListItem(LV_SYMBOL_WARNING, "ค้นหาไม่สำเร็จ กดค้นหาอีกครั้ง", MAX_NETWORKS + 1);
    addManualItem();
  }
}

static void pollScan() {
  if (!s_scanning) return;
  int n = WiFi.scanComplete();
  if (n >= 0) {
    s_scanning = false;
    showScanResults(n);
  } else if (n == WIFI_SCAN_FAILED || millis() - s_scanStart > SCAN_TIMEOUT_MS) {
    s_scanning = false;
    lv_obj_clean(s_list);
    addListItem(LV_SYMBOL_WARNING, "ค้นหาไม่สำเร็จ กดค้นหาอีกครั้ง", MAX_NETWORKS + 1);
    addManualItem();
  }
}

static void onScanClicked(lv_event_t*) {
  if (!s_scanning) startScan();
}

// ---------------------------------------------------------------------------
// Save

static void showError(const char* text) {
  static const char* btns[] = {"ตกลง", ""};
  lv_obj_t* box = lv_msgbox_create(nullptr, "", text, btns, false);
  lv_obj_set_width(box, 260);
  lv_obj_set_style_text_font(box, &prompt_16, 0);
  lv_obj_set_style_bg_color(box, COL_CARD, 0);
  lv_obj_set_style_border_color(box, COL_ERR, 0);
  lv_obj_set_style_border_width(box, 2, 0);
  lv_obj_add_event_cb(box, [](lv_event_t* e) { lv_msgbox_close_async(lv_event_get_current_target(e)); },
                      LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_center(box);
}

static void save() {
  const char* ssid = lv_textarea_get_text(s_ssidTa);
  const char* pass = lv_textarea_get_text(s_passTa);
  size_t passLen = strlen(pass);
  if (!ssid[0]) return showError("กรุณาใส่ชื่อ Wi-Fi");
  if (strlen(ssid) > 32) return showError("ชื่อ Wi-Fi ยาวเกินไป");
  if (passLen > 0 && (passLen < 8 || passLen > 63)) return showError("รหัสผ่านต้องยาว 8-63 ตัวอักษร");

  strlcpy(s_target, ssid, sizeof(s_target));
  netSetWifi(ssid, pass);
  s_connecting = true;
  updateStatus();
  showListPage();
}

static void onKeyboard(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY) save();       // the keyboard's OK key
  if (code == LV_EVENT_CANCEL) showListPage();  // the keyboard's close key
}

static void onTextareaFocused(lv_event_t* e) {
  lv_obj_t* ta = lv_event_get_target(e);
  lv_obj_clear_state(ta == s_ssidTa ? s_passTa : s_ssidTa, LV_STATE_FOCUSED);
  lv_keyboard_set_textarea(s_kb, ta);
}

static void onShowPassword(lv_event_t*) {
  lv_textarea_set_password_mode(s_passTa, !lv_textarea_get_password_mode(s_passTa));
}

// ---------------------------------------------------------------------------
// Layout

static lv_obj_t* makePage(lv_obj_t* parent) {
  lv_obj_t* p = lv_obj_create(parent);
  lv_obj_remove_style_all(p);
  lv_obj_set_size(p, APP_CONTENT_W, APP_CONTENT_H);
  lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  return p;
}

static lv_obj_t* makeTextarea(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, uint32_t maxLen) {
  lv_obj_t* ta = lv_textarea_create(parent);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_max_length(ta, maxLen);
  lv_obj_set_pos(ta, x, y);
  lv_obj_set_size(ta, w, 34);
  lv_obj_set_style_text_font(ta, &prompt_16, 0);
  lv_obj_set_style_text_color(ta, COL_TEXT, 0);
  lv_obj_set_style_bg_color(ta, COL_CARD, 0);
  lv_obj_set_style_border_color(ta, COL_EMERALD, 0);
  lv_obj_set_style_border_color(ta, COL_GOLD, LV_STATE_FOCUSED);
  lv_obj_set_style_pad_ver(ta, 4, 0);
  lv_obj_set_style_pad_hor(ta, 8, 0);
  lv_obj_add_event_cb(ta, onTextareaFocused, LV_EVENT_FOCUSED, nullptr);
  return ta;
}

static void createListPage(lv_obj_t* content) {
  s_listPage = makePage(content);

  s_status = appLabel(s_listPage, &prompt_14, COL_MUTED, "");
  lv_obj_set_pos(s_status, 8, 12);
  lv_obj_set_width(s_status, 200);
  lv_label_set_long_mode(s_status, LV_LABEL_LONG_DOT);

  lv_obj_t* scan = appButton(s_listPage, LV_SYMBOL_REFRESH " ค้นหา", onScanClicked);
  lv_obj_align(scan, LV_ALIGN_TOP_RIGHT, -6, 4);

  s_list = lv_list_create(s_listPage);
  lv_obj_set_pos(s_list, 6, 48);
  lv_obj_set_size(s_list, 308, APP_CONTENT_H - 52);
  lv_obj_set_style_bg_color(s_list, COL_CARD, 0);
  lv_obj_set_style_border_color(s_list, COL_EMERALD, 0);
  lv_obj_set_style_pad_all(s_list, 0, 0);
}

static void createEditPage(lv_obj_t* content) {
  s_editPage = makePage(content);
  lv_obj_add_flag(s_editPage, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* l = appLabel(s_editPage, &prompt_14, COL_GOLD_LIGHT, "ชื่อ");
  lv_obj_set_pos(l, 6, 9);
  s_ssidTa = makeTextarea(s_editPage, 44, 2, 270, 32);

  l = appLabel(s_editPage, &prompt_14, COL_GOLD_LIGHT, "รหัส");
  lv_obj_set_pos(l, 6, 45);
  s_passTa = makeTextarea(s_editPage, 44, 38, 226, 63);
  lv_textarea_set_password_mode(s_passTa, true);
  lv_textarea_set_password_show_time(s_passTa, 800);

  lv_obj_t* eye = lv_btn_create(s_editPage);
  lv_obj_set_pos(eye, 274, 38);
  lv_obj_set_size(eye, 40, 34);
  lv_obj_set_style_bg_color(eye, COL_CARD, 0);
  lv_obj_set_style_bg_color(eye, COL_CARD_HI, LV_STATE_PRESSED);
  lv_obj_add_event_cb(eye, onShowPassword, LV_EVENT_CLICKED, nullptr);
  lv_obj_center(appLabel(eye, &lv_font_montserrat_16, COL_GOLD, LV_SYMBOL_EYE_OPEN));

  s_kb = lv_keyboard_create(s_editPage);
  // lv_keyboard_create() aligns itself BOTTOM_MID, so position it with align (set_pos would be an offset from the bottom)
  lv_obj_set_size(s_kb, APP_CONTENT_W, APP_CONTENT_H - 76);
  lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_text_font(s_kb, &lv_font_montserrat_16, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(s_kb, COL_BAR, 0);
  lv_obj_set_style_pad_all(s_kb, 2, 0);
  lv_obj_set_style_pad_gap(s_kb, 3, 0);
  lv_obj_add_event_cb(s_kb, onKeyboard, LV_EVENT_ALL, nullptr);
}

static void onTimer(lv_timer_t*) {
  pollScan();
  updateStatus();
}

static void create(lv_obj_t* content) {
  s_scanning = false;
  s_connecting = false;
  s_failed = false;
  createListPage(content);
  createEditPage(content);
  updateStatus();
  startScan();
  appTimerCreate(onTimer, 500);
}

AppDef APP_WIFI = {"ตั้งค่า Wi-Fi", LV_SYMBOL_SETTINGS, create, nullptr};

