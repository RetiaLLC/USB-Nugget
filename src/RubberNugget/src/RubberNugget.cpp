#include "Arduino.h"
#include <WiFi.h>
#include <ESPmDNS.h>   // nugget.local
#include <esp_random.h>   // hardware RNG for the persisted BLE PIN
#include "board_config.h"   // BOARD_HAS_BLE / pins / NuggetDisplay — must precede the NimBLE include
#if BOARD_HAS_BLE
#include <NimBLEDevice.h>   // BLE Nordic-UART transport (S3 only; the S2 USB Nugget has no Bluetooth)
#endif

#include "RubberNugget.h"
#include "utils.h"

// ESPTinyUSB libraries
#include "cdcusb.h"
#include "mscusb.h"
#include "flashdisk.h"
#include "hidcomposite.h"
#include "remote.h"
#include "interface/lib/NuggetInterface.h"   // NuggetInterface::injectScreen (remote ~X run)
#include "interface/screens/runner.h"        // ScriptRunnerScreen (remote ~X run)

HIDcomposite hid;           // keyboard + mouse on ONE HID interface — frees the endpoint the CDC needs

// STA "Join AP" — lets the workbench Pi reach the Nugget's web UI over a shared AP
String gStaSsid;
String gStaPass;
// Our own AP creds, shown on the "Connect" info screen (B on the main list)
String gApSsid;
String gApPass;
String gApPin;   // per-device BLE control PIN (persisted /.blepin), shown on the Connect screen
const char* NUGGET_MDNS = "nugget";   // browse to http://nugget.local
// Restart the mDNS responder. The boot-time MDNS.begin() runs before the STA has an IP, so it only
// covers the SoftAP; call this once the STA joins a LAN so nugget.local resolves there too.
void nugMdnsRestart() {
  MDNS.end();
  if (MDNS.begin(NUGGET_MDNS)) MDNS.addService("http", "tcp", 80);
}

// --- OS fingerprint ($_OS) ---
// The host asks for the USB config descriptor a distinguishable number of times
// during enumeration; EspTinyUSB (Retia patch) counts them in g_usbCfgDescReqs.
// The thresholds below are PLACEHOLDERS — the Connect screen shows the raw count so
// we can set them from real Windows/macOS/Linux numbers.
extern volatile uint32_t g_usbCfgDescReqs;
extern volatile uint32_t g_usbDevDescReqs;   // device-descriptor requests
extern volatile uint32_t g_usbStrDescReqs;   // string-descriptor requests
uint32_t nuggetOSReqs() { return g_usbCfgDescReqs; }
uint32_t nuggetOSDev()  { return g_usbDevDescReqs; }
uint32_t nuggetOSStr()  { return g_usbStrDescReqs; }
volatile char g_forceOS = 0;   // manual $_OS override from the app (~O): 0=auto, else W/M/L/A/I
String nuggetOS() {
  // Manual override (app "OS" picker -> ~O<char>) wins over the fingerprint. This is the reliable
  // path for BadUSB: you usually KNOW the target, and it splits hosts the fingerprint can't (iOS/macOS).
  switch (g_forceOS) {
    case 'W': return "WINDOWS"; case 'M': return "MACOS"; case 'L': return "LINUX";
    case 'A': return "ANDROID"; case 'I': return "IOS";
  }
  // Auto-fingerprint from USB enumeration (config C / device D / string S descriptor request counts).
  // Calibrated 2026-07 from real hosts:
  //   Windows C4 D3 S13 · macOS C3 D2 S15 · iOS(iPhone 15) C3 D2 S12 · Linux(RPi) C3 D2 S8 · Android(Pixel 10) C3 D2 S26
  uint32_t c = g_usbCfgDescReqs;
  if (c == 0) return "NONE";                    // nothing enumerated us — BLE-only or a power-only port
  if (c >= 4) return "WINDOWS";                 // Windows asks for the config descriptor more times
  // C==3 ties the Apple/Linux/Android family; the string-descriptor count splits them:
  //   Linux ~8 · Apple(macOS/iOS) ~12-15 · Android ~26.
  uint32_t s = g_usbStrDescReqs;
  if (s >= 21) return "ANDROID";
  // Apple host. macOS reads ~15 direct, but a USB HUB in the path lowers its string-descriptor count
  // into the old iOS band (~12) and made real Macs misreport as IOS. iOS-as-host is rare and types the
  // same as macOS (GUI = Cmd), so report MACOS for the whole Apple range and force IOS via ~O when
  // you actually target an iPhone/iPad. (Was: s>=14 MACOS / s>=10 IOS — the split was hub-fragile.)
  if (s >= 10) return "MACOS";
  return "LINUX";
}

