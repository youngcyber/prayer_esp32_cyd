#include "net_task.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <sys/time.h>
#include <time.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Missing include/secrets.h - copy include/secrets.example.h to include/secrets.h and set your Wi-Fi credentials"
#endif

// AlAdhan calculation method: 3 = Muslim World League, 17 = JAKIM (Malaysia).
static constexpr int ALADHAN_METHOD = 3;
static constexpr const char* TZ_INFO = "ICT-7";  // Asia/Bangkok, no DST
static constexpr const char* NVS_NAMESPACE = "prayer";

static constexpr uint32_t WIFI_TIMEOUT_MS = 15000;
static constexpr uint32_t NTP_TIMEOUT_MS = 15000;
static constexpr uint32_t HTTP_TIMEOUT_MS = 10000;
static constexpr uint32_t RETRY_INTERVAL_MS = 60000;
static constexpr uint32_t MAINTENANCE_PERIOD_MS = 30000;

static QueueHandle_t s_reqQueue;  // length 1, overwritten -> latest selection wins
static QueueHandle_t s_msgQueue;
static SemaphoreHandle_t s_httpMutex;

// ---- network-task-only state ----
static uint8_t s_currentProvince = DEFAULT_PROVINCE;
static uint32_t s_currentSeq = 0;
static bool s_lastFetchOk = false;
static int s_lastFetchYday = -1;     // day-of-year of the last successful live fetch
static uint32_t s_lastAttemptMs = 0;
static bool s_wifiReported = false;
static bool s_wifiLastState = false;
static bool s_ntpStarted = false;
static NetMsg s_lastResult;          // re-sent when a duplicate request needs no new fetch

// ---------------------------------------------------------------------------
// NVS helpers (each call opens its own handle; NVS itself is thread-safe)

uint8_t netLoadSavedProvince() {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, true);
  uint8_t p = prefs.getUChar("prov", DEFAULT_PROVINCE);
  prefs.end();
  return p < PROVINCE_COUNT ? p : DEFAULT_PROVINCE;
}

static void saveProvince(uint8_t province) {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  if (prefs.getUChar("prov", 0xFF) != province) prefs.putUChar("prov", province);
  prefs.end();
}

static void cacheKey(uint8_t province, char* key) { snprintf(key, 8, "c%u", province); }

static void saveCache(const PrayerData& d) {
  char key[8];
  cacheKey(d.province, key);
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putBytes(key, &d, sizeof(d));
  prefs.end();
}

// Only returns data that was cached for exactly this province.
static bool loadCache(uint8_t province, PrayerData& d) {
  char key[8];
  cacheKey(province, key);
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, true);
  size_t n = prefs.getBytesLength(key) == sizeof(d) ? prefs.getBytes(key, &d, sizeof(d)) : 0;
  prefs.end();
  return n == sizeof(d) && d.magic == CACHE_MAGIC && d.province == province;
}

// ---------------------------------------------------------------------------

static void postMsg(const NetMsg& m) { xQueueSend(s_msgQueue, &m, pdMS_TO_TICKS(100)); }

static void postWifiStatus(bool ok) {
  if (s_wifiReported && s_wifiLastState == ok) return;
  s_wifiReported = true;
  s_wifiLastState = ok;
  NetMsg m = {};
  m.type = NetMsgType::WifiStatus;
  m.wifiOk = ok;
  postMsg(m);
}

static void startNtpOnce() {
  if (s_ntpStarted) return;
  s_ntpStarted = true;
  configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
}

static bool ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    startNtpOnce();
    postWifiStatus(true);
    return true;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
    vTaskDelay(pdMS_TO_TICKS(250));
  }
  bool ok = WiFi.status() == WL_CONNECTED;
  postWifiStatus(ok);
  if (ok) startNtpOnce();
  return ok;
}

static bool timeValid(struct tm& t) {
  time_t now = time(nullptr);
  localtime_r(&now, &t);
  return t.tm_year + 1900 >= 2024;
}

static bool waitForTime(struct tm& t) {
  uint32_t start = millis();
  while (!timeValid(t) && millis() - start < NTP_TIMEOUT_MS) vTaskDelay(pdMS_TO_TICKS(200));
  return timeValid(t);
}

