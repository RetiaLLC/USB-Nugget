#pragma once
// Central board config for the (Bad) Nugget firmware — pins, display driver, and feature flags for
// every target. Selection: the USB Nugget is the only ESP32-S2 board (auto-detected by chip); the
// Bluetooth Nugget is the S3 default; the Nibble Zero is an S3 build selected with -DNIBBLE_ZERO.
// Force any board explicitly with -DBOARD_USB_NUGGET / -DBOARD_BLUETOOTH_NUGGET / -DNIBBLE_ZERO.
#include <Arduino.h>

#if !defined(BOARD_USB_NUGGET) && !defined(BOARD_BLUETOOTH_NUGGET) && !defined(NIBBLE_ZERO) && !defined(BOARD_NEWSHEEN)
  #if defined(CONFIG_IDF_TARGET_ESP32S2)
    #define BOARD_USB_NUGGET 1
  #else
    #define BOARD_BLUETOOTH_NUGGET 1
  #endif
#endif

#if defined(BOARD_USB_NUGGET)
  // ---- USB Nugget (ESP32-S2): SH1106 OLED, 4-button d-pad (no A/B), 1 NeoPixel, NO Bluetooth ----
  #define BOARD_NAME    "USB Nugget"
  #define BOARD_HAS_BLE 0
  #define BOARD_HAS_AB  0            // d-pad only: RIGHT = select/enter, LEFT = back
  #define OLED_FLIP     1            // this SH1106 is mounted 180° (bench-confirmed upside-down)
  #include "SH1106Wire.h"
  typedef SH1106Wire NuggetDisplay;  // the S2 board uses an SH1106 controller, not SSD1306
  #define OLED_ADDR 0x3C
  #define OLED_SDA  33
  #define OLED_SCL  35
  #define NUG_BTN_COUNT 4
  #define NUG_BTN_UP    9
  #define NUG_BTN_DOWN  18
  #define NUG_BTN_LEFT  11
  #define NUG_BTN_RIGHT 7
  #define NUG_NEOPIXEL_PIN 12
  #define NUG_NEOPIXEL_CNT 1

#elif defined(NIBBLE_ZERO)
  // ---- Nibble Zero Connect (ESP32-S3-Zero): SSD1306 128x64 mounted 180°, 6 buttons, 1 NeoPixel, BLE ----
  #define BOARD_NAME    "Nibble Zero"
  #define BOARD_HAS_BLE 1
  #define BOARD_HAS_AB  1
  #define OLED_FLIP     1            // Nibble Zero OLED is mounted upside-down vs the Nugget
  #include "SSD1306Wire.h"
  typedef SSD1306Wire NuggetDisplay;
  #define OLED_ADDR 0x3C
  #define OLED_SDA  8
  #define OLED_SCL  7
  // Buttons: 6 switches SW1..SW6 on the right edge = GPIO 1,2,40,41,42,45 (official REV01 pinout).
  // U/D/L/R GPIOs match the nibble-zero-connect Meshtastic variant.h trackball EXACTLY, so a given
  // physical key behaves the same across Meshtastic and the Bad Nugget:
  //   Meshtastic: TB_UP 42 · TB_DOWN 41 · TB_LEFT 40 · TB_RIGHT 45 · BUTTON_PIN 1 · TB_PRESS 2
  // A = SW1/GPIO1 (the far-left/primary key, = Meshtastic BUTTON_PIN), B = SW2/GPIO2 (next key, = TB_PRESS).
  #define NUG_BTN_COUNT 6
  #define NUG_BTN_A     1            // SW1 — far-left/primary key (select/enter -> RIGHT)
  #define NUG_BTN_B     2            // SW2 — key right of A            (back        -> LEFT)
  #define NUG_BTN_UP    42           // SW5 = TB_UP
  #define NUG_BTN_DOWN  41           // SW4 = TB_DOWN
  #define NUG_BTN_LEFT  40           // SW3 = TB_LEFT
  #define NUG_BTN_RIGHT 45           // SW6 = TB_RIGHT — GPIO45 is a strapping pin (VDD_SPI); internal pull-up ok at runtime
  #define NUG_NEOPIXEL_PIN 21        // onboard WS2812 on the base ESP32-S3-Zero module (matches Meshtastic NEOPIXEL_DATA)
  #define NUG_NEOPIXEL_CNT 1

