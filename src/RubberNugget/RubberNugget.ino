#include <Adafruit_NeoPixel.h>
#include "src/RubberNugget.h"
#include "src/recovery_fixed.h"   // R2 (corrected): boot-counter + watchdog auto-revert to the rescue
#include "Arduino.h"
#include <base64.h>
#include "base64.hpp"

#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <DNSServer.h>   // captive portal

#include "webUI/index.h"

#include "src/utils.h"
#include "src/interface/screens/splash.h"
#include "src/interface/screens/dir.h"
#include "src/interface/screens/runner.h"
#include "src/interface/screens/wifijoin.h"
#include "src/interface/lib/NuggetInterface.h"
#include "src/remote.h"   // remoteService() for the deferred screen dump

const char *ssid = "Nugget AP";
const char *password = "nugget123";

WebServer server(80);
DNSServer dnsServer;      // captive portal: resolve every host to the AP so the UI auto-opens

TaskHandle_t webapp;
TaskHandle_t nuggweb;

// Captive portal: send any unknown URL (incl. the OS connectivity checks) to the
// payload UI, so the "sign in to network" sheet pops the page open automatically.
void handleCaptive() {
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

void getPayloads() {
  String* payloadPaths = RubberNugget::allPayloadPaths();
  if (payloadPaths == nullptr) {
    // Empty/unreadable filesystem: return an empty list. allPayloadPaths()
    // returns nullptr when there are no files — dereferencing it here used to
    // crash the web-server task (LoadProhibited) on a fresh device, which
    // aborted the page's payload fetch and left the CREATE buttons unwired.
    server.send(200, "text/plain", "");
    return;
  }
  Serial.printf("[SERVER][getPayloads] %s\n", payloadPaths->c_str());
  server.send(200, "text/plain", *payloadPaths);
  delete payloadPaths;   // allPayloadPaths() allocates a String on the heap
}

void handleRoot() {
  Serial.println("handling root!");
  // Serve the ~20KB UI from flash in small chunks with an EXPLICIT Content-Length. Two S2-specific
  // traps this sidesteps, both of which sent an empty 200 ("captive pops up but stays blank"):
  //   (1) String(INDEX) needs a 20KB *contiguous* heap block; under WiFi-SoftAP heap pressure that
  //       alloc fails and the String is empty.
  //   (2) send_P(200,ct,INDEX) computes length via strlen_P — which returned 0 on this pre-release
  //       core (bench-confirmed: curl saw Content-Length: 0), so nothing was sent.
  // sizeof(INDEX)-1 is the COMPILE-TIME length (INDEX ends in a NUL, can't be 0); sendContent_P streams
  // straight from flash-mapped .rodata 1KB at a time — the same proven send() path /payloads uses.
  const size_t len = sizeof(INDEX) - 1;
  server.setContentLength(len);
  server.send(200, "text/html", "");
  for (size_t i = 0; i < len; i += 1024) {
    server.sendContent_P(INDEX + i, (len - i < 1024) ? (len - i) : 1024);
  }
}

void delpayload() {
  String path(server.arg("path"));
  FRESULT res = f_unlink(path.c_str());
  if (res == FR_OK){
    server.send(200);
  } else {
    server.send(500);
  }
}

void websave() {
  fileOp decodeOp = base64Decode(server.arg("payloadText"));
  if (!decodeOp.ok){
    server.send(500, "text/plain", decodeOp.result);
    return;
  }
  fileOp saveOp = saveFile(server.arg("path"), decodeOp.result);
  if (!saveOp.ok){
    server.send(500, "text/plain", saveOp.result);
    return;
  }
  server.send(200, "text/plain", "payload saved successfully");
}

void webget() {
  String path = server.arg("path");
  fileOp op = readFile(path);
  if (!op.ok) {
    // TODO: send 500/4XX depending on file existence vs internal error
    server.send(500, "text/plain", String("error getting payload: ") + op.result);
    return;
  }
  String payload = base64::encode(op.result);
  server.send(200, "text/plain", payload);
}

NuggetInterface* nuggetInterface;

// run payload with get request path
void webrun() {
  fileOp op = readFile(server.arg("path"));
  if (op.ok) {
    server.send(200, "text/html", "Running payload...");
    NuggetScreen* runner = new ScriptRunnerScreen(op.result);
    bool ok = nuggetInterface->injectScreen(runner);
    return;
  }
  server.send(500, "text/html", "couldn't run payload: " + op.result);
}

void webrunlive() {
  // TODO: use server.arg "content" or "payload" instead of "plain"
  fileOp op = base64Decode(server.arg("plain"));
  if (op.ok) {
    server.send(200, "text/plain", "running live payload");
    NuggetScreen* runner = new ScriptRunnerScreen(op.result);
    bool ok = nuggetInterface->injectScreen(runner);
    // TODO: send 503 when device is busy
    return;
  }
  server.send(500, "text/html", "Device busy");
}

// --- Network config API (Wi-Fi station join + per-OS auto-deploy), consumed by the NETWORK tab ---
static String jsonEsc(const String& s) {
  String o; for (unsigned i = 0; i < s.length(); i++) { char c = s[i]; if (c=='"'||c=='\\') o += '\\'; o += c; } return o;
}
void getWifi() {
  bool conn = (WiFi.status() == WL_CONNECTED);
  String ip = conn ? WiFi.localIP().toString() : String("");
  String out = "{\"ap\":\"" + jsonEsc(gApSsid) + "\",\"sta_ssid\":\"" + jsonEsc(gConfig.staSsid) +
               "\",\"connected\":" + (conn ? "true" : "false") + ",\"ip\":\"" + ip + "\"}";
  server.send(200, "application/json", out);
}
void saveWifi() {
  NuggetConfig c = gConfig;
  c.staSsid = server.arg("sta_ssid");
  c.staPass = server.arg("sta_pass");
  if (!writeConfig(c)) { server.send(500, "text/plain", "config write failed"); return; }
  gStaSsid = c.staSsid; gStaPass = c.staPass;
  gConfig = c;   // keep gConfig in sync so GET /wifi (getWifi) reports the new STA, not the boot-time value
  // Respond BEFORE touching the radio: switching AP -> AP_STA can briefly blip the SoftAP, and we
  // want the client to have its reply in hand first.
  if (gStaSsid.length()) server.send(200, "text/plain", "Saved. Joining \"" + gStaSsid + "\" - check status in a few seconds.");
  else                   server.send(200, "text/plain", "Saved. Station disabled (AP-only).");
  delay(20);
  // Switch mode on demand (staying AP-only when idle keeps the SoftAP link solid). The response is
  // already sent, so the one-time SoftAP blip from the mode switch / channel-follow is harmless: the
  // client reconnects to the AP (now on the joined net's channel) or reaches the device via its STA IP.
  if (gStaSsid.length()) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(gApSsid.c_str(), gApPass.c_str());          // re-assert the AP across the mode change
    WiFi.begin(gStaSsid.c_str(), gStaPass.c_str());
    // Show the join result on the OLED too (no reboot). The screen polls the connection and, on
    // failure, falls back to AP-only so the captive portal stays reachable.
    if (nuggetInterface) nuggetInterface->injectScreen(new WifiJoinScreen(gStaSsid));
  } else {
    WiFi.disconnect(false, true);                           // drop the STA target
    WiFi.mode(WIFI_AP);                                     // back to AP-only for a stable idle SoftAP
  }
}
void getAutorun() {
  String out = "{\"os\":\"" + nuggetOS() + "\",\"win\":\"" + jsonEsc(gConfig.autorunWin) +
               "\",\"mac\":\"" + jsonEsc(gConfig.autorunMac) + "\",\"lin\":\"" + jsonEsc(gConfig.autorunLin) +
               "\",\"and\":\"" + jsonEsc(gConfig.autorunAnd) + "\",\"ios\":\"" + jsonEsc(gConfig.autorunIos) + "\"}";
  server.send(200, "application/json", out);
}
void saveAutorun() {
  NuggetConfig c = gConfig;
  c.autorunWin = server.arg("win"); c.autorunMac = server.arg("mac"); c.autorunLin = server.arg("lin");
  c.autorunAnd = server.arg("and"); c.autorunIos = server.arg("ios");
  if (!writeConfig(c)) { server.send(500, "text/plain", "config write failed"); return; }
  server.send(200, "text/plain", "Auto-deploy saved.");
}

