# prayer_esp32_cyd

จอแสดงเวลาละหมาดสำหรับบอร์ด **ESP32-2432S028 ("Cheap Yellow Display", 2.8")** ใช้ LVGL 8 + LovyanGFX ดึงเวลาละหมาดจาก [AlAdhan API](https://aladhan.com/prayer-times-api) และแสดงผลเป็นภาษาไทย

## ความสามารถ

- แสดงเวลาละหมาด 5 เวลา (ซุบฮิ, ซุฮรี, อัศรี, มัฆริบ, อิชาอ์) พร้อมไฮไลต์เวลาถัดไป
- นับถอยหลังถึงเวลาละหมาดถัดไป
- นาฬิกา วันที่ไทย และวันที่ฮิจเราะห์
- เลือกจังหวัดได้จาก dropdown: ยะลา, ปัตตานี, นราธิวาส, สงขลา, สตูล (บันทึกค่าที่เลือกไว้ใน NVS)
- แคชข้อมูลล่าสุดของแต่ละจังหวัดไว้ใน NVS ใช้งานได้แม้ออฟไลน์
- ซิงค์เวลาด้วย NTP ถ้า NTP ถูกบล็อกจะตั้งเวลาจาก HTTP `Date` header แทน
- ปรับเทียบหน้าจอสัมผัสบนเครื่อง (ครั้งแรกที่บูต หรือเมื่อแตะจอค้างไว้ตอนเปิดเครื่อง)
- งานเครือข่ายแยกไปรันบน core 0 หน้าจอไม่ค้างระหว่างโหลดข้อมูล

## ฮาร์ดแวร์

| ส่วน | รายละเอียด |
|---|---|
| บอร์ด | ESP32-2432S028 (CYD 2.8") |
| จอ | ILI9341 240x320, SPI (HSPI) |
| ทัชสกรีน | XPT2046 resistive, SPI แยก (VSPI) |

> บางรุ่น (ที่มีทั้งพอร์ต USB-C และ micro-USB) ใช้จอ ST7789 ถ้าจอขาวหรือสีเพี้ยน ให้เปิด `-D CYD_ST7789` ใน `build_flags` ของ `platformio.ini`

## การติดตั้ง

ต้องมี [PlatformIO](https://platformio.org/) (ส่วนขยายของ VS Code หรือ CLI)

1. แก้ชื่อและรหัส Wi-Fi ใน `include/secrets.h`

   ```c
   #define WIFI_SSID     "your-wifi-ssid"
   #define WIFI_PASSWORD "your-wifi-password"
   ```

2. Build และ flash ลงบอร์ด

   ```sh
   pio run -t upload
   pio device monitor   # ดู serial log ที่ 115200
   ```

3. ครั้งแรกที่บูตจะเข้าโหมดปรับเทียบทัชสกรีน ให้แตะที่ลูกศรตามมุมจอทีละจุด

## การตั้งค่า

| ค่า | ไฟล์ | หมายเหตุ |
|---|---|---|
| `ALADHAN_METHOD` | `src/net_task.cpp` | วิธีคำนวณ: `3` = Muslim World League (ค่าเริ่มต้น), `17` = JAKIM |
| `TZ_INFO` | `src/net_task.cpp` | เขตเวลา ค่าเริ่มต้น `ICT-7` |
| `PROVINCES` | `src/app_types.h` | รายชื่อจังหวัดและพิกัด |
| `DEFAULT_PROVINCE` | `src/app_types.h` | จังหวัดเริ่มต้น (สงขลา) |
| `SCREEN_ROTATION` | `src/main.cpp` | ใช้ `3` ถ้าภาพกลับหัว |
| `upload_speed` | `platformio.ini` | ถ้าอัปโหลดไม่ผ่าน ให้ลด upload_speed เป็น `115200` |

## โครงสร้างโปรเจกต์

```
include/
  lv_conf.h            การตั้งค่า LVGL
  secrets.h            ชื่อและรหัส Wi-Fi
src/
  main.cpp             ตั้งค่าจอ/ทัช, LVGL, ปรับเทียบทัชสกรีน
  display.h            การตั้งค่า LovyanGFX สำหรับ CYD
  ui.cpp / ui.h        หน้าจอแดชบอร์ด
  net_task.cpp / .h    Wi-Fi, NTP, AlAdhan API, แคช NVS
  app_types.h          โครงสร้างข้อมูลและตารางจังหวัด
  fonts/               ฟอนต์ Prompt (ภาษาไทย) สำหรับ LVGL
```

## ไลบรารี

- [LovyanGFX](https://github.com/lovyan03/LovyanGFX) 1.1.16
- [LVGL](https://lvgl.io/) 8.3.x
- [ArduinoJson](https://arduinojson.org/) 7.x

## เครดิต

- เวลาละหมาดจาก [AlAdhan API](https://aladhan.com/)
- ฟอนต์ [Prompt](https://fonts.google.com/specimen/Prompt) ใช้สัญญาอนุญาต SIL Open Font License (ดู `src/fonts/Prompt-OFL.txt`)
