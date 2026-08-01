// recovery_fixed.cpp — corrected R2 boot-counter + watchdog.
// Drop-in replacement for recovery.cpp; see WORKBENCH-R2-INVESTIGATION.md for the analysis.
//
// The load-bearing fix (F1): EspTinyUSB registers usb_persist_shutdown_handler() from its base
// constructor — which, because CDCusb/HIDcomposite/FlashUSB are file-scope globals in
// RubberNugget.cpp, runs BEFORE setup(). That handler sees `usb_persist_mode` at its default of
// RESTART_BOOTLOADER and writes RTC_CNTL_FORCE_DOWNLOAD_BOOT on EVERY esp_restart(). So the old
// revert path rebooted into ROM download mode instead of the rescue, and needed the host to issue a
// second reset to complete. _recSafeRestart() below neutralizes that for recovery restarts only —
// the 1200-baud touch in cdcusb.cpp still relies on the RESTART_BOOTLOADER default and is untouched.
//
// Structural change (per the results doc §3): the watchdog no longer writes flash. It records a
// reason and restarts; the otadata write happens in recoveryBegin(), single-core, before USB and the
// scheduler are live. A one-time wedge therefore resets and continues on the candidate; only a
// persistent loop falls all the way back to the rescue.
#include "recovery_fixed.h"
#include "esp_attr.h"
#include "esp_idf_version.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_private/system_internal.h"   // esp_restart_noos() — handler-free restart for reverts
#include <Arduino.h>

// EspTinyUSB's global persist mode (esptinyusb.cpp:236). Declared `int` and non-static there, so
// this extern binds to it. The Arduino core has a same-named global in esp32-hal-tinyusb.c:384 but
// it is `static` (file-local) and defaults to RESTART_NO_PERSIST, so there is no clash and nothing
// else can re-latch download boot behind our back.
extern int usb_persist_mode;
// Deliberately NOT named RESTART_NO_PERSIST: that is an enumerator in esptinyusb.h, and a macro of
// the same name would rewrite the enum declaration to `0,` and break the build for anyone who later
// includes that header in this translation unit.
#define REC_RESTART_NO_PERSIST 0   // == restart_type_t::RESTART_NO_PERSIST (first enumerator)

#ifndef REC_MAX_BOOTS
#define REC_MAX_BOOTS   4        // consecutive un-marked-good boots before reverting to rescue
#endif
#ifndef REC_HANG_MS
#define REC_HANG_MS     12000    // steady-state heartbeat gap that counts as a hang
#endif
#ifndef REC_BOOT_GRACE_MS
#define REC_BOOT_GRACE_MS 60000  // F3: allow for FFat format + WiFi join + splash before the first kick
#endif
#ifndef REC_GOOD_MS
#define REC_GOOD_MS     10000    // run this long without incident -> declare this boot healthy
#endif
#ifndef REC_MAX_REVERT_TRIES
#define REC_MAX_REVERT_TRIES 2   // F6: give up rather than loop forever on a failing revert
#endif
#define REC_MAGIC       0xB007C0DEu

// RTC_NOINIT: retained across warm resets / crashes / WDT resets, cleared only by real power loss.
RTC_NOINIT_ATTR static uint32_t _recBootCount;
RTC_NOINIT_ATTR static uint32_t _recMagic;
RTC_NOINIT_ATTR static uint32_t _recImageId;      // F5: identity of the image the counter belongs to
RTC_NOINIT_ATTR static uint32_t _recReason;       // why the last restart happened (recovery_reason_t)
RTC_NOINIT_ATTR static uint32_t _recRevertTries;  // failed esp_ota_set_boot_partition attempts

static volatile uint32_t _recLastKick;
static volatile bool     _recKicked;              // has the run loop ever kicked? (boot grace vs steady)

// First 4 bytes of the running app's ELF SHA — changes on every rebuild, so a reflash (which does
// NOT power-cycle the board) can be distinguished from a reboot of the same image.
// Both Arduino cores installed on this machine are supported: PlatformIO's 2.0.17 (IDF 4.4) uses
// esp_ota_get_app_description(); the Arduino-IDE 3.0.0-alpha3 (IDF 5.1) deprecates it in favour of
// esp_app_get_description() from esp_app_desc.h.
#if ESP_IDF_VERSION_MAJOR >= 5
#include "esp_app_desc.h"
#define _REC_APP_DESC() esp_app_get_description()
#else
#define _REC_APP_DESC() esp_ota_get_app_description()
#endif

static uint32_t _recCurrentImageId() {
  const esp_app_desc_t* d = _REC_APP_DESC();
  if (!d) return 0;
  return ((uint32_t)d->app_elf_sha256[0]) | ((uint32_t)d->app_elf_sha256[1] << 8) |
         ((uint32_t)d->app_elf_sha256[2] << 16) | ((uint32_t)d->app_elf_sha256[3] << 24);
}

// TRANSIENT restart — used when a wedge is detected after the app is fully up (USB begun). esp_restart()
// runs shutdown handlers, so EspTinyUSB tears its (begun) composite USB down cleanly; we neutralize the
// persist mode first so that teardown does NOT latch download-boot (F1). Proven on the bench for the
// OTG->OTG reboot (single-wedge test: board returns to the candidate, zero 303a:0009).
static void _recSafeRestart(uint32_t reason) {
  _recReason = reason;
  usb_persist_mode = REC_RESTART_NO_PERSIST;    // make the shutdown handler a no-op re: download-boot
  REG_WRITE(RTC_CNTL_OPTION1_REG, 0);           // clear any pre-existing download latch
  esp_restart();
}

