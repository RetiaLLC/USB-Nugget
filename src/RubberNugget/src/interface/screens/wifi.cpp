#include "wifi.h"
#include <WiFi.h>
#include "../../RubberNugget.h"
#include "../../board_config.h"   // BOARD_HAS_BLE

WifiScreen::WifiScreen() {
  this->lastUp = false;
  this->lastRefresh = 0;
}

// "How to connect" card: the SoftAP name + pass to reach the Nugget locally, and — when it has ALSO
// joined an existing network — the live STA IP (browse straight to it on your LAN), plus the detected
// OS fingerprint. Refreshes ~1/s so the IP appears the moment the join completes.
bool WifiScreen::draw() {
  display->drawString(0, 0, "Connect");
  display->drawLine(0, 11, 127, 11);
  display->drawString(0, 13, "AP:"); display->drawString(22, 13, gApSsid.length() ? gApSsid : String("Nugget AP"));
#if BOARD_HAS_BLE
  display->drawString(0, 24, "PIN:");  display->drawString(30, 24, gApPin.length() ? gApPin : String("------"));
#else
  display->drawString(0, 24, "Pass:"); display->drawString(30, 24, gApPass.length() ? gApPass : String("nugget123"));
#endif
  // Line 3: when STA creds are set, show the joined network + its IP (what you browse to on your LAN);
  // otherwise the mDNS address for the SoftAP. WiFi.localIP() is 0.0.0.0 until DHCP finishes.
  if (gStaSsid.length()) {
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress((uint32_t)0)) {
      display->drawString(0, 35, "IP:"); display->drawString(22, 35, WiFi.localIP().toString());
    } else {
      display->drawString(0, 35, "Join:"); display->drawString(36, 35, gStaSsid);
    }
  } else {
    display->drawString(0, 35, "Web:"); display->drawString(30, 35, "nugget.local");
  }
  // OS guess + full enumeration fingerprint (C=config D=device S=string reqs) for Mac/Linux calibration
  display->drawString(0, 46, "OS:" + nuggetOS() + " C" + String(nuggetOSReqs())
                             + " D" + String(nuggetOSDev()) + " S" + String(nuggetOSStr()));
  return true;
}

int WifiScreen::update(int btn) {
  this->alwaysUpdates(true);
  // A (BTN_RIGHT) or B (BTN_LEFT) returns to the payload list.
  if (btn == BTN_LEFT || btn == BTN_RIGHT) { return SCREEN_BACK; }
  unsigned long now = millis();
  if (now - this->lastRefresh > 1000) { this->lastRefresh = now; return SCREEN_REDRAW; }
  return SCREEN_NONE;
}
