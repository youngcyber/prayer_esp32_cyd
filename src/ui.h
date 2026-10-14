// Dashboard UI. Every function here must be called from LVGL context (loop()).
#pragma once

#include <stdint.h>

void uiCreate(uint8_t initialProvince);

// Drains messages from the network task and applies them to widgets.
void uiProcessNetMessages();
