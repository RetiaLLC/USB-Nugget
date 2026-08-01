#include "Arduino.h"
#include <WiFi.h>
#include <ESPmDNS.h>   // nugget.local
#include <esp_random.h>   // hardware RNG for the persisted BLE PIN
#include <NimBLEDevice.h>   // BLE Nordic-UART transport for the ~P/~S remote (see src/remote.cpp)

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
  //   Linux ~8 · iOS ~12 · macOS ~15 · Android ~26.  NOTE: iOS↔macOS is a NARROW split — more samples wanted.
  uint32_t s = g_usbStrDescReqs;
  if (s >= 21) return "ANDROID";
  if (s >= 14) return "MACOS";
  if (s >= 10) return "IOS";                     // iPhone-as-host: fewer localized-string reads than macOS
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

void RubberNugget::init() {
  // Mount FAT fs
  if (fat1.init("/fat1", "ffat")) {
        //disable this on startup
        if (fat1.begin()) {
            Serial.println("MSC lun 1 begin");
            // Seed demo payloads before the USB host enumerates/mounts the MSC
            // drive, so there's no host-side FAT cache to conflict with.
            seedDefaultPayloads();
            blePinLoad();   // load or first-boot-generate the persisted BLE PIN (needs the FS mounted)
        }
        else {
            log_e("LUN 1 failed");
        }
  }
  if (!CDCUSBSerial.begin())
      Serial.println("Failed to start CDC USB stack");
  CDCUSBSerial.setCallbacks(new MyCDCCallbacks());
  EspTinyUSB::registerDeviceCallbacks(new Device());

  // Read config from settings file
  NuggetConfig c = getConfig();

  // Register ALL USB interfaces (CDC + MSC above, HID here) BEFORE the slow WiFi setup, so the
  // host enumerates the full composite at once — fixes the racy CDC+MSC-without-HID descriptor.
  hid.deviceID(c.vid,c.pid);
  hid.setBaseEP(3);
  hid.begin();
  hid.setCallbacks(new MyHIDCallbacks());

  // WiFi: always run our own AP; if STA creds are set, ALSO join that AP (AP+STA)
  gStaSsid = c.staSsid; gStaPass = c.staPass;
  gApSsid = c.network;  gApPass = c.password;
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
  if (MDNS.begin(NUGGET_MDNS)) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS: http://nugget.local");
  }

  nugBleInit();   // BLE NUS remote ('Nugget'); coexists with the Wi-Fi AP on the shared 2.4GHz radio
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
  conf.network = "Nugget AP";
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
      lineEnd = configRead.result.length();
      break;
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