// --- Script manager (remote ~L/~F/~W/~X/~D) ---
// Called from remoteService() (task ctx: flash + injectScreen aren't callback-safe). Each answers
// over the current transport via remoteReply* (CDC or BLE, routed by remoteService()).
extern NuggetInterface* nuggetInterface;   // the live UI, created in the .ino; NULL-checked before use

void scriptList() {
  String* paths = RubberNugget::allPayloadPaths();   // comma-separated w/ trailing comma; NULL if empty
  remoteReply("~LST ");
  if (paths) { remoteReplyBytes((const uint8_t*)paths->c_str(), paths->length()); delete paths; }
  remoteReply("\n");
}

void scriptRead(const char* path) {
  fileOp op = readFile(String(path));
  if (!op.ok) { remoteReply("~ERR read\n"); return; }
  char hdr[128];
  snprintf(hdr, sizeof(hdr), "~FIL %s:%d\n", path, (int)op.result.length());
  remoteReply(hdr);
  remoteReplyBytes((const uint8_t*)op.result.c_str(), op.result.length());   // exactly <len> raw bytes
}

void scriptWrite(const char* path, const char* body, int len) {
  (void)len;                                         // body is NUL-terminated at <len> (payloads are text)
  fileOp op = saveFile(String(path), String(body));
  remoteReply(op.ok ? "~OK write\n" : "~ERR write\n");
}

void scriptRun(const char* path) {
  fileOp op = readFile(String(path));
  if (!op.ok) { remoteReply("~ERR run\n"); return; }
  if (nuggetInterface) nuggetInterface->injectScreen(new ScriptRunnerScreen(op.result));
  remoteReply("~OK run\n");
}

void scriptDelete(const char* path) {
  FRESULT res = f_unlink(path);
  remoteReply(res == FR_OK ? "~OK del\n" : "~ERR del\n");
}

// --- BLE PIN (persisted per-device auth for the wireless remote) ---
static String blePinGen() {
  uint32_t r = esp_random() % 900000UL + 100000UL;   // 6 digits: 100000..999999
  return String(r);
}
void blePinLoad() {   // init(): use the persisted PIN, or generate + save one on first boot
  fileOp op = readFile("/.blepin");
  if (op.ok) { String p = op.result; p.trim(); if (p.length() == 6) { gApPin = p; return; } }
  gApPin = blePinGen(); saveFile("/.blepin", gApPin);
}
String blePinRegen() {   // ~G: rotate the PIN, persist it, return the new value
  gApPin = blePinGen(); saveFile("/.blepin", gApPin); return gApPin;
}

void wifiStaJoin() {
  if (gStaSsid.length()) {
    WiFi.begin(gStaSsid.c_str(), gStaPass.c_str());
  }
}
CDCusb CDCUSBSerial;
FlashUSB fat1;

char *l1 = "ffat";
String payloadPath = "";

/*-----------------------------------------------------------------*/

class MyCDCCallbacks : public CDCCallbacks {
    void onCodingChange(cdc_line_coding_t const* p_line_coding)
    {
        int bitrate = CDCUSBSerial.getBitrate();
        Serial.printf("new bitrate: %d\n", bitrate);
    }

    bool onConnect(bool dtr, bool rts)
    {
        Serial.printf("connection state changed, dtr: %d, rts: %d\n", dtr, rts);
        return true;  // allow to persist reset, when Arduino IDE is trying to enter bootloader mode
    }

    void onData()
    {
        int len = CDCUSBSerial.available();
        uint8_t buf[len] = {};
        CDCUSBSerial.read(buf, len);
        remoteFeed(buf, len);          // serial remote-control: ~P press / ~S screen / ~R reboot-bl
        // NOTE: do NOT echo to Serial here. Serial (HWCDC/UART0) has no reader on the bench, so its
        // TX buffer fills and Serial.write() blocks *inside this USB callback*, wedging CDC RX and
        // killing the whole remote. That was the root cause of the flaky ~P / dead ~S.
    }
};

