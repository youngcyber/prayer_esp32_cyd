// Network task: Wi-Fi, NTP, AlAdhan API, NVS cache.
// The UI never blocks on HTTP; it only posts requests and drains results.
#pragma once

#include "app_types.h"

void netTaskStart();

// Called from LVGL context. Non-blocking; newer requests replace pending ones.
void netRequestFetch(uint8_t province, uint32_t seq);

// Called from LVGL context. Returns true if a message was received.
bool netPollMessage(NetMsg& out);

// Reads the saved province from NVS (DEFAULT_PROVINCE if missing/invalid).
uint8_t netLoadSavedProvince();

// Wi-Fi network: the one saved on the settings screen (NVS), else the default from secrets.h.
// Both are safe to call from LVGL context. netSetWifi() saves and makes the network task reconnect.
void netGetWifiSsid(char* out, size_t len);
void netSetWifi(const char* ssid, const char* pass);
// True from netSetWifi() until the network task has finished trying the new network.
bool netWifiBusy();

// Serialises HTTPS sessions between the prayer-times fetch and the apps' HTTP worker.
void netHttpLock();
void netHttpUnlock();
