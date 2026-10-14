// Shared data types and the province table.
// These structs are POD so they can be copied through FreeRTOS queues and stored in NVS.
#pragma once

#include <Arduino.h>

struct Province {
  const char* nameEn;
  const char* nameTh;
  float lat;
  float lon;
};

// Representative coordinates per province (not per district).
static const Province PROVINCES[] = {
    {"Yala",       "ยะลา",     6.54f, 101.28f},
    {"Pattani",    "ปัตตานี",   6.87f, 101.25f},
    {"Narathiwat", "นราธิวาส",  6.43f, 101.82f},
    {"Songkhla",   "สงขลา",    7.01f, 100.47f},  // Hat Yai reference point
    {"Satun",      "สตูล",     6.62f, 100.07f},
};
static constexpr uint8_t PROVINCE_COUNT = sizeof(PROVINCES) / sizeof(PROVINCES[0]);
static constexpr uint8_t DEFAULT_PROVINCE = 3;  // Songkhla

static constexpr uint8_t PRAYER_COUNT = 5;
static const char* const PRAYER_NAMES[PRAYER_COUNT] = {"Fajr", "Dhuhr", "Asr", "Maghrib", "Isha"};  // API keys
static const char* const PRAYER_NAMES_TH[PRAYER_COUNT] = {"ซุบฮิ", "ซุฮรี", "อัศรี", "มัฆริบ", "อิชาอ์"};

static constexpr uint32_t CACHE_MAGIC = 0x50524159;  // "PRAY", bump if PrayerData layout changes

struct PrayerData {
  uint32_t magic;
  uint8_t province;
  uint8_t hijriDay;
  uint8_t hijriMonth;  // 1..12
  uint16_t hijriYear;
  uint16_t minutes[PRAYER_COUNT];  // minutes since local midnight
  char gregDate[11];               // "DD-MM-YYYY" as returned by AlAdhan
};

// UI -> network task
struct FetchRequest {
  uint8_t province;
  uint32_t seq;
};

enum class DataSource : uint8_t { Live, Cache, None };
enum class NetMsgType : uint8_t { WifiStatus, Loading, Result };

// network task -> UI
struct NetMsg {
  NetMsgType type;
  uint32_t seq;
  bool wifiOk;
  DataSource source;
  PrayerData data;
  char error[48];
};
