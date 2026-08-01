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
  server.send(200, "text/html", String(INDEX));
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

void webserverInit(void *p) {
  while (1) {
    remoteService();                  // deferred ~S screen dump (task ctx so CDC TX can drain)
    dnsServer.processNextRequest();   // captive portal
    server.handleClient();
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
  server.onNotFound(handleCaptive);   // captive portal + friendly 404

  dnsServer.start(53, "*", WiFi.softAPIP());   // hijack DNS so any hostname -> our UI
  server.begin();

  xTaskCreate(webserverInit, "webapptask", 12 * 1024, NULL, 5, &webapp); // create task priority 1
  nuggetInterface = new NuggetInterface;
  NuggetScreen* dirScreen = new DirScreen("/");
  NuggetScreen* splashScreen = new SplashScreen(1500);
  nuggetInterface->pushScreen(dirScreen);
  nuggetInterface->pushScreen(splashScreen);
  nuggetInterface->start();

}

void loop() { return; }