// REVERT restart — issued from recoveryBegin(), i.e. BEFORE RubberNugget::init() brings USB up. Here
// esp_restart() is wrong: it runs EspTinyUSB's shutdown handler against a NOT-YET-BEGUN USB, which left
// the OTG->USB-Serial-JTAG switch dark (clean disconnect, no re-enumeration — verified via dmesg).
// esp_restart_noos() performs the same low-level chip reset WITHOUT running shutdown handlers, so the
// handler can neither mangle the un-begun USB nor latch download-boot; the hardware reset re-inits the
// PHY and the rescue's USB-Serial-JTAG enumerates. RTC_NOINIT (counter/reason) survives.
static void _recRevertRestart(uint32_t reason) {
  _recReason = reason;
  REG_WRITE(RTC_CNTL_OPTION1_REG, 0);
  esp_restart_noos();
}

// Point otadata at the resident rescue. Flash write — only ever called from recoveryBegin(),
// before the scheduler and USB are up, so no other core is executing flash-resident code.
static bool _recSetBootToFactory() {
  const esp_partition_t* f =
      esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
  if (!f) return false;
  return esp_ota_set_boot_partition(f) == ESP_OK;
}

static void _recWatchdogTask(void*) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(500));
    uint32_t limit = _recKicked ? REC_HANG_MS : REC_BOOT_GRACE_MS;   // F3
    if ((uint32_t)(millis() - _recLastKick) > limit) {
      // No flash write here (that was the unsafe cross-core op). Just restart with a reason;
      // recoveryBegin() decides on the next boot whether this is a one-off or a pattern.
      _recSafeRestart(REC_REASON_WATCHDOG);
    }
  }
}

void recoveryBegin() {
  // A stale FORCE_DOWNLOAD_BOOT (host-forced download, interrupted 1200-baud touch) would send the
  // NEXT reset back into ROM. Clear it now that we know we booted an application successfully.
  REG_WRITE(RTC_CNTL_OPTION1_REG, 0);

  uint32_t img = _recCurrentImageId();
  if (_recMagic != REC_MAGIC) {                       // cold power-on: RTC RAM is garbage
    _recMagic = REC_MAGIC; _recBootCount = 0; _recReason = REC_REASON_NONE; _recRevertTries = 0;
    _recImageId = img;
  } else if (_recImageId != img) {                    // F5: different image was flashed since last boot
    _recBootCount = 0; _recRevertTries = 0; _recImageId = img;
  }

  _recBootCount++;

  if (_recBootCount >= REC_MAX_BOOTS) {
    // Persistent crash/hang loop -> hand control back to the rescue. Flash write is safe here.
    // Use the handler-free _recRevertRestart(): we are pre-USB-init, so esp_restart() would run the
    // shutdown handler against a not-yet-begun USB and land the OTG->JTAG switch dark.
    if (_recSetBootToFactory()) {
      _recBootCount = 0;
      _recRevertRestart(_recReason ? _recReason : REC_REASON_BOOTLOOP);
    }
    // F6: the revert itself failed (no factory partition, or otadata write rejected). Do not zero
    // the counter and spin forever — try a couple of times, then stop restarting and stay up in
    // whatever degraded state this image can manage, so the JTAG/CDC serial has a chance to appear
    // and the bench can see the reason.
    if (++_recRevertTries < REC_MAX_REVERT_TRIES) {
      _recRevertRestart(REC_REASON_REVERT_FAILED);
    }
    _recReason = REC_REASON_REVERT_FAILED;
    _recBootCount = 0;   // let the image run; the bench will see REVERT_FAILED in the status string
  }

  _recLastKick = millis();
  _recKicked   = false;
  // Pin to core 0 (Arduino loop runs on core 1) at max priority so a wedged UI core can't starve it.
  // TODO(F2): this task is flash-resident, so a cache-disable wedge stalls it too. Add the hardware
  // RTC watchdog (hal/wdt_hal.h, WDT_RWDT on the S3) armed to ~30 s and fed from recoveryKick() as
  // the cache-independent backstop. Verify the API against the installed Arduino core version.
  xTaskCreatePinnedToCore(_recWatchdogTask, "recwdt", 3072, NULL, configMAX_PRIORITIES - 1, NULL, 0);
}

void recoveryKick() {
  uint32_t now = millis();
  _recLastKick = now;
  _recKicked   = true;                 // switch the watchdog from boot-grace to steady-state
  static uint32_t bootT0 = 0;
  if (bootT0 == 0) bootT0 = now ? now : 1;
  if (_recBootCount != 0 && (uint32_t)(now - bootT0) > REC_GOOD_MS) {
    _recBootCount   = 0;               // this boot is healthy; next boot starts fresh
    _recRevertTries = 0;
  }
  // TODO(F4): a running Ducky payload executes inside screen->_update() and never returns here, so
  // a payload with a long DELAY trips REC_HANG_MS and reverts a healthy board. Call recoveryKick()
  // from the payload interpreter's step/delay loop (preferred — keeps coverage during payloads).
}

uint32_t recoveryLastReason() { return _recReason; }

void recoveryClearCounter() {         // called by the rescue's `B ota` path: deliberate boot = clean slate
  _recBootCount = 0;
  _recRevertTries = 0;
  _recReason = REC_REASON_NONE;
}
