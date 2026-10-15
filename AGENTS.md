# คู่มือสำหรับ AI (อ่านก่อนแก้โค้ด)

โปรเจกต์นี้เป็นเฟิร์มแวร์ PlatformIO สำหรับบอร์ด ESP32-2432S028 ("Cheap Yellow Display") จอ 320x240 ทัชสกรีนแบบ resistive ใช้ LVGL **8.3** (ไม่ใช่ v9) + LovyanGFX + ArduinoJson 7

หน้าแรกเป็นแดชบอร์ดเวลาละหมาด ปุ่ม ☰ ที่แถบบนจะเปิด **เมนูแอป** งานของผู้ใช้ส่วนใหญ่คือ **เพิ่มแอปใหม่** ในเมนูนี้

## วิธีเพิ่มแอปใหม่

1. คัดลอก `src/apps/app_sun.cpp` เป็น `src/apps/app_<ชื่อ>.cpp` (ชื่อไฟล์ภาษาอังกฤษตัวเล็ก เช่น `app_weather.cpp`)
2. เปลี่ยนชื่อตัวแปรท้ายไฟล์เป็น `AppDef APP_<ชื่อ> = {"ชื่อไทย", LV_SYMBOL_..., create, nullptr};`
3. ใน `src/apps/apps.cpp` เพิ่ม `extern AppDef APP_<ชื่อ>;` และ `&APP_<ชื่อ>,` ในรายการ `APPS`
4. สร้าง UI ใน `create(lv_obj_t* content)` พื้นที่ `content` มีขนาด **320 x 204 px**
5. Build ด้วย `pio run` ให้ผ่านก่อนบอกว่าเสร็จ

API ทั้งหมดที่แอปใช้ได้อยู่ใน `src/apps/app.h` อ่านไฟล์นี้และ `src/apps/app_sun.cpp` ก่อนเขียนโค้ด

## ไฟล์ที่ห้ามแก้ (ยกเว้นผู้ใช้สั่งชัดเจน)

`src/main.cpp`, `src/display.h`, `src/net_task.*`, `src/ui.*`, `src/app_types.h`, `src/theme.h`, `src/apps/app.h`, `src/apps/app_manager.cpp`, `src/apps/app_http.cpp`, `src/apps/app_wifi.cpp`, `include/lv_conf.h`, `platformio.ini`, `include/secrets.h`, ไฟล์ของแอปกลุ่มอื่น

แอปหนึ่งตัวควรอยู่ในไฟล์ `.cpp` ไฟล์เดียว และแก้ `apps.cpp` แค่บรรทัดที่ลงทะเบียนแอป

## กฎสำคัญ (ถ้าไม่ทำตาม บอร์ดจะค้างหรือรีบูตเอง)

**Thread / การบล็อก**
- เรียกฟังก์ชัน LVGL (`lv_*`) ได้เฉพาะใน `create()`, event callback, callback ของ `appTimerCreate()` และ callback ของ `appHttpGet()` เท่านั้น
- ห้ามสร้าง FreeRTOS task, ห้ามใช้ `HTTPClient` / `WiFiClient` เอง ให้ใช้ `appHttpGet(url, callback)` เท่านั้น
- ห้ามใช้ `delay()`, ห้ามวนลูปรอ, ห้ามเรียก `WiFi.begin()` (เชื่อมต่อ Wi-Fi ให้อัตโนมัติแล้ว เปลี่ยนเครือข่ายได้ที่แอป "ตั้งค่า Wi-Fi")
- งานที่ต้องทำซ้ำให้ใช้ `appTimerCreate()` ห้ามใช้ `lv_timer_create()` ตรงๆ (timer จะถูกลบอัตโนมัติเมื่อออกจากแอป)

**หน่วยความจำ** (บอร์ดไม่มี PSRAM, RAM เหลือใช้ไม่มาก)
- ข้อมูลที่ดาวน์โหลดได้สูงสุด 16 KB เลือก API/พารามิเตอร์ที่ให้ข้อมูลน้อย (เช่น Open-Meteo ขอเฉพาะฟิลด์ที่ใช้)
- ประกาศ `JsonDocument` เป็นตัวแปร local ใน callback เท่านั้น ห้ามเป็น global
- ห้ามสร้าง array ขนาดใหญ่ ห้ามใช้รูปภาพ bitmap ให้ใช้ LV_SYMBOL_*, รูปทรง, `lv_bar`, `lv_arc`, `lv_chart` แทน
- อย่าเรียก API ถี่กว่าทุก 5 นาที
- ถ้าต้องบันทึกค่าไว้หลังปิดเครื่อง ใช้ `Preferences` (NVS) โดยตั้ง namespace ของแอปเอง เช่น `"tasbih"` ห้ามใช้ `"prayer"`, `"touch"` หรือ `"wifi"` และอย่าเขียนถี่ (เขียนเมื่อค่าเปลี่ยนเท่านั้น)