class Device: public USBCallbacks {
    void onMount() { Serial.println("Mount"); }
    void onUnmount() { Serial.println("Unmount"); }
    void onSuspend(bool remote_wakeup_en) { Serial.println("Suspend"); }
    void onResume() { Serial.println("Resume"); }
};

class MyHIDCallbacks : public HIDCallbacks
{
    void onData(uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
    {
        Serial.printf("ID: %d, type: %d, size: %d\n", report_id, (int)report_type, bufsize);
        for (size_t i = 0; i < bufsize; i++)
        {
            Serial.printf("%d\n", buffer[i]);
        }
    }
};

void echo_all(char c) {
    CDCUSBSerial.write(c);
    Serial.write(c);
}

/*-----------------------------------------------------------------*/


// A few benign demo payloads written to the FAT drive on first boot so the
// device and web UI aren't empty out of the box. Each one exercises a different
// subsystem (keyboard, mouse, LEDs, loops) and is safe to run on the bench.
// Guarded by a hidden "/.seeded" marker so it only happens once and never
// clobbers the operator's own edits on later boots.
struct DefaultPayload { const char* path; const char* body; };
static const DefaultPayload kDefaultPayloads[] = {
  { "/hello.txt",
    "// Types a greeting - focus a text field on the host first\n"
    "SCREEN Hello!\n"
    "LED G\n"
    "TYPE Hello from the Bad Nugget!\n"
    "ENTER\n" },
  { "/jiggler.txt",
    "// Mouse anti-idle - safe to run anywhere, no keystrokes\n"
    "SCREEN Jiggling\n"
    "LED C\n"
    "LOOP 60\n"
    "  JIGGLE 1\n"
    "  WAIT 2000\n"
    "ENDLOOP\n" },
  { "/leds.txt",
    "// Ear LED colour cycle - device only, no HID output\n"
    "LED R\n"
    "WAIT 400\n"
    "LED Y\n"
    "WAIT 400\n"
    "LED G\n"
    "WAIT 400\n"
    "LED C\n"
    "WAIT 400\n"
    "LED B\n"
    "WAIT 400\n"
    "LED M\n"
    "WAIT 400\n"
    "LED W\n"
    "WAIT 400\n" },
  { "/mouse.txt",
    "// Moves the cursor in a square, then a left click\n"
    "SCREEN Mouse demo\n"
    "LED M\n"
    "LOOP 3\n"
    "  MOUSE_MOVE 60 0\n"
    "  WAIT 300\n"
    "  MOUSE_MOVE 0 60\n"
    "  WAIT 300\n"
    "  MOUSE_MOVE -60 0\n"
    "  WAIT 300\n"
    "  MOUSE_MOVE 0 -60\n"
    "  WAIT 300\n"
    "ENDLOOP\n"
    "MOUSE_CLICK L\n" },
};

static void seedDefaultPayloads() {
  // Only seed once: presence of the marker means we've already run (or the
  // operator deleted the demos on purpose — don't bring them back).
  FILINFO fno;
  if (f_stat("/.seeded", &fno) == FR_OK) {
    return;
  }
  for (auto& p : kDefaultPayloads) {
    fileOp op = saveFile(String(p.path), String(p.body));
    if (!op.ok) {
      Serial.printf("[seed] failed to write %s: %s\n", p.path, op.result.c_str());
    }
  }
  saveFile("/.seeded", "1");   // marker; hidden from listings (leading '.')
  Serial.println("[seed] default payloads written");
}

// Per-OS example payloads, organised into /windows /macos /linux /android folders (saveFile() creates
// the parent dir). Each is a real, benign, reversible demo of that OS's hotkeys — safe to run on the
// bench and useful as a teaching set. Seeded under a SEPARATE marker (/.seeded_os) so units already
// seeded with the root demos still gain the folders on the next boot without touching the operator's
// edits. Tag any of these for auto-deploy with autorun_<os> in .usbnugget.conf or the web UI.
static const DefaultPayload kOsPayloads[] = {
  // ---------------- Windows (GUI r = Run, GUI l = lock, CTRL SHIFT ESC = Task Mgr) ----------------
  { "/windows/hello.txt",
    "REM Notepad greeting via the Run dialog\n"
    "SCREEN Win: hello\n"
    "LED G\n"
    "GUI r\n"
    "WAIT 600\n"
    "STRINGLN notepad\n"
    "WAIT 1200\n"
    "STRING Hello from the Bad Nugget on Windows!\n" },
  { "/windows/sysinfo.txt",
    "REM Open System Information (msinfo32) - read-only recon demo\n"
    "SCREEN Win: sysinfo\n"
    "LED C\n"
    "GUI r\n"
    "WAIT 600\n"
    "STRINGLN msinfo32\n" },
  { "/windows/ipconfig.txt",
    "REM Open a console and print the IP configuration - benign recon\n"
    "SCREEN Win: ipconfig\n"
    "LED B\n"
    "GUI r\n"
    "WAIT 600\n"
    "STRINGLN cmd\n"
    "WAIT 1000\n"
    "STRINGLN ipconfig /all\n" },
  { "/windows/lock.txt",
    "REM Lock the workstation - instantly reversible, shows a GUI combo\n"
    "SCREEN Win: lock\n"
    "LED M\n"
    "GUI l\n" },
  { "/windows/taskmgr.txt",
    "REM Open Task Manager with the three-key shortcut\n"
    "SCREEN Win: taskmgr\n"
    "LED Y\n"
    "CTRL SHIFT ESC\n" },

  // ---------------- macOS (GUI SPACE = Spotlight, CTRL GUI q = lock, GUI SHIFT 3 = screenshot) ------
  { "/macos/hello.txt",
    "REM TextEdit greeting via Spotlight\n"
    "SCREEN Mac: hello\n"
    "LED G\n"
    "GUI SPACE\n"
    "WAIT 500\n"
    "STRING TextEdit\n"
    "WAIT 500\n"
    "ENTER\n"
    "WAIT 1500\n"
    "STRING Hello from the Bad Nugget on macOS!\n" },
  { "/macos/terminal.txt",
    "REM Open Terminal via Spotlight and print the OS version\n"
    "SCREEN Mac: terminal\n"
    "LED C\n"
    "GUI SPACE\n"
    "WAIT 500\n"
    "STRING Terminal\n"
    "WAIT 500\n"
    "ENTER\n"
    "WAIT 1500\n"
    "STRINGLN sw_vers\n" },
  { "/macos/note.txt",
    "REM Open Notes via Spotlight and jot a line\n"
    "SCREEN Mac: note\n"
    "LED B\n"
    "GUI SPACE\n"
    "WAIT 500\n"
    "STRING Notes\n"
    "WAIT 500\n"
    "ENTER\n"
    "WAIT 1500\n"
    "STRING Bad Nugget was here.\n" },
  { "/macos/lock.txt",
    "REM Lock the screen (Control-Command-Q)\n"
    "SCREEN Mac: lock\n"
    "LED M\n"
    "CTRL GUI q\n" },
  { "/macos/screenshot.txt",
    "REM Full-screen screenshot (Command-Shift-3)\n"
    "SCREEN Mac: screenshot\n"
    "LED Y\n"
    "GUI SHIFT 3\n" },

  // ---------------- Linux (CTRL ALT t = terminal, ALT F2 = run, GUI l = lock on GNOME) -------------
  { "/linux/hello.txt",
    "REM Open a terminal and echo a greeting\n"
    "SCREEN Lin: hello\n"
    "LED G\n"
    "CTRL ALT t\n"
    "WAIT 1500\n"
    "STRINGLN echo Hello from the Bad Nugget on Linux!\n" },
  { "/linux/sysinfo.txt",
    "REM Terminal + uname - benign recon\n"
    "SCREEN Lin: uname\n"
    "LED C\n"
    "CTRL ALT t\n"
    "WAIT 1500\n"
    "STRINGLN uname -a\n" },
  { "/linux/distro.txt",
    "REM Terminal + lsb_release - which distro is this?\n"
    "SCREEN Lin: distro\n"
    "LED B\n"
    "CTRL ALT t\n"
    "WAIT 1500\n"
    "STRINGLN lsb_release -a\n" },
  { "/linux/run.txt",
    "REM GNOME/KDE run dialog (Alt-F2) launches the calculator\n"
    "SCREEN Lin: run\n"
    "LED W\n"
    "ALT F2\n"
    "WAIT 700\n"
    "STRINGLN gnome-calculator\n" },
  { "/linux/lock.txt",
    "REM Lock the session (Super-L on most desktops)\n"
    "SCREEN Lin: lock\n"
    "LED M\n"
    "GUI l\n" },

  // ---------------- Android (USB-host HID is limited; text entry + basic keys only) ----------------
  { "/android/hello.txt",
    "REM Types a greeting into the focused field, then Enter\n"
    "SCREEN Andr: hello\n"
    "LED G\n"
    "STRINGLN Hello from the Bad Nugget on Android!\n" },
  { "/android/search.txt",
    "REM Assumes a search/URL box is focused - type a query and go\n"
    "SCREEN Andr: search\n"
    "LED C\n"
    "STRING bad nugget usb\n"
    "WAIT 300\n"
    "ENTER\n" },
  { "/android/note.txt",
    "REM Multi-line note into a focused text field\n"
    "SCREEN Andr: note\n"
    "LED B\n"
    "STRINGLN Shopping list:\n"
    "STRINGLN - solder\n"
    "STRINGLN - flux\n" },
  { "/android/jiggle.txt",
    "REM Cursor wiggle - Android accepts USB mice; no keystrokes\n"
    "SCREEN Andr: jiggle\n"
    "LED M\n"
    "LOOP 30\n"
    "  JIGGLE 1\n"
    "  WAIT 1500\n"
    "ENDLOOP\n" },
  { "/android/url.txt",
    "REM Assumes the browser address bar is focused\n"
    "SCREEN Andr: url\n"
    "LED Y\n"
    "STRING retia.io\n"
    "WAIT 300\n"
    "ENTER\n" },
};

static void seedOsPayloads() {
  // Separate marker from the root demos: an already-seeded unit (/.seeded present) still gets the
  // per-OS folders added on the next boot, and re-running never clobbers the operator's own edits.
  FILINFO fno;
  if (f_stat("/.seeded_os", &fno) == FR_OK) return;
  for (auto& p : kOsPayloads) {
    fileOp op = saveFile(String(p.path), String(p.body));   // saveFile() creates /windows /macos ... as needed
    if (!op.ok) Serial.printf("[seed-os] failed %s: %s\n", p.path, op.result.c_str());
  }
  saveFile("/.seeded_os", "1");
  Serial.println("[seed-os] per-OS example payloads written");
}

#if BOARD_HAS_BLE
// ---- BLE Nordic UART Service: the ~P/~S remote works wirelessly too ----
// App writes commands to RX; the device notifies replies (incl. the ~SCR framebuffer) on TX.
#define NUS_SVC "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX  "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"   // app -> device (write)
#define NUS_TX  "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"   // device -> app (notify)

static NimBLECharacteristic* g_txChar = nullptr;
static volatile bool g_bleConnected = false;
static volatile uint16_t g_bleMtu = 23;

// Called from remote.cpp's cdcAll() when the active reply transport is BLE. Chunks to the negotiated
// MTU and paces the notifies (runs on the web task for the big dump, so delay() is fine).
void nugBleTx(const uint8_t* p, int n) {
  if (!g_bleConnected || g_txChar == nullptr) return;
  int chunk = (g_bleMtu > 23) ? (g_bleMtu - 3) : 20;
  int sent = 0;
  while (sent < n) {
    int c = (n - sent < chunk) ? (n - sent) : chunk;
    g_txChar->setValue((uint8_t*)(p + sent), c);
    g_txChar->notify();
    sent += c;
    delay(6);
  }
}

class NugSrvCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, ble_gap_conn_desc* d) override { g_bleConnected = true; }
  void onDisconnect(NimBLEServer* s) override {
    g_bleConnected = false; g_bleMtu = 23;
    remoteBleReset();                 // next client must re-send the passphrase (~K) before it can control
    NimBLEDevice::startAdvertising();
  }
  void onMTUChange(uint16_t mtu, ble_gap_conn_desc* d) override { g_bleMtu = mtu; }
};

class NugRxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override {
    std::string v = c->getValue();
    if (v.length()) remoteFeed((const uint8_t*)v.data(), (int)v.length(), REMOTE_TR_BLE);
  }
};

static void nugBleInit() {
  NimBLEDevice::init("Nugget");
  NimBLEDevice::setMTU(247);   // request a large MTU so the 2KB framebuffer dump is only ~9 packets
  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new NugSrvCallbacks());
  NimBLEService* svc = server->createService(NUS_SVC);
  NimBLECharacteristic* rx = svc->createCharacteristic(NUS_RX,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rx->setCallbacks(new NugRxCallbacks());
  g_txChar = svc->createCharacteristic(NUS_TX, NIMBLE_PROPERTY::NOTIFY);
  svc->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(NUS_SVC);
  adv->setScanResponse(true);
  NimBLEDevice::startAdvertising();
  Serial.println("BLE NUS 'Nugget' advertising");
}
#else
// S2 (USB Nugget) has no Bluetooth — stubs so remote.cpp links (nugBleTx) and init() compiles.
void nugBleTx(const uint8_t*, int) {}
static void nugBleInit() {}
#endif  // BOARD_HAS_BLE

// Boot-progress marker on the OLED. Brought up before USB so (a) the device is usable via
// buttons+screen even if USB never enumerates, and (b) a hang's LAST step stays on-screen — the
// definitive way to localize an S2 USB-composite hang without a serial console.
static void bootMsg(const char* s) {
  if (!g_display) return;
  g_display->clear();
  g_display->drawString(0, 0, "boot:");
  g_display->drawString(40, 0, s);
  g_display->display();
}

