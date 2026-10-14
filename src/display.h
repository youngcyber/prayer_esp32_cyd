// LovyanGFX driver config for ESP32-2432S028 ("Cheap Yellow Display", 2.8")
// Display: ILI9341 240x320 on HSPI.  Touch: XPT2046 resistive on its own VSPI bus.
//
// Some newer S028 units (the version with both USB-C and micro-USB) ship an ST7789
// instead of the ILI9341. If the screen stays blank or colours look inverted on such
// a unit, add  -D CYD_ST7789  to build_flags in platformio.ini.
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
#ifdef CYD_ST7789
  lgfx::Panel_ST7789   _panel;
#else
  lgfx::Panel_ILI9341  _panel;
#endif
  lgfx::Bus_SPI        _bus;
  lgfx::Light_PWM      _light;
  lgfx::Touch_XPT2046  _touch;

 public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = HSPI_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 14;
      cfg.pin_mosi = 13;
      cfg.pin_miso = 12;
      cfg.pin_dc = 2;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = 15;
      cfg.pin_rst = -1;
      cfg.pin_busy = -1;
      cfg.memory_width = 240;
      cfg.memory_height = 320;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable = true;
#ifdef CYD_ST7789
      cfg.invert = true;
#else
      cfg.invert = false;
#endif
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;  // touch has its own SPI bus
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = 21;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    {
      auto cfg = _touch.config();
      cfg.x_min = 300;
      cfg.x_max = 3900;
      cfg.y_min = 200;
      cfg.y_max = 3700;
      cfg.pin_int = -1;  // poll pressure instead of relying on the IRQ line (GPIO36)
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;  // orientation is fixed by the on-screen calibration in main.cpp
      cfg.spi_host = VSPI_HOST;
      cfg.freq = 1000000;
      cfg.pin_sclk = 25;
      cfg.pin_mosi = 32;
      cfg.pin_miso = 39;
      cfg.pin_cs = 33;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};
