#include "wifi.h"
#include <WiFi.h>
#include "../../RubberNugget.h"

WifiScreen::WifiScreen() {
  this->lastUp = false;
}

// Read-only "how to connect" card: AP name + password + web-UI address + the
// detected OS with its raw enumeration-request count (for $_OS threshold tuning).
bool WifiScreen::draw() {
  display->drawString(0, 0, "Connect / edit");
  display->drawLine(0, 11, 127, 11);
  display->drawString(0, 13, "WiFi:"); display->drawString(30, 13, gApSsid.length() ? gApSsid : String("Nugget AP"));
  display->drawString(0, 24, "PIN:"); display->drawString(30, 24, gApPin.length() ? gApPin : String("------"));
  display->drawString(0, 35, "Web:");  display->drawString(30, 35, "nugget.local");
  // OS guess + full enumeration fingerprint (C=config D=device S=string reqs) for Mac/Linux calibration
  display->drawString(0, 46, "OS:" + nuggetOS() + " C" + String(nuggetOSReqs())
                             + " D" + String(nuggetOSDev()) + " S" + String(nuggetOSStr()));
  return true;
}

int WifiScreen::update(int btn) {
  // A (BTN_RIGHT) or B (BTN_LEFT) returns to the payload list.
  if (btn == BTN_LEFT || btn == BTN_RIGHT) { return SCREEN_BACK; }
  return SCREEN_NONE;
}