void RubberNugget::init() {
  // OLED FIRST (before FS/USB) so boot progress is visible and the UI survives a USB failure.
  g_display = new NuggetDisplay(OLED_ADDR, OLED_SDA, OLED_SCL);
  g_display->init();
#if OLED_FLIP
  g_display->flipScreenVertically();
#endif
  g_display->setFont(ArialMT_Plain_10);
  bootMsg("start");

  // Mount FAT fs
  bootMsg("fs");
  if (fat1.init("/fat1", "ffat")) {
        //disable this on startup
        if (fat1.begin()) {
            Serial.println("MSC lun 1 begin");
            // Seed demo payloads before the USB host enumerates/mounts the MSC
            // drive, so there's no host-side FAT cache to conflict with.
            seedDefaultPayloads();
            seedOsPayloads();   // /windows /macos /linux /android example folders (own marker: /.seeded_os)
            blePinLoad();   // load or first-boot-generate the persisted BLE PIN (needs the FS mounted)
        }
        else {
            log_e("LUN 1 failed");
        }
  }
  bootMsg("usb-cdc");
  if (!CDCUSBSerial.begin())
      Serial.println("Failed to start CDC USB stack");
  CDCUSBSerial.setCallbacks(new MyCDCCallbacks());
  EspTinyUSB::registerDeviceCallbacks(new Device());

  // Read config from settings file (kept in gConfig for the web UI + OS auto-deploy)
  NuggetConfig c = getConfig();
  gConfig = c;

  // Register ALL USB interfaces (CDC + MSC above, HID here) BEFORE the slow WiFi setup, so the
  // host enumerates the full composite at once — fixes the racy CDC+MSC-without-HID descriptor.
  bootMsg("usb-hid");
  hid.deviceID(c.vid,c.pid);
  hid.setBaseEP(3);
  hid.begin();
  hid.setCallbacks(new MyHIDCallbacks());

  // RACE FIX: fat1.begin() (MSC) above already fired tusb_init() with an MSC-ONLY descriptor, before
  // CDC+HID registered and before deviceID() set 05ac:020b. A host that enumerated in that window saw a
  // partial/invalid composite (observed: 303a:0002, bNumInterfaces=1 Mass Storage, "can't set config").
  // Now that the FULL composite (MSC+CDC+HID) is assembled, force one clean re-enumeration so every host
  // reliably reads the complete descriptor + correct VID/PID.
  bootMsg("usb-conn");
  tud_disconnect();
  delay(150);
  tud_connect();

  // WiFi: always run our own AP; if STA creds are set, ALSO join that AP (AP+STA)
  bootMsg("wifi");
  gStaSsid = c.staSsid; gStaPass = c.staPass;
  gApSsid = c.network;  gApPass = c.password;
  // AP-only unless STA creds are set. IMPORTANT: booting AP+STA with an IDLE (unconnected) STA makes
  // the ESP32 background-scan and it DESTROYS the SoftAP data path (bench: 100% ping loss while the AP
  // still beacons). So stay AP-only when not joining; saveWifi() switches to AP_STA on demand.
  WiFi.mode(gStaSsid.length() ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(c.network.c_str(), c.password.c_str());
  if (gStaSsid.length()) {
    Serial.printf("Joining AP '%s'...\n", gStaSsid.c_str());
    wifiStaJoin();   // non-blocking; status/IP shown on the Connect screen
  }
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);

  // mDNS: let clients reach the web UI at http://nugget.local instead of the IP
  bootMsg("mdns");
  if (MDNS.begin(NUGGET_MDNS)) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS: http://nugget.local");
  }

  bootMsg("ble");
  nugBleInit();   // BLE NUS remote ('Nugget'); coexists with the Wi-Fi AP on the shared 2.4GHz radio
  bootMsg("ready");
}

