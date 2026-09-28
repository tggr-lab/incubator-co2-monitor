#pragma once
//
// ESP32-2432S028R ("CYD") pin map.
//
// Every value here is copied from the board variant that ships inside the
// Espressif Arduino core:
//
//   ~/.arduino15/packages/esp32/hardware/esp32/3.3.11/variants/jczn_2432s028r/pins_arduino.h
//
// That file is the authority for this board -- it is maintained by Espressif
// for this exact part number, so it beats any pinout diagram found online.
// The names below are re-exported with project-local spellings so the rest of
// the firmware never has to care which core version defined them.
//
// Before flashing, consider saving what your board shipped with:
//   esptool --chip esp32 read-flash 0 0x400000 factory.bin
//
#include <Arduino.h>

namespace pins {

// ---------------------------------------------------------------- display --
// ILI9341, 240x320 native, driven landscape. Lives on HSPI/SPI2, which is
// entirely separate from the VSPI/SPI3 bus used by touch and the SD slot, so
// the panel never has to arbitrate with anything else.
constexpr int8_t TFT_SCK  = 14;
constexpr int8_t TFT_MOSI = 13;
constexpr int8_t TFT_MISO = 12;  // also the MTDI strapping pin -- see note below
constexpr int8_t TFT_CS   = 15;
constexpr int8_t TFT_DC   = 2;
constexpr int8_t TFT_RST  = -1;  // tied to the ESP32 reset line on this board

// GPIO12 is MTDI, which selects the internal flash regulator voltage at reset.
// It is safe as a SPI MISO because the panel only drives it after boot, but
// nothing may hold it high while the board comes out of reset.

// Backlight. NOT a general purpose pin on this board, and notably the core
// variant also declares GPIO21 as the default I2C SDA -- see the I2C note.
constexpr int8_t TFT_BL = 21;

// ------------------------------------------------------------------ touch --
// XPT2046 resistive controller on VSPI/SPI3, on its own set of pins rather
// than the bus defaults, so it does not collide with the SD card.
constexpr int8_t TP_SCK  = 25;
constexpr int8_t TP_MOSI = 32;
constexpr int8_t TP_MISO = 39;  // input-only pin
constexpr int8_t TP_CS   = 33;
constexpr int8_t TP_IRQ  = 36;  // input-only pin

// --------------------------------------------------------------- SD card --
// Shares the VSPI/SPI3 peripheral with touch but uses the bus default pins.
// Optional: the instrument runs fine with the slot empty.
constexpr int8_t SD_SCK  = 18;
constexpr int8_t SD_MOSI = 23;
constexpr int8_t SD_MISO = 19;
constexpr int8_t SD_CS   = 5;

// -------------------------------------------------------- our own sensors --
// MH-Z16 CO2, PWM output. GPIO35 is input-only, which is exactly what a pulse
// width measurement wants and is why it was chosen: it costs us nothing else.
// Never configure this as an output -- the silicon has no output driver here.
constexpr int8_t CO2_PWM = 35;

// DS18B20 temperature probe, 1-Wire on GPIO27 (header CN1). One pin is the
// whole point: I2C would need GPIO22 as well, and GPIO22 is unreachable through
// this board's CN1 connector -- the contact never grips a wire, measured as an
// open line. GPIO22 is therefore deliberately unused by this firmware.
constexpr int8_t ONEWIRE_DATA = 27;

// ------------------------------------------------------ onboard extras -----
// Common-anode RGB LED: each channel is active LOW.
constexpr int8_t LED_R = 4;
constexpr int8_t LED_G = 16;
constexpr int8_t LED_B = 17;

constexpr int8_t AUDIO_OUT   = 26;  // amplifier input, via the onboard speaker pad
constexpr int8_t LDR         = 34;  // ambient light sensor, input-only
constexpr int8_t USER_BUTTON = 0;   // also the BOOT strapping pin

// -------------------------------------------------- exposed header pins ----
// CN1: 3V3, GPIO27, GPIO22, GND     -> GPIO27 = DS18B20 data; GPIO22 unused (dead contact)
// P3 : GPIO21, GPIO22, GPIO35, GND  -> GPIO35 used for MH-Z16 PWM
//      (GPIO21 on P3 is the backlight and must be left alone)

}  // namespace pins
