#include "remote.h"
#include <string.h>
#include <stdlib.h>
#include "SSD1306Wire.h"
#include "interface/lib/NuggetInterface.h"
#include "cdcusb.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_system.h"

extern CDCusb CDCUSBSerial;

SSD1306Wire* g_display = nullptr;
volatile bool g_testHang = false;
volatile bool g_abortReq = false;        // ~PB/~PL during a payload -> stop it (runner wantStop)
volatile int g_screenDumpReq = 0;        // ~S sets this to the requesting transport; remoteService() dumps
static int g_curTransport = REMOTE_TR_CDC;        // where the current reply is routed (CDC or BLE)
extern void nugBleTx(const uint8_t* p, int n);    // BLE NUS notify (RubberNugget.cpp); no-op if BLE down
extern String gApPin;                             // BLE control PIN (per-device, persisted; on the Connect screen)
extern String blePinRegen();                      // ~G: rotate + persist the PIN, returns the new value
extern volatile char g_forceOS;                   // manual $_OS override (nuggetOS() reads it): 0=auto else W/M/L/A/I
static volatile bool g_bleAuthed = false;         // a BLE client must send ~K<pass> before it can control anything
void remoteBleReset() { g_bleAuthed = false; }    // called on BLE disconnect so the next client re-authenticates

// Deferred script-manager ops (payloads on FFat). Handled in remoteService() (task ctx: flash + UI
// aren't callback-safe); a one-slot request set from the CDC/BLE callback.
enum { SCR_NONE = 0, SCR_LIST, SCR_READ, SCR_RUN, SCR_DEL, SCR_WRITE, SCR_REGEN };
static volatile int g_scriptOp = SCR_NONE;
static int  g_scriptTr = REMOTE_TR_CDC;
static char g_scriptName[80];
static char g_wbuf[8192];             // ~W body buffer (payloads are small; the cap keeps RAM bounded)
static int  g_wcapRemaining = 0;      // >0 while capturing raw ~W body bytes in remoteFeed()
static int  g_wcapIdx = 0;

// Button-press QUEUE. A single shared slot dropped batched presses (several ~PD arriving in one
// USB packet overwrote each other), so remote navigation skipped steps. A ring buffer lets every
// queued press reach getInput() one at a time.
#define BTNQ_N 24
static volatile uint8_t btnq[BTNQ_N];
static volatile uint8_t btnqHead = 0, btnqTail = 0;
static void btnqPush(uint8_t b) {
  uint8_t n = (uint8_t)((btnqHead + 1) % BTNQ_N);
  if (n != btnqTail) { btnq[btnqHead] = b; btnqHead = n; }   // full -> drop newest
}
int remotePopBtn() {   // returns BTN_NONE when empty
  if (btnqHead == btnqTail) return BTN_NONE;
  int b = btnq[btnqTail]; btnqTail = (uint8_t)((btnqTail + 1) % BTNQ_N);
  return b;
}

// Write everything in <=64B pieces (one USB-FS bulk packet), retrying when the CDC TX buffer is
// full. A large single write() wedges EspTinyUSB's CDC (observed: the 2KB framebuffer dump killed
// the endpoint and then the whole device). Capping each write() to the FIFO packet size fixes it.
static void cdcAll(const uint8_t* p, int n) {
  if (g_curTransport == REMOTE_TR_BLE) { nugBleTx(p, n); return; }   // route replies over BLE NUS
  int sent = 0, spins = 0;
  while (sent < n && spins < 4000) {
    int chunk = n - sent; if (chunk > 64) chunk = 64;
    int w = CDCUSBSerial.write(p + sent, chunk);
    if (w > 0) { sent += w; spins = 0; } else { delay(1); spins++; }
  }
}
static void cdc(const char* s) { cdcAll((const uint8_t*)s, strlen(s)); }

// Public reply helpers so the script-op functions (in RubberNugget.cpp) can answer over whichever
// transport the request came in on — remoteService() sets g_curTransport before calling them.
void remoteReply(const char* s) { cdc(s); }
void remoteReplyBytes(const uint8_t* p, int n) { cdcAll(p, n); }

// Dump the exact 128x64 mono framebuffer (1024 bytes) as hex, so the workbench renders
// the pixel-perfect screen instead of squinting at a webcam.
static void dumpScreen() {
  if (!g_display) { cdc("~SCR none\n"); return; }
  static char hx[2048];
  const char* H = "0123456789abcdef";
  uint8_t* fb = g_display->buffer;
  for (int i = 0; i < 1024; i++) { hx[i*2] = H[fb[i] >> 4]; hx[i*2+1] = H[fb[i] & 0xf]; }
  cdc("~SCR ");
  cdcAll((const uint8_t*)hx, 2048);
  cdc("\n");
}