// newFileList returns a paginated list of strings representing the files
// available at the given path. resultsPerPage will be set to number of results
// returned. On error, it returns nullptr and numFiles is set to -1.
FILINFO* newFileList(const char* path, int& numFiles) {
  uint currentCapacity = 4; // arbitrary, will grow as needed
  uint filesSoFar = 0;
  bool errored = false;
  FILINFO* fileList = new FILINFO[currentCapacity];

  FRESULT res;
  FF_DIR dir;
  FILINFO fno;

  res = f_opendir(&dir, path);
  if (res != FR_OK) {
    Serial.printf("[newFileList] f_opendir error: %d\n", res);
    goto onError;
  }

  for (;;) {
      res = f_readdir(&dir, &fno); // Read a directory item
      if (res != FR_OK) {
        errored = true;
        break;
      }
      if (fno.fname[0] == 0) { // End of dir
        break;
      }
      if (fno.fname[0] == '.') { // Don't show files starting with '.'
        continue;
      }
      // Found a file/dir. Store it, doubling capacity first if needed. 
      if (filesSoFar >= currentCapacity) {
        FILINFO* newFileList = new FILINFO[currentCapacity*2];
        for (int i = 0; i < currentCapacity; i++){
          newFileList[i] = fileList[i];
        }
        currentCapacity *= 2;
        delete[] fileList;
        fileList = newFileList;
      }
      fileList[filesSoFar] = fno;
      filesSoFar++; 
  }
  if (errored) {
    Serial.printf("[newFileList] f_readdir error: %d\n", res);
    goto onError;
  }
  res = f_closedir(&dir);
  if (res != FR_OK) {
    Serial.printf("[newFileList] f_closedir error: %d\n", res);
    goto onError;
  }
  if (filesSoFar < 1){
    Serial.println("[newFileList] directory empty");
    goto onError;
  }
  numFiles = filesSoFar;
  return fileList;

onError:
  numFiles = -1;
  delete[] fileList;
  return nullptr;
}