// Fallback when NTP is blocked: set the clock from an HTTP "Date" header,
// e.g. "Wed, 15 Oct 2026 03:04:05 GMT". Accuracy is about a second.
static bool setClockFromHttpDate(const String& hdr) {
  static const char* const MONTHS = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4] = "";
  int d, y, hh, mm, ss;
  if (sscanf(hdr.c_str(), "%*3s, %d %3s %d %d:%d:%d", &d, mon, &y, &hh, &mm, &ss) != 6) return false;
  const char* hit = strstr(MONTHS, mon);
  if (!hit || y < 2024) return false;
  int m = (hit - MONTHS) / 3 + 1;
  // days since 1970-01-01 (civil-from-days inverse, valid for the Gregorian calendar)
  int yy = y - (m <= 2);
  int era = yy / 400;
  int yoe = yy - era * 400;
  int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = era * 146097L + doe - 719468L;
  struct timeval tv = {(time_t)(days * 86400L + hh * 3600L + mm * 60L + ss), 0};
  settimeofday(&tv, nullptr);
  Serial.printf("[net] clock set from HTTP Date: %s\n", hdr.c_str());
  return true;
}

static bool parseHHMM(const char* s, uint16_t& minutes) {
  int h, m;
  if (!s || sscanf(s, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) return false;
  minutes = h * 60 + m;
  return true;
}

static bool fetchFromApi(uint8_t province, PrayerData& out, char* err, size_t errLen) {
  struct tm t;
  bool haveTime = waitForTime(t);

  const Province& p = PROVINCES[province];
  char url[200];
  if (haveTime) {
    snprintf(url, sizeof(url),
             "https://api.aladhan.com/v1/timings/%02d-%02d-%04d?latitude=%.4f&longitude=%.4f"
             "&method=%d&timezonestring=Asia/Bangkok",
             t.tm_mday, t.tm_mon + 1, t.tm_year + 1900, p.lat, p.lon, ALADHAN_METHOD);
  } else {
    // No NTP: let the server pick "today" in Asia/Bangkok and take the clock from its Date header.
    snprintf(url, sizeof(url),
             "https://api.aladhan.com/v1/timings?latitude=%.4f&longitude=%.4f"
             "&method=%d&timezonestring=Asia/Bangkok",
             p.lat, p.lon, ALADHAN_METHOD);
  }

  WiFiClientSecure client;
  client.setInsecure();  // no CA pinning; swap for setCACert() if you need server authentication
  HTTPClient http;
  http.useHTTP10(true);  // avoid chunked encoding so the JSON can be parsed from the stream
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(client, url)) {
    snprintf(err, errLen, "HTTP begin failed");
    return false;
  }

  const char* headerKeys[] = {"Date"};
  http.collectHeaders(headerKeys, 1);

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    snprintf(err, errLen, code < 0 ? "Network error %d" : "API HTTP %d", code);
    http.end();
    return false;
  }
  if (!haveTime && !setClockFromHttpDate(http.header("Date"))) {
    snprintf(err, errLen, "Clock not synced");
    http.end();
    return false;
  }

  JsonDocument filter;
  filter["data"]["timings"]["Fajr"] = true;
  filter["data"]["timings"]["Dhuhr"] = true;
  filter["data"]["timings"]["Asr"] = true;
  filter["data"]["timings"]["Maghrib"] = true;
  filter["data"]["timings"]["Isha"] = true;
  filter["data"]["date"]["gregorian"]["date"] = true;
  filter["data"]["date"]["hijri"]["day"] = true;
  filter["data"]["date"]["hijri"]["year"] = true;
  filter["data"]["date"]["hijri"]["month"]["number"] = true;

  JsonDocument doc;
  DeserializationError jerr =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (jerr) {
    snprintf(err, errLen, "JSON: %s", jerr.c_str());
    return false;
  }

  JsonObject timings = doc["data"]["timings"];
  JsonObject date = doc["data"]["date"];
  out = {};
  out.magic = CACHE_MAGIC;
  out.province = province;
  for (uint8_t i = 0; i < PRAYER_COUNT; i++) {
    if (!parseHHMM(timings[PRAYER_NAMES[i]], out.minutes[i])) {
      snprintf(err, errLen, "Bad time for %s", PRAYER_NAMES[i]);
      return false;
    }
  }
  strlcpy(out.gregDate, date["gregorian"]["date"] | "", sizeof(out.gregDate));
  out.hijriDay = atoi(date["hijri"]["day"] | "0");
  out.hijriYear = atoi(date["hijri"]["year"] | "0");
  out.hijriMonth = date["hijri"]["month"]["number"] | 0;
  return true;
}