**ฟอนต์**
- ข้อความภาษาไทยต้องใช้ `&prompt_14`, `&prompt_16`, `&prompt_20` เท่านั้น (มีอักษรไทย, ASCII, `°` และ LV_SYMBOL_* ในตัว)
- `&lv_font_montserrat_12/14/16/20/28/40` มีแค่ภาษาอังกฤษ ตัวเลข และ LV_SYMBOL_* **ไม่มีภาษาไทยและไม่มี `°`** ใช้กับตัวเลขขนาดใหญ่ได้
- ไม่มีภาษาอาหรับ ไม่มี emoji ไม่มี `µ` `³` `²` ห้ามใช้ ถ้าต้องแสดง `°C` ให้ใช้ฟอนต์ prompt หน่วยฝุ่นให้เขียน "มคก./ลบ.ม."
- ห้ามสร้างฟอนต์ใหม่ ยกเว้นผู้ใช้ขอ

**หน้าจอและทัชสกรีน**
- ปุ่มต้องมีขนาดอย่างน้อย 40x36 px (ทัชสกรีนไม่แม่นยำ)
- ใช้สีจาก `src/theme.h` (`COL_BG`, `COL_CARD`, `COL_GOLD`, `COL_TEXT`, `COL_MUTED`, `COL_OK`, `COL_WARN`, `COL_ERR` ...) ให้ทุกแอปดูเป็นชุดเดียวกัน
- ใช้ `appCard()`, `appLabel()`, `appButton()` เพื่อให้สไตล์ตรงกัน
- ถ้าเนื้อหาสูงเกิน 204 px `content` จะเลื่อนขึ้นลงได้เอง

## รูปแบบโค้ด

- ตัวแปรและฟังก์ชันในไฟล์แอปต้องเป็น `static` และตัวแปรใช้ prefix `s_` (กันชื่อชนกับแอปอื่น)
- คอมเมนต์เป็นภาษาไทยได้ อธิบายสั้นๆ ให้นักเรียนอ่านเข้าใจ
- ทุก `appHttpGet()` ต้องแสดงสถานะ "กำลังโหลด..." และแสดง error ด้วย `appHttpErrorText(status)` เมื่อ `status != 200`
- ตรวจค่า JSON ด้วยรูปแบบ `doc["key"] | ค่าเริ่มต้น` เสมอ ห้ามสมมติว่ามีข้อมูลครบ

## API ที่แนะนำ (ไม่ต้องใช้ API key)

| ข้อมูล | URL ตัวอย่าง |
|---|---|
| สภาพอากาศปัจจุบัน | `https://api.open-meteo.com/v1/forecast?latitude=7.01&longitude=100.47&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m&timezone=Asia%2FBangkok` |
| พยากรณ์รายวัน | `https://api.open-meteo.com/v1/forecast?latitude=7.01&longitude=100.47&daily=temperature_2m_max,temperature_2m_min,weather_code&forecast_days=7&timezone=Asia%2FBangkok` |
| PM2.5 / AQI | `https://air-quality-api.open-meteo.com/v1/air-quality?latitude=7.01&longitude=100.47&current=pm2_5,pm10,us_aqi&timezone=Asia%2FBangkok` |
| วันที่ฮิจเราะห์ของวันนี้ | `https://api.aladhan.com/v1/gToH/15-10-2026` (วัน-เดือน-ปี ค.ศ.) |
| แปลงฮิจเราะห์เป็น ค.ศ. | `https://api.aladhan.com/v1/hToG/01-09-1448` (1 รอมฎอน 1448) |

- ใน `snprintf` ต้องเขียน `%` ใน URL เป็น `%%` (เช่น `Asia%%2FBangkok`)
- ใช้ `appProvince()` เพื่อเอา `lat`, `lon`, `nameTh` ของจังหวัดที่ผู้ใช้เลือกไว้
- `weather_code` ของ Open-Meteo เป็นรหัส WMO: 0 แจ่มใส, 1-3 มีเมฆ, 45/48 หมอก, 51-57 ฝนปรอย, 61-67 ฝน, 80-82 ฝนซู่, 95-99 พายุฝนฟ้าคะนอง

## การ build / ทดสอบ

```sh
pio run                 # build (ต้องผ่านทุกครั้ง)
pio run -t upload       # flash ลงบอร์ด
pio device monitor      # ดู log ที่ 115200 (บรรทัด [http] แสดงผล request แต่ละครั้ง)
```

ถ้าบอร์ดรีบูตเอง ให้ดู log หลัง `Guru Meditation Error` / `Backtrace` และตรวจกฎด้านบนก่อน
