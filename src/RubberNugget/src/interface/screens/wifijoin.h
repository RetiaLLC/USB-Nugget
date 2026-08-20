#pragma once
#include "../lib/NuggetInterface.h"

// Boot / on-demand Wi-Fi join screen. WiFi.begin() is kicked off elsewhere (RubberNugget::init at boot,
// or saveWifi() from the web UI); this screen POLLS the result and shows it: "Joining <ssid>" with an
// animated ellipsis, then either "Connected!  IP x.x.x.x" or, on timeout, it falls back to AP-only (so
// the SoftAP / captive portal stays solid) and shows "Use AP: <SoftAP>". Auto-advances to the payload
// list after a short hold; LEFT/RIGHT skips. No reboot needed.
class WifiJoinScreen : public NuggetScreen {
  public:
    WifiJoinScreen(String ssid);
    ~WifiJoinScreen(){};
    bool draw();
    int update(int);
  private:
    String ssid;
    uint8_t phase;            // 0 = joining, 1 = connected, 2 = failed (fell back to AP)
    unsigned long timeoutAt;  // give up joining after this
    unsigned long doneAt;     // when to auto-advance after a result is shown
    unsigned long lastAnim;   // ellipsis animation tick
    uint8_t dots;
    String ip;
};