#elif defined(BOARD_NEWSHEEN)
  // ---- Newsheen / Pusheen puck (ESP32-S3-WROOM-1 N16R2, 16MB flash): SCREENLESS, 8x WS2812, BLE ----
  // No OLED: all visual output is the 8-pixel ring (warm-white idle -> active while a payload runs).
  // Interaction = the Wi-Fi AP / web UI + the USB MSC drive. Native USB (D-/D+ 19/20) -> BadUSB works.
  // NoopDisplay keeps the 128x64 framebuffer (the ~S phone mirror still renders) but does zero I2C.
  // ⚠ HARDWARE: the 8 WS2812B are fed through level shifter U5 whose DIR pin is strapped LOW
  //   (R21->GND) -> pixels stay DARK until U5 pin5 is bodged high; the firmware is correct regardless.
  #define BOARD_NAME    "Newsheen"
  #define BOARD_HAS_BLE 1
  #define BOARD_HAS_AB  0            // no A/B
  #define BOARD_HAS_DPAD 0           // no d-pad: only LEFT (BOOT/abort) + RIGHT (user button/select) wired
  #define BOARD_HAS_DISPLAY 0        // screenless
  #define OLED_FLIP     0
  #include "interface/lib/NoopDisplay.h"
  typedef NoopDisplay NuggetDisplay;
  #define OLED_ADDR 0x3C
  #define OLED_SDA  35               // (ignored by NoopDisplay; the puck's I2C header pins, for reference)
  #define OLED_SCL  36
  #define NUG_BTN_COUNT 2
  #define NUG_BTN_UP    100          // placeholder (no d-pad; never wired or returned by getInput)
  #define NUG_BTN_DOWN  101          // placeholder
  #define NUG_BTN_LEFT  0            // BOOT button (SW2) -> back / hold = abort
  #define NUG_BTN_RIGHT 17           // user button (SW3) -> select / run
  #define NUG_NEOPIXEL_PIN  16       // 8x WS2812B via U5 level shifter (GPIO16)
  #define NUG_NEOPIXEL_CNT  8
  #define NUG_NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800)   // WS2812B GRB — operator confirms warm-white by eye
                                                     // (the C270 webcam mis-renders warm-white-through-silicone as magenta; trust the eye)
  #define NUG_NEOPIXEL_BRIGHTNESS 230 // 90% of 255 (operator) — bright warm glow through the silicone diffuser
  #define NUG_AP_SSID "Newsheen"     // its own AP name (users interact via the AP web UI, no screen)

#else
  // ---- Bluetooth Nugget (ESP32-S3): SSD1306 OLED, 6-button d-pad + A/B, 2 NeoPixel ears, BLE ----
  #define BOARD_NAME    "Bluetooth Nugget"
  #define BOARD_HAS_BLE 1
  #define BOARD_HAS_AB  1            // A = select/enter (-> RIGHT), B = back (-> LEFT)
  #define OLED_FLIP     0
  #include "SSD1306Wire.h"
  typedef SSD1306Wire NuggetDisplay;
  #define OLED_ADDR 0x3C
  #define OLED_SDA  35
  #define OLED_SCL  36
  #define NUG_BTN_COUNT 6
  #define NUG_BTN_A     44
  #define NUG_BTN_B     43
  #define NUG_BTN_UP    13
  #define NUG_BTN_DOWN  18
  #define NUG_BTN_LEFT  11
  #define NUG_BTN_RIGHT 12
  #define NUG_NEOPIXEL_PIN 10
  #define NUG_NEOPIXEL_CNT 2
#endif

// ---- Feature-flag + NeoPixel-type defaults (a variant above may override) ----
#ifndef BOARD_HAS_DISPLAY
  #define BOARD_HAS_DISPLAY 1         // boards with a real OLED (Nugget/Nibble)
#endif
#ifndef BOARD_HAS_DPAD
  #define BOARD_HAS_DPAD 1            // boards with UP/DOWN nav keys
#endif
#ifndef NUG_NEOPIXEL_TYPE
  #define NUG_NEOPIXEL_TYPE (NEO_RGB + NEO_KHZ800)   // the Nuggets' ear pixels are wired RGB
#endif
#ifndef NUG_NEOPIXEL_BRIGHTNESS
  #define NUG_NEOPIXEL_BRIGHTNESS 8   // near-lowest visible; the Nuggets' bare ears dazzle at full
#endif
#ifndef NUG_AP_SSID
  #define NUG_AP_SSID "Nugget AP"     // default AP name (per-board override above)
#endif
