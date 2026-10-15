// รายชื่อแอปที่แสดงในเมนู (เรียงตามลำดับในรายการ)
//
// เพิ่มแอปใหม่ 2 ขั้นตอน:
//   1. ประกาศ  extern AppDef APP_<ชื่อ>;  ไว้ด้านล่าง
//   2. ใส่  &APP_<ชื่อ>,  ในรายการ APPS

#include "app.h"

extern AppDef APP_WIFI;  // built-in settings screen, keep it first
extern AppDef APP_SUN;

const AppDef* const APPS[] = {
    &APP_WIFI,
    &APP_SUN,
};

const uint8_t APP_COUNT = sizeof(APPS) / sizeof(APPS[0]);