static void handleCmd(const char* c) {
  // BLE is wireless and open to anyone in range, so gate it behind the passphrase. USB (CDC) needs
  // physical access, so it's trusted and never gated — the bench tooling drives it without a key.
  if (g_curTransport == REMOTE_TR_BLE && !g_bleAuthed && c[0] != 'K') { cdc("~LOCKED\n"); return; }
  switch (c[0]) {
    case 'K':  // ~K<pin>: unlock BLE control. PIN = the per-device code shown on the Connect screen.
      if (gApPin.length() == 0 || gApPin == String(c + 1)) { g_bleAuthed = true; cdc("~OK auth\n"); }
      else cdc("~BADKEY\n");
      break;
    case 'G':  // regenerate the BLE PIN — DEFERRED: it writes flash, which faults from the USB callback
      g_scriptOp = SCR_REGEN; g_scriptTr = g_curTransport;
      break;
    case 'O':  // ~O<W|M|L|A|I>: force $_OS for payloads; ~O? or ~O0 restores auto-detect
      g_forceOS = (c[1] == '?' || c[1] == '0' || c[1] == 0) ? 0 : c[1];
      cdc("~OK os\n");
      break;
    case 'P':  // press a button (queued; A->select/right, B->back/left). Fire-and-forget: no reply,
      switch (c[1]) {          // so rapid navigation never blocks on host-side reads.
        case 'U':            btnqPush(BTN_UP);    break;
        case 'D':            btnqPush(BTN_DOWN);  break;
        case 'L': case 'B':  btnqPush(BTN_LEFT);  g_abortReq = true; break;
        case 'R': case 'A':  btnqPush(BTN_RIGHT); break;
      }
      break;
    case 'S':  // screen dump — DEFERRED (heavy write can't drain from a callback ctx), routed back to
      g_screenDumpReq = g_curTransport;   // whichever transport (CDC/BLE) asked. remoteService() does it.
      break;
    case 'R':  // reboot into the bootloader (WiFi-independent recovery)
      cdc("~OK reboot-bl\n");
      delay(40);
      REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
      esp_restart();
      break;
    case 'H':  // TEST: request the UI task to wedge, so the R2 watchdog auto-reverts to the rescue
      cdc("~OK will-hang\n");
      g_testHang = true;   // NuggetInterface::start() spins on this -> heartbeat stops
      break;
    case 'L':  // list payloads (deferred; short reply)
      g_scriptOp = SCR_LIST; g_scriptTr = g_curTransport;
      break;
    case 'F':  // read payload: ~F<path>
      strncpy(g_scriptName, c + 1, sizeof(g_scriptName) - 1); g_scriptName[sizeof(g_scriptName) - 1] = 0;
      g_scriptOp = SCR_READ; g_scriptTr = g_curTransport;
      break;
    case 'X':  // run payload by path: ~X<path>
      strncpy(g_scriptName, c + 1, sizeof(g_scriptName) - 1); g_scriptName[sizeof(g_scriptName) - 1] = 0;
      g_scriptOp = SCR_RUN; g_scriptTr = g_curTransport;
      break;
    case 'D':  // delete payload: ~D<path>
      strncpy(g_scriptName, c + 1, sizeof(g_scriptName) - 1); g_scriptName[sizeof(g_scriptName) - 1] = 0;
      g_scriptOp = SCR_DEL; g_scriptTr = g_curTransport;
      break;
    case 'W': {  // write payload header: ~W<path>:<len>; the next <len> raw bytes are the body
      const char* colon = strrchr(c, ':');
      if (!colon) { cdc("~ERR wfmt\n"); break; }
      int wlen = atoi(colon + 1);
      int nlen = (int)(colon - (c + 1));
      if (wlen < 0 || wlen > (int)sizeof(g_wbuf) - 1 || nlen <= 0 || nlen >= (int)sizeof(g_scriptName)) { cdc("~ERR wlen\n"); break; }
      memcpy(g_scriptName, c + 1, nlen); g_scriptName[nlen] = 0;
      g_scriptTr = g_curTransport; g_wcapIdx = 0;
      if (wlen == 0) { g_wbuf[0] = 0; g_scriptOp = SCR_WRITE; }  // empty file, nothing to capture
      else g_wcapRemaining = wlen;                              // capture body -> remoteFeed sets SCR_WRITE
      break;
    }
  }
}

// Parse '~'-framed commands out of the CDC byte stream (payload data passes through untouched).
void remoteFeed(const uint8_t* data, int len, int transport) {
  g_curTransport = transport;   // route this command's replies back the way it came in
  static char line[80];
  static int idx = 0;
  static bool inCmd = false;
  for (int i = 0; i < len; i++) {
    char ch = (char)data[i];
    if (g_wcapRemaining > 0) {                     // ~W body: raw bytes, NOT '~'-framed (may contain '~')
      if (g_wcapIdx < (int)sizeof(g_wbuf) - 1) g_wbuf[g_wcapIdx++] = ch;
      if (--g_wcapRemaining == 0) { g_wbuf[g_wcapIdx] = 0; g_scriptTr = transport; g_scriptOp = SCR_WRITE; }
      continue;
    }
    if (ch == '~') { inCmd = true; idx = 0; continue; }
    if (!inCmd) continue;
    if (ch == '\n' || ch == '\r') { line[idx] = 0; if (idx) handleCmd(line); inCmd = false; idx = 0; continue; }
    if (idx < 79) line[idx++] = ch;
  }
}

// Call from a normal task loop (NOT the USB onData callback). Here CDCUSBSerial's TX can drain
// because the USB task runs concurrently, so the big framebuffer write completes instead of
// spinning and faulting the USB stack. Only the heavy ~S dump is deferred; ~P/~R stay immediate.
void remoteService() {
  if (g_screenDumpReq) { g_curTransport = g_screenDumpReq; g_screenDumpReq = 0; dumpScreen(); }
  if (g_scriptOp != SCR_NONE) {
    int op = g_scriptOp; g_scriptOp = SCR_NONE;
    g_curTransport = g_scriptTr;   // route the reply back to whoever asked (CDC or BLE)
    switch (op) {
      case SCR_LIST:  scriptList();                        break;
      case SCR_READ:  scriptRead(g_scriptName);            break;
      case SCR_RUN:   scriptRun(g_scriptName);             break;
      case SCR_DEL:   scriptDelete(g_scriptName);          break;
      case SCR_WRITE: scriptWrite(g_scriptName, g_wbuf, g_wcapIdx); break;
      case SCR_REGEN: { String p = blePinRegen(); String m = "~PIN " + p + "\n"; remoteReply(m.c_str()); break; }
    }
  }
}
