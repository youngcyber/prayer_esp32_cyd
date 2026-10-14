// Background HTTP worker for apps.
// Apps queue a URL from LVGL context; one task on core 0 does the blocking GET and hands
// the body back through a second queue, which appHttpPoll() drains in loop().

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "app.h"
#include "../net_task.h"

static constexpr size_t MAX_URL = 320;
static constexpr uint32_t WIFI_WAIT_MS = 15000;
static constexpr uint32_t HTTP_TIMEOUT_MS = 10000;

uint32_t appCurrentToken();

struct HttpJob {
  char url[MAX_URL];
  AppHttpCb cb;
  uint32_t token;
};

struct HttpDone {
  AppHttpCb cb;
  uint32_t token;
  int status;
  char* body;  // malloc'd, freed by appHttpPoll()
};

static QueueHandle_t s_jobs;
static QueueHandle_t s_done;

const char* appHttpErrorText(int status) {
  switch (status) {
    case 200: return "สำเร็จ";
    case APP_HTTP_NO_WIFI: return "ไม่ได้เชื่อมต่อ Wi-Fi";
    case APP_HTTP_TOO_LARGE: return "ข้อมูลใหญ่เกินไป";
    case APP_HTTP_NO_MEMORY: return "หน่วยความจำไม่พอ";
    case HTTPC_ERROR_CONNECTION_REFUSED: return "เชื่อมต่อเซิร์ฟเวอร์ไม่ได้";
    case HTTPC_ERROR_READ_TIMEOUT: return "เซิร์ฟเวอร์ตอบช้าเกินไป";
    case 400: return "คำขอไม่ถูกต้อง (400)";
    case 401:
    case 403: return "ไม่มีสิทธิ์ (ตรวจ API key)";
    case 404: return "ไม่พบ URL (404)";
    case 429: return "เรียกถี่เกินไป (429)";
    default: return status >= 500 ? "เซิร์ฟเวอร์มีปัญหา" : "เกิดข้อผิดพลาด";
  }
}

// Reads at most APP_HTTP_MAX_BODY bytes of the body into a new buffer.
static int readBody(HTTPClient& http, char** out) {
  int len = http.getSize();  // -1 if the server did not send Content-Length
  if (len > (int)APP_HTTP_MAX_BODY) return APP_HTTP_TOO_LARGE;
  size_t cap = len >= 0 ? (size_t)len : APP_HTTP_MAX_BODY;
  char* buf = (char*)malloc(cap + 1);
  if (!buf) return APP_HTTP_NO_MEMORY;

  WiFiClient* stream = http.getStreamPtr();
  size_t n = 0;
  uint32_t last = millis();
  while (n < cap && (http.connected() || stream->available()) && millis() - last < HTTP_TIMEOUT_MS) {
    size_t avail = stream->available();
    if (!avail) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    n += stream->readBytes(buf + n, min(avail, cap - n));
    last = millis();
  }
  if (len < 0 && n == cap && stream->available()) {
    free(buf);
    return APP_HTTP_TOO_LARGE;
  }
  buf[n] = '\0';
  *out = buf;
  return 0;
}

static int doGet(const char* url, char** body) {
  bool https = strncmp(url, "https://", 8) == 0;
  WiFiClientSecure secure;
  WiFiClient plain;
  if (https) secure.setInsecure();  // no certificate check, same as the prayer-times fetch

  HTTPClient http;
  http.useHTTP10(true);  // no chunked encoding, so the body can be read straight from the stream
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  // One TLS session at a time: two at once can exhaust the heap on a board without PSRAM.
  netHttpLock();
  int status;
  if (!(https ? http.begin(secure, url) : http.begin(plain, url))) {
    status = HTTPC_ERROR_CONNECTION_REFUSED;
  } else {
    status = http.GET();
    if (status > 0) {
      int err = readBody(http, body);
      if (err) status = err;
    }
    http.end();
  }
  netHttpUnlock();
  return status;
}

static void httpTask(void*) {
  for (;;) {
    HttpJob job;
    xQueueReceive(s_jobs, &job, portMAX_DELAY);
    if (job.token != appCurrentToken()) continue;  // app already closed: skip the request

    HttpDone done = {job.cb, job.token, 0, nullptr};
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_WAIT_MS) vTaskDelay(pdMS_TO_TICKS(250));
    done.status = WiFi.status() == WL_CONNECTED ? doGet(job.url, &done.body) : APP_HTTP_NO_WIFI;
    Serial.printf("[http] %d %s\n", done.status, job.url);
    xQueueSend(s_done, &done, portMAX_DELAY);
  }
}

void appHttpInit() {
  s_jobs = xQueueCreate(4, sizeof(HttpJob));
  s_done = xQueueCreate(4, sizeof(HttpDone));
  xTaskCreatePinnedToCore(httpTask, "apphttp", 10240, nullptr, 1, nullptr, 0);
}

bool appHttpGet(const char* url, AppHttpCb cb) {
  HttpJob job;
  if (strlcpy(job.url, url, sizeof(job.url)) >= sizeof(job.url)) {
    Serial.println("[http] URL too long");
    return false;
  }
  job.cb = cb;
  job.token = appCurrentToken();
  return xQueueSend(s_jobs, &job, 0) == pdTRUE;
}

void appHttpPoll() {
  HttpDone done;
  while (xQueueReceive(s_done, &done, 0) == pdTRUE) {
    if (done.token == appCurrentToken() && done.cb) done.cb(done.status, done.body ? done.body : "");
    free(done.body);
  }
}
