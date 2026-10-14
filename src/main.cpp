// Islamic Prayer Times Dashboard - ESP32-2432S028 + LVGL 8.x
//
// Threading model:
//   loop()  (core 1) : the only place that touches LVGL (timers, events, widget updates)
//   netTask (core 0) : Wi-Fi, NTP, HTTPS to AlAdhan, NVS cache - never touches LVGL
//   They talk only through two FreeRTOS queues (see net_task.cpp).

#include <Arduino.h>
#include <Preferences.h>
#include <lvgl.h>

#include "display.h"
#include "fonts/fonts.h"
#include "net_task.h"
#include "ui.h"

static constexpr uint16_t SCREEN_W = 320;
static constexpr uint16_t SCREEN_H = 240;
static constexpr uint8_t SCREEN_ROTATION = 1;  // landscape; use 3 if the image is upside down
static constexpr uint8_t BACKLIGHT = 200;

static LGFX tft;
static lv_disp_draw_buf_t s_drawBuf;
static lv_color_t s_buf[SCREEN_W * 24];

static void flushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.writePixels((lgfx::rgb565_t*)&px->full, w * h);
  tft.endWrite();
  lv_disp_flush_ready(drv);
}

static void touchCb(lv_indev_drv_t*, lv_indev_data_t* data) {
  static bool wasPressed = false;
  uint16_t x, y;
  bool pressed = tft.getTouch(&x, &y);
  if (pressed) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
    if (!wasPressed) Serial.printf("[touch] %u,%u\n", x, y);
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
  wasPressed = pressed;
}

static bool fingerOnScreen() {
  uint16_t x, y;
  return tft.getTouchRaw(&x, &y) > 0;
}

// The XPT2046 range and orientation vary between units, so calibrate on the device:
// on first boot, or when a finger is held on the screen while powering up.
// Calibration data is kept in NVS so it survives reboots and re-flashing.
static void loadOrCalibrateTouch() {
  uint16_t cal[8];
  Preferences prefs;
  prefs.begin("touch", false);
  bool have = prefs.getBytesLength("cal") == sizeof(cal) && prefs.getBytes("cal", cal, sizeof(cal)) == sizeof(cal);

  bool held = false;
  if (have) {
    delay(300);
    held = fingerOnScreen();
  }

  if (have && !held) {
    tft.setTouchCalibrate(cal);
    prefs.end();
    Serial.println("[touch] calibration loaded");
    return;
  }

  // Instructions are drawn with LVGL (Thai font), then LovyanGFX draws the corner arrows on top.
  lv_obj_t* calScr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(calScr, lv_color_hex(0x062A22), 0);
  lv_obj_t* l = lv_label_create(calScr);
  lv_obj_set_style_text_font(l, &prompt_16, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(0xF2D27A), 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(l, "ปรับเทียบหน้าจอสัมผัส\nแตะที่ลูกศรตามมุมจอทีละจุด");
  lv_obj_center(l);
  lv_scr_load(calScr);
  lv_refr_now(nullptr);

  while (fingerOnScreen()) delay(20);  // wait for the boot-time finger to lift
  delay(200);
  tft.calibrateTouch(cal, (uint32_t)0xD4AF37, (uint32_t)0x062A22, 18);
  tft.setTouchCalibrate(cal);
  prefs.putBytes("cal", cal, sizeof(cal));
  prefs.end();
  Serial.println("[touch] calibration saved");

  lv_obj_t* mainScr = lv_obj_create(nullptr);
  lv_scr_load(mainScr);
  lv_obj_del(calScr);
}

void setup() {
  Serial.begin(115200);

  bool ok = tft.init();
  tft.setRotation(SCREEN_ROTATION);
  tft.setBrightness(BACKLIGHT);
  Serial.printf("[lcd] init=%d size=%dx%d\n", ok, tft.width(), tft.height());

  tft.fillScreen(TFT_BLACK);

  lv_init();
  lv_disp_draw_buf_init(&s_drawBuf, s_buf, nullptr, SCREEN_W * 24);

  static lv_disp_drv_t dispDrv;
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = SCREEN_W;
  dispDrv.ver_res = SCREEN_H;
  dispDrv.flush_cb = flushCb;
  dispDrv.draw_buf = &s_drawBuf;
  lv_disp_t* disp = lv_disp_drv_register(&dispDrv);

  static lv_indev_drv_t indevDrv;
  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = touchCb;
  lv_indev_drv_register(&indevDrv);
  Serial.println("[lvgl] display + touch registered");

  lv_theme_t* th = lv_theme_default_init(disp, lv_color_hex(0xD4AF37), lv_color_hex(0x1E8C6E), true,
                                         &lv_font_montserrat_14);
  lv_disp_set_theme(disp, th);

  loadOrCalibrateTouch();

  // Start the network task first so the initial request from uiCreate() has a queue to land in.
  netTaskStart();
  uiCreate(netLoadSavedProvince());
}

void loop() {
  uiProcessNetMessages();
  lv_timer_handler();
  delay(5);
}