// --- OS auto-deploy -----------------------------------------------------------------------------
// When a host is detected on plug-in, optionally auto-run the payload tagged for that OS via
// autorun_<os> in .usbnugget.conf / the web UI. It is OPT-IN (fires only if a path is configured for
// the detected OS), happens at most once per boot, and is cancellable by holding LEFT while plugging
// in. The payload itself still honours hold-LEFT / ~PB abort while running, so there are two brakes.
static bool g_autorunHandled = false;
static void maybeAutorun(uint32_t startedMs) {
  if (g_autorunHandled) return;
  uint32_t age = millis() - startedMs;
  if (age < 3000) return;                    // let USB enumeration + the OS fingerprint settle first
  String os = nuggetOS();
  if (os == "NONE") {                        // nothing has enumerated us yet — keep waiting, but cap it
    if (age > 20000) g_autorunHandled = true;
    return;
  }
  g_autorunHandled = true;                   // commit to a single decision for this boot
  String path = autorunPathForOS(os);
  if (path.length() == 0) return;            // no payload tagged for this OS -> do nothing
  if (digitalRead(BTN_LEFT) == BTN_PRESS) {  // safety: hold LEFT (back/abort) while plugging in to cancel
    Serial.println("[autorun] cancelled — LEFT held");
    return;
  }
  fileOp op = readFile(path);
  if (op.ok && nuggetInterface) {
    Serial.printf("[autorun] %s detected -> running %s\n", os.c_str(), path.c_str());
    nuggetInterface->injectScreen(new ScriptRunnerScreen(op.result));
  } else {
    Serial.printf("[autorun] configured payload not found: %s\n", path.c_str());
  }
}

