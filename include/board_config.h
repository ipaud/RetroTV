#pragma once

// Pin map of the Freenove ESP32-S3 Display 2.8" (FNK0104A no touch / FNK0104B touch: same PCB).
// Every value was checked against the official repo; the source of each one is in docs/HARDWARE.md.
// Strapping pins in use: GPIO0 (BOOT), GPIO45 (backlight), GPIO46 (LCD DC).

#include <stdint.h>

// --- LCD: ILI9341V, 240x320 IPS, 4-wire SPI -------------------------------------------------
constexpr int PIN_LCD_MOSI = 11;
constexpr int PIN_LCD_SCLK = 12;
constexpr int PIN_LCD_MISO = 13;
constexpr int PIN_LCD_CS = 10;
constexpr int PIN_LCD_DC = 46;
constexpr int PIN_LCD_BL = 45;         // BSS138 low-side switch: HIGH = on, PWM capable
constexpr bool LCD_IPS_INVERT = true;  // TFT_INVERSION_ON in Freenove's TFT_eSPI setup
// LCD reset is wired to CHIP_PU (EN): there is no reset GPIO.

// --- microSD: SD_MMC 4-bit ------------------------------------------------------------------
constexpr int PIN_SD_CLK = 38;
constexpr int PIN_SD_CMD = 40;
constexpr int PIN_SD_D0 = 39;
constexpr int PIN_SD_D1 = 41;
constexpr int PIN_SD_D2 = 48;
constexpr int PIN_SD_D3 = 47;

// --- Shared I2C bus: ES8311 codec + FT6336 touch (B only) + external I2C connector ----------
constexpr int PIN_I2C_SDA = 16;
constexpr int PIN_I2C_SCL = 15;
constexpr uint32_t I2C_FREQ_HZ = 400000;
constexpr uint8_t I2C_ADDR_ES8311 = 0x18;  // CE tied low
constexpr uint8_t I2C_ADDR_TOUCH = 0x38;   // FT6336U / FT6336G

// --- Touch controller (FNK0104B only) -------------------------------------------------------
constexpr int PIN_TOUCH_RST = 18;
constexpr int PIN_TOUCH_INT = 17;
// How the controller's portrait frame maps to the screen at PAUTV_ROTATION 1
// (Freenove Sketch_12.1: x = raw.y, y = 240 - raw.x). Rotation 3 is derived in code.
constexpr bool TOUCH_SWAP_XY = true;
constexpr bool TOUCH_INVERT_X = false;
constexpr bool TOUCH_INVERT_Y = true;

// --- Audio: ES8311 codec over I2S + SC8002B amplifier ---------------------------------------
constexpr int PIN_I2S_MCLK = 4;
constexpr int PIN_I2S_BCLK = 5;
constexpr int PIN_I2S_WS = 7;
constexpr int PIN_I2S_DOUT = 8;  // ESP32 -> codec (DSDIN)
constexpr int PIN_I2S_DIN = 6;   // codec (ASDOUT) -> ESP32, microphone path
constexpr int PIN_AMP_EN = 1;    // amplifier SHUTDOWN is active high: LOW = amplifier on

// --- Misc on-board -------------------------------------------------------------------------
constexpr int PIN_RGB_LED = 42;  // WS2812
constexpr int PIN_BOOT_BTN = 0;  // 10K pull-up on board, LOW when pressed
constexpr int PIN_BAT_ADC = 9;   // 200K/200K divider: battery mV = ADC mV * 2
constexpr int BAT_ADC_DIVIDER = 2;

// --- Front of the tele90 case --------------------------------------------------------------
// Four 6x6 mm push buttons to GND, left to right CH-, CH+, VOL-, VOL+, on the four expansion
// connector pins (no external pull-ups: INPUT_PULLUP). GPIO3 is a strapping pin (JTAG source):
// a key held at power-on changes nothing the firmware uses.
constexpr int PIN_KEY_CH_DOWN = 2;
constexpr int PIN_KEY_CH_UP = 3;
constexpr int PIN_KEY_VOL_DOWN = 14;
constexpr int PIN_KEY_VOL_UP = 21;
// 3 mm LED + ~1K resistor to GND on the UART connector's TXD (GPIO43, free: the log goes over
// USB). HIGH = on. The ROM bootloader writes to UART0 at power-on: it flickers then.
constexpr int PIN_LED_FRONT = 43;
