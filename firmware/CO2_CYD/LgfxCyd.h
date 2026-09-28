#pragma once
//
// LovyanGFX device description for the ESP32-2432S028R.
//
// LovyanGFX was picked over TFT_eSPI deliberately: its panel, bus, backlight
// and touch settings all live in this header, inside the sketch. TFT_eSPI
// wants the same information written into User_Setup.h *inside the installed
// library*, which is invisible to version control and silently lost whenever
// the library is reinstalled. For something meant to stay a working instrument,
// having the hardware description travel with the source matters more than the
// marginal differences between the two drivers.
//
// Pin numbers all come from pins::, i.e. from Espressif's own board variant.
//
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "BoardPins.h"

class LgfxCyd : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341   _panel;
  lgfx::Bus_SPI         _bus;
  lgfx::Light_PWM       _light;
  lgfx::Touch_XPT2046   _touch;

 public:
  LgfxCyd() {
    {  // ---- SPI bus for the panel: HSPI / SPI2, not shared with anything ----
      auto cfg = _bus.config();
      cfg.spi_host    = SPI2_HOST;
      cfg.spi_mode    = 0;
      cfg.freq_write  = 40000000;
      cfg.freq_read   = 16000000;
      cfg.spi_3wire   = false;
      cfg.use_lock    = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk    = pins::TFT_SCK;
      cfg.pin_mosi    = pins::TFT_MOSI;
      cfg.pin_miso    = pins::TFT_MISO;
      cfg.pin_dc      = pins::TFT_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }

    {  // ---------------------------------------------------------- panel ----
      auto cfg = _panel.config();
      cfg.pin_cs           = pins::TFT_CS;
      cfg.pin_rst          = pins::TFT_RST;   // -1: tied to the board reset
      cfg.pin_busy         = -1;
      cfg.panel_width      = 240;             // native portrait geometry
      cfg.panel_height     = 320;
      cfg.offset_x         = 0;
      cfg.offset_y         = 0;
      cfg.offset_rotation  = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable         = true;
      cfg.invert           = false;
      cfg.rgb_order        = false;
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = false;           // the SD card is on a different bus
      _panel.config(cfg);
    }

    {  // ------------------------------------------------------ backlight ----
      // GPIO21 is hard-wired to the backlight transistor on this board. It is
      // also the core variant's default I2C SDA, which is why our I2C bus is
      // explicitly moved to GPIO27.
      auto cfg = _light.config();
      cfg.pin_bl      = pins::TFT_BL;
      cfg.invert      = false;
      cfg.freq        = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }

    {  // ---------------------------------------------------------- touch ----
      // XPT2046 on VSPI / SPI3. The x/y limits are raw ADC bounds; they are
      // only starting values -- a calibration pass overrides them and stores
      // the result in NVS.
      auto cfg = _touch.config();
      cfg.x_min      = 300;
      cfg.x_max      = 3900;
      cfg.y_min      = 300;
      cfg.y_max      = 3900;
      cfg.pin_int    = pins::TP_IRQ;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.spi_host   = SPI3_HOST;
      cfg.freq       = 1000000;
      cfg.pin_sclk   = pins::TP_SCK;
      cfg.pin_mosi   = pins::TP_MOSI;
      cfg.pin_miso   = pins::TP_MISO;
      cfg.pin_cs     = pins::TP_CS;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }

    setPanel(&_panel);
  }

  // Re-routes the SPI3 pins back to the touch controller.
  //
  // Needed because this board wants three SPI devices but the ESP32 has only
  // two general-purpose SPI hosts. The panel owns SPI2 outright; touch and the
  // SD slot both have to live on SPI3, and they sit on *different* pins, so
  // only one of them can be routed through the peripheral at a time. The
  // logger borrows the bus for the length of a write and calls this to hand it
  // back. Everything happens in the main loop, single-threaded, so there is
  // never a read in flight while the pins are pointed elsewhere.
  bool reinitTouch() { return _panel.initTouch(); }

  // There is deliberately no "re-init the backlight" helper here. On core 3.x
  // the light driver binds GPIO21 with ledcAttach(), and calling that again on
  // a bound pin makes the core clear the binding first; one such helper shipped
  // and produced a dark screen. The invariant is: after init(), nothing touches
  // GPIO21. backlightSelfCheck() in the sketch verifies it from the GPIO matrix.
};