static void doFetch(uint8_t province, uint32_t seq) {
  s_lastAttemptMs = millis();

  NetMsg loading = {};
  loading.type = NetMsgType::Loading;
  loading.seq = seq;
  postMsg(loading);

  NetMsg res = {};
  res.type = NetMsgType::Result;
  res.seq = seq;

  bool ok = false;
  if (ensureWifi()) {
    netHttpLock();
    ok = fetchFromApi(province, res.data, res.error, sizeof(res.error));
    netHttpUnlock();
  } else {
    snprintf(res.error, sizeof(res.error), "Wi-Fi not connected");
  }

  if (ok) {
    res.source = DataSource::Live;
    saveCache(res.data);
    Serial.printf("[net] %s %s: Fajr %02u:%02u ... Isha %02u:%02u\n", PROVINCES[province].nameEn,
                  res.data.gregDate, res.data.minutes[0] / 60, res.data.minutes[0] % 60,
                  res.data.minutes[4] / 60, res.data.minutes[4] % 60);
    struct tm t;
    s_lastFetchYday = timeValid(t) ? t.tm_yday : -1;
  } else {
    res.source = loadCache(province, res.data) ? DataSource::Cache : DataSource::None;
    Serial.printf("[net] fetch failed for %s: %s\n", PROVINCES[province].nameEn, res.error);
  }
  s_lastFetchOk = ok;
  s_lastResult = res;
  postMsg(res);  // the UI ignores it if a newer selection was made meanwhile
}

static void netTask(void*) {
  for (;;) {
    FetchRequest req;
    if (xQueueReceive(s_reqQueue, &req, pdMS_TO_TICKS(MAINTENANCE_PERIOD_MS)) == pdTRUE) {
      if (req.province >= PROVINCE_COUNT) continue;
      struct tm t;
      bool sameAsLoaded = req.province == s_currentProvince && s_lastFetchOk &&
                          timeValid(t) && t.tm_yday == s_lastFetchYday;
      s_currentProvince = req.province;
      s_currentSeq = req.seq;
      saveProvince(req.province);
      if (sameAsLoaded) {  // today's data is already fetched: no API call, just re-deliver it
        s_lastResult.seq = req.seq;
        postMsg(s_lastResult);
        continue;
      }
      doFetch(req.province, req.seq);
      continue;
    }

    // Periodic maintenance: Wi-Fi state, daily refresh after midnight, retry after failure.
    postWifiStatus(WiFi.status() == WL_CONNECTED);
    struct tm t;
    bool newDay = s_lastFetchOk && timeValid(t) && t.tm_yday != s_lastFetchYday;
    bool retry = !s_lastFetchOk && millis() - s_lastAttemptMs >= RETRY_INTERVAL_MS;
    if (newDay || retry) doFetch(s_currentProvince, s_currentSeq);
  }
}

void netTaskStart() {
  s_reqQueue = xQueueCreate(1, sizeof(FetchRequest));
  s_msgQueue = xQueueCreate(6, sizeof(NetMsg));
  s_httpMutex = xSemaphoreCreateMutex();
  // Core 0 alongside the Wi-Fi stack; LVGL runs in loop() on core 1.
  xTaskCreatePinnedToCore(netTask, "net", 12288, nullptr, 1, nullptr, 0);
}

void netRequestFetch(uint8_t province, uint32_t seq) {
  FetchRequest req = {province, seq};
  xQueueOverwrite(s_reqQueue, &req);
}

bool netPollMessage(NetMsg& out) { return xQueueReceive(s_msgQueue, &out, 0) == pdTRUE; }

void netHttpLock() { xSemaphoreTake(s_httpMutex, portMAX_DELAY); }

void netHttpUnlock() { xSemaphoreGive(s_httpMutex); }
