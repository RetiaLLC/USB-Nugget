# Porting the USB Nugget (Bad Nugget) from ESP32-S2 to ESP32-S3

The upstream HakCat USB-Nugget / RubberNugget targets an **ESP32-S2**. This port retargets it to
the **ESP32-S3 "Bluetooth Nugget"** (LOLIN S3 Mini). Both chips do native USB with TinyUSB, so the
app logic carries over — but the toolchain, USB descriptor budget, pin map, display, and the
flash/recovery story all needed work. Here's the short version of what was actually required.

## 1. Build target + toolchain
- **FQBN:** `esp32:esp32:lolin_s3_mini:USBMode=default,CDCOnBoot=default,MSCOnBoot=default,PartitionScheme=defaultffat`
  (upstream Dockerfile used `-b esp32:esp32:esp32s2`).
  - `USBMode=default` = TinyUSB-OTG composite (**not** `hwcdc`). `CDCOnBoot/MSCOnBoot=default` — the
    sketch's EspTinyUSB drives CDC/MSC/HID itself; don't let the Arduino core also bring them up.
- **esp32 arduino core pinned to `3.0.0-alpha3` (IDF 5.1).** This is mandatory: the EspTinyUSB fork's
  `esptinyusb.cpp` uses `hal/usb_hal.h` + `usb_hal_init()`, which were **removed in IDF 5.x** (core
  `3.1+`). On a newer core the build dies with `fatal error: hal/usb_hal.h`. arduino-cli keeps only one
  core version, so if the shared core has drifted, install alpha3, build, then reinstall the newer one.
- **USB stack:** the RetiaLLC/EspTinyUSB fork (has S3 support) under `~/Documents/Arduino/libraries`.

## 2. USB composite — the endpoint-budget trap
The device enumerates as a composite: **CDC (serial) + MSC (payload drive) + HID (keyboard+mouse)**,
advertising `05ac:020b` (Apple VID, inherited from the HID descriptor).
- **Keyboard and mouse must share ONE HID interface** (`HIDcomposite`), not two. A second HID interface
  overflows the S3 USB-OTG endpoint budget → a **duplicate EP `0x84`** → the host `SET_CONFIGURATION`
  **STALLs** and *every* interface dies (no CDC, no MSC, no HID). Registering all interfaces before the
  slow Wi-Fi init also matters, so the host enumerates the full composite in one pass.
- Set the HID base endpoint explicitly (`hid.setBaseEP(3)`) so CDC/MSC/HID don't collide.

## 3. Pin map + peripherals (S2 → S3 GPIO)
- **NeoPixel ears:** GPIO **10**, **2** pixels, `NEO_RGB`.
- **Buttons:** UP 13, DOWN 18, LEFT 11, RIGHT 12, A 44, B 43.
- **Display:** upstream S2 used an **SH1106**; the S3 board uses an **SSD1306** (128×64 I²C) — swap the
  driver and confirm orientation (this board reads right-side-up, no vertical flip).

## 4. Filesystem / payload storage
- `PartitionScheme=defaultffat` → a **FAT filesystem on flash via the wear-leveling layer**, mounted with
  `esp_vfs_fat_spiflash_mount(format_if_mount_failed=true)` and **exposed to the host as the MSC drive**.
  The same partition is read by the firmware (FatFs `f_open`/`f_opendir`) and by the USB host — they share
  the WL layer, so they stay consistent. A corrupt FAT presents as "empty but full" (0 files, `ENOSPC`);
  fix with a full `erase-flash` so the firmware formats a clean volume on next boot.

## 5. Flashing & recovery on native USB (the part that bites)
The S3 has **two** USB download paths, which the S2 lacks — this is the biggest operational difference:
- **`303a:1001`** USB-Serial-JTAG (built-in). A *blank* chip lands here; flash it with
  `esptool --before default-reset` (DTR/RTS reset **works** on this interface).
- **`303a:0009`** USB-OTG ROM DFU. Reached from a running TinyUSB app.
- **`05ac:020b`** — the app running (composite up).

Key facts:
- A running TinyUSB build **ignores esptool's `default-reset`** (RTS is a no-op on the OTG path). To flash,
  first drop it to ROM download via a **1200-baud touch**, the firmware's **`~R` serial command**
  (sets `RTC_CNTL_FORCE_DOWNLOAD_BOOT`), or a **hardware EN reset**, then `--before no-reset`.
- `esp_restart()` runs EspTinyUSB's shutdown handler, which sets `FORCE_DOWNLOAD_BOOT` — so a warm reset
  can re-enter download mode. That RTC latch survives an EN pulse; clear it with
  `esptool write-mem 0x6000812C 0x0` (or a true power cycle) before booting the app.
- Reliable large-image flashing over a flaky link: run esptool detached and retry from a clean reset.

## TL;DR
Retarget the FQBN, **pin the core to `3.0.0-alpha3`** (IDF 5.1 for `usb_hal.h`), keep **keyboard+mouse on
one HID interface** to fit the S3 endpoint budget, remap the GPIOs, swap **SH1106→SSD1306**, use the
**`defaultffat`** partition for the MSC payload drive, and learn the **`303a:1001` vs `303a:0009` vs
`05ac:020b`** flash/recovery dance (1200-baud touch / `~R` / EN reset, and clear `FORCE_DOWNLOAD_BOOT`).