// allPayloadPaths returns a comma seperated list of paths to all payloads.
String* RubberNugget::allPayloadPaths(const char* path) {
  int numFiles = 0;
  FILINFO* files = newFileList(path, numFiles);

  // DFS on fs
  // TODO: measure max stack usage; if cutting it close reimplement
  // non-recursively.
  if (numFiles < 1) {
    return nullptr;
  }
  String* ret = new String;
  for(int i = 0; i < numFiles; i++) {
    // Skip FAT system/hidden entries (Windows' "System Volume Information", etc.) and dotfiles
    // (.blepin, .seeded) — they aren't runnable payloads and shouldn't clutter the list.
    if ((files[i].fattrib & (AM_HID | AM_SYS)) || files[i].fname[0] == '.') continue;
    if (files[i].fattrib & AM_DIR) { // Directory; recurse
      String recursivePath(path);
      if (recursivePath.length()>1){ // non-root
        recursivePath += "/";
      }
      recursivePath += files[i].fname;
      String* subDirFiles = allPayloadPaths(recursivePath.c_str());
      if (subDirFiles) {
        (*ret) += (*subDirFiles);
        delete subDirFiles;
      }
    } else { // file, append to list
      // TODO: escape commas in file names 
      String pathString(path);
      if (pathString.length() > 1) { // non-root
        pathString += "/";
      }
      (*ret) += pathString;
      (*ret) += files[i].fname;
      (*ret) += ",";
    }
  }
  delete[] files;
  return ret;
}

