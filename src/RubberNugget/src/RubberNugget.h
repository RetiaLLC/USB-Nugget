#pragma once

#include "Arduino.h"
#include "cdcusb.h"
#include "mscusb.h"
#include "flashdisk.h"

class RubberNugget {
  public:
    RubberNugget(){};
    static void init();
    static String* allPayloadPaths(const char* path="/");
};

struct NuggetConfig {
  String locale;
  String network;
  String password;
  String staSsid;   // AP to JOIN (STA mode); empty = AP-only
  String staPass;
  long pid;
  long vid;
};

FILINFO* newFileList(const char* path, int& numFiles);
NuggetConfig getConfig();

// STA "Join AP" state + action, shared with the WiFi screen
extern String gStaSsid;
extern String gStaPass;
// Our AP creds, shown on the Connect info screen
extern String gApSsid;
extern String gApPass;
extern String gApPin;        // per-device BLE control PIN (persisted); shown on the Connect screen
String blePinRegen();        // ~G: rotate + persist the BLE PIN, returns the new value
void wifiStaJoin();

// OS fingerprint ($_OS) from USB enumeration request counts
String nuggetOS();          // "WINDOWS"/"MACOS"/"LINUX"/"?"
uint32_t nuggetOSReqs();    // config-descriptor request count
uint32_t nuggetOSDev();     // device-descriptor request count
uint32_t nuggetOSStr();     // string-descriptor request count
