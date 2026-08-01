// recovery_fixed.h — corrected R2 interface (see WORKBENCH-R2-INVESTIGATION.md).
// Supersedes recovery.h. Same three-line adoption as before, plus two optional accessors:
//
//   #include "src/recovery_fixed.h"
//   recoveryBegin();              // FIRST statement in setup()
//   recoveryKick();               // every iteration of the main run loop (and inside payload delays)
//
// The important behavioural difference vs. recovery.h: recovery restarts no longer route through
// EspTinyUSB's persist-shutdown handler, so they land on the rescue instead of ROM download mode.
#pragma once
#include <stdint.h>

// Why the last recovery restart happened. Retained in RTC across the reset so the next boot — and,
// after a revert, the rescue — can report the cause instead of just the outcome.
enum recovery_reason_t : uint32_t {
  REC_REASON_NONE          = 0,
  REC_REASON_WATCHDOG      = 1,   // heartbeat stopped: UI/run loop wedged
  REC_REASON_BOOTLOOP      = 2,   // REC_MAX_BOOTS boots without ever being marked healthy
  REC_REASON_REVERT_FAILED = 3,   // could not point otadata at `factory` (missing/unwritable)
};

// Call as the FIRST statement in setup(), before any USB / TinyUSB / radio init.
void recoveryBegin();

// Call every iteration of the main UI/run loop — and from inside the payload interpreter's
// step/delay loop, so a long-running payload does not look like a wedge (F4).
void recoveryKick();

// Reason for the most recent recovery restart (REC_REASON_NONE on a clean power-on).
// Surface this in the rescue's `?` status so the bench can log *why* it reverted.
uint32_t recoveryLastReason();

// Clear the boot counter. Call from the rescue's `B ota` handler: an operator-commanded boot into
// the candidate should always start from a clean slate, since a reflash does not power-cycle the
// board and the RTC-retained counter would otherwise carry over (F5).
void recoveryClearCounter();
