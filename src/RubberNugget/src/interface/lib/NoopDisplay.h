#pragma once
#include "SSD1306Wire.h"

// Headless display for SCREENLESS boards (e.g. the Newsheen / Pusheen puck). It is a drop-in
// NuggetDisplay: it keeps the full 128x64 framebuffer, so every existing screen still "renders"
// (dir/runner/wifi/splash), the ~S phone-app screen-mirror still works, and no UI code needs
// #ifdef'ing out — but it performs ZERO I2C, so no physical OLED is required and there is no
// per-frame I2C latency. OLEDDisplay::init() allocates the buffer before any hardware access;
// the only I2C touch points are connect()/display()/sendCommand(), which we neutralize here.
class NoopDisplay : public SSD1306Wire {
  public:
    NoopDisplay(uint8_t address, int sda = -1, int scl = -1,
                OLEDDISPLAY_GEOMETRY g = GEOMETRY_128_64)
      : SSD1306Wire(address, sda, scl, g) {}

    bool connect()                    { return true; }        // skip Wire.begin
    void display(void)                { /* never push the buffer over I2C */ }
    void sendCommand(uint8_t command) { (void)command; }      // skip init/command I2C
};