NuggetConfig getConfig() {
  NuggetConfig conf;
  conf.locale = "EN";
  conf.network = NUG_AP_SSID;
  conf.password = "nugget123";
  conf.staSsid = "";        // AP-only by default. Set sta_ssid/sta_pass in
  conf.staPass = "";        // .usbnugget.conf to ALSO join an existing AP.
  conf.pid = 0x20b;
  conf.vid = 0x05ac;

  fileOp configRead = readFile(".usbnugget.conf");
  if (!configRead.ok) {
    Serial.printf("config file could not be read: %s\n", configRead.result);
    return conf;
  }

  int lineStart = 0;
  String currentLine;
  String currentLineKeyValue;
  while (lineStart < configRead.result.length()) {
    int lineEnd = configRead.result.indexOf('\n', lineStart);
    if (lineEnd == -1){
      lineEnd = configRead.result.length();   // last line w/o trailing newline: process it, don't drop it
    }

    // process line
    currentLine = configRead.result.substring(lineStart, lineEnd);
    currentLine.trim();
    int valueStart = currentLine.indexOf("\"");
    int valueEnd = currentLine.lastIndexOf("\"");
    if (valueStart == -1 || valueStart==valueEnd) {
      // couldn't find both quotes
      goto nextLine;
    }
    currentLineKeyValue = currentLine.substring(valueStart+1, valueEnd);

    if (currentLine.indexOf("network = \"") == 0) {
      conf.network = currentLineKeyValue;
    } else
    if (currentLine.indexOf("password = \"") == 0) {
      if (currentLineKeyValue.length() < 8) {
        goto nextLine;
      }
      conf.password = currentLineKeyValue;
    } else
    if (currentLine.indexOf("sta_ssid = \"") == 0) {
      conf.staSsid = currentLineKeyValue;
    } else
    if (currentLine.indexOf("sta_pass = \"") == 0) {
      conf.staPass = currentLineKeyValue;
    } else
    if (currentLine.indexOf("autorun_win = \"") == 0) {
      conf.autorunWin = currentLineKeyValue;
    } else
    if (currentLine.indexOf("autorun_mac = \"") == 0) {
      conf.autorunMac = currentLineKeyValue;
    } else
    if (currentLine.indexOf("autorun_lin = \"") == 0) {
      conf.autorunLin = currentLineKeyValue;
    } else
    if (currentLine.indexOf("autorun_and = \"") == 0) {
      conf.autorunAnd = currentLineKeyValue;
    } else
    if (currentLine.indexOf("autorun_ios = \"") == 0) {
      conf.autorunIos = currentLineKeyValue;
    } else
    if (currentLine.indexOf("pid = \"") == 0) {
        char hex[currentLineKeyValue.length() + 1];
        strcpy(hex, currentLineKeyValue.c_str());
        char* ptr;
        conf.pid = strtoul(hex, &ptr, 16);
    } else
    if (currentLine.indexOf("vid = \"") == 0) {
        char hex[currentLineKeyValue.length() + 1];
        strcpy(hex, currentLineKeyValue.c_str());
        char* ptr;
        conf.vid = strtoul(hex, &ptr, 16);
    }
nextLine:
    // update line
    lineStart = lineEnd+1;
  }
  return conf;
}

// Active config, loaded once at boot (RubberNugget::init) and kept in sync by writeConfig().
NuggetConfig gConfig;

String autorunPathForOS(const String& os) {
  if (os == "WINDOWS")              return gConfig.autorunWin;
  if (os == "MACOS" || os == "OSX") return gConfig.autorunMac;
  if (os == "LINUX")               return gConfig.autorunLin;
  if (os == "ANDROID")             return gConfig.autorunAnd;
  if (os == "IOS")                 return gConfig.autorunIos;
  return "";
}

// Serialise the config back to .usbnugget.conf in the same `key = "value"` form getConfig() parses,
// so a round-trip through the web UI preserves everything (AP creds, STA creds, autorun map, vid/pid).
bool writeConfig(const NuggetConfig& c) {
  String out;
  out += "network = \""     + c.network    + "\"\n";
  out += "password = \""    + c.password   + "\"\n";
  out += "sta_ssid = \""    + c.staSsid    + "\"\n";
  out += "sta_pass = \""    + c.staPass    + "\"\n";
  out += "autorun_win = \"" + c.autorunWin + "\"\n";
  out += "autorun_mac = \"" + c.autorunMac + "\"\n";
  out += "autorun_lin = \"" + c.autorunLin + "\"\n";
  out += "autorun_and = \"" + c.autorunAnd + "\"\n";
  out += "autorun_ios = \"" + c.autorunIos + "\"\n";
  out += "vid = \""         + String(c.vid, HEX) + "\"\n";
  out += "pid = \""         + String(c.pid, HEX) + "\"\n";
  fileOp op = saveFile(".usbnugget.conf", out);
  if (op.ok) gConfig = c;
  return op.ok;
}
