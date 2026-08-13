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
  // OS auto-deploy: path of a payload to auto-run when that OS is detected on plug-in (empty = off).
  // Set via autorun_win/mac/lin/and/ios in .usbnugget.conf or the web UI's Network/Auto-deploy panel.
  String autorunWin;
  String autorunMac;
  String autorunLin;
  String autorunAnd;
  String autorunIos;
};

FILINFO* newFileList(const char* path, int& numFiles);
NuggetConfig getConfig();
// Return the configured auto-deploy payload path for an OS name ("WINDOWS"/"MACOS"/...), or "" if none.
String autorunPathForOS(const String& os);
// Persist Wi-Fi STA creds + the autorun map back to .usbnugget.conf, preserving the other keys.
bool writeConfig(const NuggetConfig& c);

// The active config (loaded at boot by getConfig(), updated by writeConfig()).
extern NuggetConfig gConfig;

// STA "Join AP" state + action, shared with the WiFi screen
extern String gStaSsid;
extern String gStaPass;
// Our AP creds, shown on the Connect info screen
extern String gApSsid;
extern String gApPass;
extern String gApPin;        // per-device BLE control PIN (persisted); shown on the Connect screen
String blePinRegen();        // ~G: rotate + persist the BLE PIN, returns the new value
void wifiStaJoin();
void nugMdnsRestart();       // re-advertise nugget.local after the STA joins a LAN

// OS fingerprint ($_OS) from USB enumeration request counts
String nuggetOS();          // "WINDOWS"/"MACOS"/"LINUX"/"?"
uint32_t nuggetOSReqs();    // config-descriptor request count
uint32_t nuggetOSDev();     // device-descriptor request count
uint32_t nuggetOSStr();     // string-descriptor request count
