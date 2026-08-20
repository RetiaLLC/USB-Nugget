#include "wifijoin.h"
#include <WiFi.h>
#include "../../RubberNugget.h"   // gApSsid

#define JOIN_TIMEOUT_MS 12000     // how long to wait for the STA to connect before falling back
#define RESULT_HOLD_MS   3500     // how long to show the Connected/Failed result before advancing

WifiJoinScreen::WifiJoinScreen(String ssid) {
  this->ssid = ssid;
  this->phase = 0;
  this->timeoutAt = millis() + JOIN_TIMEOUT_MS;
  this->doneAt = 0;
  this->lastAnim = 0;
  this->dots = 0;
  this->ip = "";
}

int WifiJoinScreen::update(int button) {
  this->alwaysUpdates(true);                 // poll even without a keypress
  if (button == EVENT_INIT) return SCREEN_REDRAW;

  if (button == BTN_LEFT || button == BTN_RIGHT) return SCREEN_BACK;   // user skips to the list

  if (this->phase == 0) {                    // still joining
    // Require a real DHCP lease, not just association — WL_CONNECTED can flip true a beat before the
    // IP arrives, which would show a blank/0.0.0.0 address.
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress((uint32_t)0)) {
      this->phase = 1;
      this->ip = WiFi.localIP().toString();
      this->doneAt = millis() + RESULT_HOLD_MS;
      return SCREEN_REDRAW;
    }
    if (millis() > this->timeoutAt) {        // give up -> fall back to AP-only (captive portal stays up)
      // A searching/idle STA in AP_STA wrecks the SoftAP link, so drop it cleanly on failure.
      WiFi.disconnect(false, true);
      WiFi.mode(WIFI_AP);
      this->phase = 2;
      this->doneAt = millis() + RESULT_HOLD_MS;
      return SCREEN_REDRAW;
    }
    if (millis() - this->lastAnim > 400) {   // animate the ellipsis
      this->lastAnim = millis();
      this->dots = (this->dots + 1) % 4;
      return SCREEN_REDRAW;
    }
    return SCREEN_NONE;
  }

  // phase 1 (connected) / 2 (failed): hold the result briefly, then advance to the payload list
  if (millis() > this->doneAt) return SCREEN_BACK;
  return SCREEN_NONE;
}

bool WifiJoinScreen::draw() {
  display->drawString(0, 0, "Wi-Fi");
  display->drawLine(0, 11, 127, 11);
  if (this->phase == 0) {
    String d = "";
    for (int i = 0; i < this->dots; i++) d += ".";
    display->drawString(0, 15, "Joining:");
    display->drawString(0, 28, this->ssid.length() ? this->ssid : String("(network)"));
    display->drawString(0, 44, "Connecting" + d);
  } else if (this->phase == 1) {
    display->drawString(0, 15, "Connected!");
    display->drawString(0, 28, this->ssid);
    display->drawString(0, 44, "IP " + this->ip);
  } else {
    display->drawString(0, 15, "No connection");
    display->drawString(0, 28, "Use AP:");
    display->drawString(0, 44, gApSsid.length() ? gApSsid : String("Nugget AP"));
  }
  return true;
}