void webserverInit(void *p) {
  uint32_t started = millis();
  wl_status_t lastSta = WL_IDLE_STATUS;
  while (1) {
    remoteService();                  // deferred ~S screen dump (task ctx so CDC TX can drain)
    dnsServer.processNextRequest();   // captive portal
    server.handleClient();
    maybeAutorun(started);            // OS auto-deploy (opt-in, once per boot, hold-LEFT cancels)
    // When the STA transitions to connected-with-IP, re-advertise mDNS so nugget.local resolves on the
    // joined LAN too (boot-time MDNS.begin only saw the SoftAP). Also unblocks the STA IP display.
    wl_status_t nowSta = WiFi.status();
    if (nowSta == WL_CONNECTED && lastSta != WL_CONNECTED && WiFi.localIP() != IPAddress((uint32_t)0)) {
      nugMdnsRestart();
    }
    lastSta = nowSta;
    vTaskDelay(2);
  }
}

void setup() {
  recoveryBegin();   // R2: FIRST — arm boot-counter + watchdog before USB/radio init so a bad
                     // build reverts to the rescue instead of wedging the bench (see src/recovery_fixed.h)
  Serial.begin(115200);

  RubberNugget::init();
 
  server.on("/", handleRoot);
  server.on("/payloads", getPayloads);
  server.on("/savepayload", HTTP_POST, websave);
  server.on("/deletepayload", HTTP_POST, delpayload);
  server.on("/runlive", HTTP_POST, webrunlive);
  server.on("/getpayload", HTTP_GET, webget);
  server.on("/runpayload", HTTP_GET, webrun);
  server.on("/wifi", HTTP_GET, getWifi);            // NETWORK tab: current AP/STA status
  server.on("/savewifi", HTTP_POST, saveWifi);      //             join/leave an existing Wi-Fi
  server.on("/autorun", HTTP_GET, getAutorun);      //             current per-OS auto-deploy map
  server.on("/saveautorun", HTTP_POST, saveAutorun);//             set per-OS auto-deploy
  server.onNotFound(handleCaptive);   // captive portal + friendly 404

  dnsServer.start(53, "*", WiFi.softAPIP());   // hijack DNS so any hostname -> our UI
  server.begin();

  xTaskCreate(webserverInit, "webapptask", 12 * 1024, NULL, 5, &webapp); // create task priority 1
  nuggetInterface = new NuggetInterface;
  NuggetScreen* dirScreen = new DirScreen("/");
  NuggetScreen* splashScreen = new SplashScreen(1500);
  nuggetInterface->pushScreen(dirScreen);
  // If STA creds are configured, show the join result on boot (over the splash -> pops to the list).
  // WiFi.begin() already fired in RubberNugget::init(); the screen just polls + reports it.
  if (gStaSsid.length()) nuggetInterface->pushScreen(new WifiJoinScreen(gStaSsid));
  nuggetInterface->pushScreen(splashScreen);
  nuggetInterface->start();

}

void loop() { return; }
