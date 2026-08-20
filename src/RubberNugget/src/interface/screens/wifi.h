#pragma once
#include "../lib/NuggetInterface.h"

// "Connect" info screen. Reached with B from the payload browser root. Read-only:
// shows the AP SSID, password and the nugget.local web-UI URL so a user knows how
// to reach the payload editor. A or B returns to the list.
class WifiScreen : public NuggetScreen {
  public:
    WifiScreen();
    ~WifiScreen(){};
    bool draw();
    int update(int);
  private:
    bool lastUp;
    unsigned long lastRefresh;   // periodic redraw so the STA IP appears live as the join completes
};
