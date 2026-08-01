#pragma once
#include <Arduino.h>
class SSD1306Wire;

// Serial remote-control (over the USB CDC): lets the workbench inject button presses,
// read the exact OLED framebuffer, and reboot into the bootloader — for autonomous testing.
int remotePopBtn();                 // next queued remote button press, or BTN_NONE (NuggetInputs::getInput)
extern volatile bool g_abortReq;    // set by ~PB/~PL; the payload runner consumes it to abort (wantStop)
extern SSD1306Wire* g_display;      // set by NuggetInterface so we can dump its 1024-byte buffer
extern volatile bool g_testHang;    // ~H sets this; the UI loop spins on it to prove the R2 watchdog

// Feed raw CDC bytes; handles '~' framed commands:
//   ~P<U|D|L|R|A|B>  press a button      ~S  dump screen (-> "~SCR <2048 hex>\n")
//   ~K<pin>          unlock BLE control  ~O<W|M|L|A|I|?>  force / clear the $_OS override
//   ~G               regenerate the BLE PIN (persisted) -> "~PIN <n>\n"
//   ~R               reboot to bootloader (download mode)
//   ~H               TEST: wedge the UI task -> R2 watchdog should auto-revert to the rescue
// Script manager (payloads on the FFat drive), works over CDC and BLE:
//   ~L               list payloads         -> "~LST /a.dd,/b.dd,\n"
//   ~F<path>         read a payload        -> "~FIL <path>:<len>\n<len bytes>"
//   ~W<path>:<len>   write a payload; the next <len> raw bytes are the body -> "~OK write <path>\n"
//   ~X<path>         run a payload by path -> "~OK run\n"
//   ~D<path>         delete a payload      -> "~OK del\n"
// BLE clients are LOCKED until they send a matching ~K (USB/CDC is trusted, never gated).
// transport ids for reply routing (1 = USB CDC, 2 = BLE NUS)
#define REMOTE_TR_CDC 1
#define REMOTE_TR_BLE 2
void remoteFeed(const uint8_t* data, int len, int transport = REMOTE_TR_CDC);
void remoteService();   // run from a task loop: performs any deferred ~S dump + script op
void remoteBleReset();  // clear BLE auth (call on BLE disconnect) so the next client must re-send ~K

// Reply back over whichever transport the current command arrived on (routed by remoteService()).
// Used by the script-op functions below so they don't need to know CDC-vs-BLE.
void remoteReply(const char* s);
void remoteReplyBytes(const uint8_t* p, int n);

// Script-manager ops — IMPLEMENTED in RubberNugget.cpp (needs the FS + UI), CALLED from
// remoteService() (task ctx: flash reads/writes and injectScreen() aren't callback-safe).
void scriptList();
void scriptRead(const char* path);
void scriptWrite(const char* path, const char* body, int len);
void scriptRun(const char* path);
void scriptDelete(const char* path);
