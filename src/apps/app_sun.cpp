// ตัวอย่างแอป: เวลาดวงอาทิตย์ขึ้น-ตก ของจังหวัดที่เลือกไว้ในหน้าเวลาละหมาด
// ข้อมูลจาก Open-Meteo (ฟรี ไม่ต้องใช้ API key)
//
// ใช้ไฟล์นี้เป็นแม่แบบของแอปใหม่:
//   1. คัดลอกเป็น src/apps/app_<ชื่อ>.cpp แล้วเปลี่ยน APP_SUN เป็นชื่อใหม่
//   2. เพิ่มชื่อแอปใน src/apps/apps.cpp
// กฎที่ต้องทำตามอยู่ใน AGENTS.md

#include <ArduinoJson.h>

#include "app.h"

static constexpr uint32_t REFRESH_MS = 30 * 60 * 1000;  // โหลดใหม่ทุก 30 นาที

// Widget ของแอปนี้ (ถูกลบอัตโนมัติเมื่อออกจากแอป และสร้างใหม่ทุกครั้งที่เปิด)
static lv_obj_t* s_sunrise;
static lv_obj_t* s_sunset;
static lv_obj_t* s_status;

// เรียกเมื่อโหลดข้อมูลเสร็จ (อยู่ใน LVGL context แก้ widget ได้เลย)
static void onData(int status, const char* body) {
  if (status != 200) {
    lv_label_set_text_fmt(s_status, LV_SYMBOL_WARNING " โหลดไม่สำเร็จ: %s", appHttpErrorText(status));
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    lv_label_set_text(s_status, LV_SYMBOL_WARNING " อ่านข้อมูลไม่ได้");
    return;
  }

  // รูปแบบข้อมูล: "daily": {"sunrise": ["2026-10-15T06:04"], "sunset": ["2026-10-15T18:03"]}
  const char* rise = doc["daily"]["sunrise"][0] | "";
  const char* set = doc["daily"]["sunset"][0] | "";
  lv_label_set_text(s_sunrise, strlen(rise) >= 16 ? rise + 11 : "--:--");  // ตัดเหลือ "06:04"
  lv_label_set_text(s_sunset, strlen(set) >= 16 ? set + 11 : "--:--");

  struct tm t;
  if (appLocalTime(t)) {
    lv_label_set_text_fmt(s_status, LV_SYMBOL_OK " อัปเดตเมื่อ %02d:%02d น.", t.tm_hour, t.tm_min);
  } else {
    lv_label_set_text(s_status, LV_SYMBOL_OK " อัปเดตแล้ว");
  }
}

static void load() {
  const Province& p = appProvince();
  char url[200];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f"
           "&daily=sunrise,sunset&forecast_days=1&timezone=Asia%%2FBangkok",
           p.lat, p.lon);
  lv_label_set_text(s_status, "กำลังโหลด...");
  appHttpGet(url, onData);
}

static void onRefresh(lv_event_t*) { load(); }
static void onTimer(lv_timer_t*) { load(); }

// สร้างการ์ดแสดงเวลา 1 ใบ คืนค่า label ตัวเลขไว้อัปเดตทีหลัง
static lv_obj_t* makeTimeCard(lv_obj_t* parent, lv_coord_t x, const char* title) {
  lv_obj_t* card = appCard(parent, 148, 96);
  lv_obj_set_pos(card, x, 8);
  lv_obj_t* t = appLabel(card, &prompt_14, COL_GOLD_LIGHT, title);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_t* value = appLabel(card, &lv_font_montserrat_40, COL_TEXT, "--:--");
  lv_obj_align(value, LV_ALIGN_BOTTOM_MID, 0, 0);
  return value;
}

// content มีขนาด 320 x 204 px
static void create(lv_obj_t* content) {
  s_sunrise = makeTimeCard(content, 8, "ดวงอาทิตย์ขึ้น");
  s_sunset = makeTimeCard(content, 164, "ดวงอาทิตย์ตก");

  lv_obj_t* place = appLabel(content, &prompt_16, COL_TEXT, "");
  lv_label_set_text_fmt(place, LV_SYMBOL_GPS " จ.%s", appProvince().nameTh);
  lv_obj_set_pos(place, 12, 116);

  s_status = appLabel(content, &prompt_14, COL_MUTED, "");
  lv_obj_set_pos(s_status, 12, 142);

  lv_obj_t* btn = appButton(content, LV_SYMBOL_REFRESH " โหลดใหม่", onRefresh);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, -8, -8);

  appTimerCreate(onTimer, REFRESH_MS);
  load();
}

AppDef APP_SUN = {"ดวงอาทิตย์", LV_SYMBOL_IMAGE, create, nullptr};
