# Building the Bad Nugget for the Bluetooth Nugget (ESP32-S3)

The committed `Dockerfile` still targets the original HakCat **ESP32-S2** (`-b esp32:esp32:esp32s2`).
This port is retargeted to the **ESP32-S3** LOLIN S3 Mini ("Bluetooth Nugget"), TinyUSB-OTG. Use the
FQBN + command below — this is the recipe that produced every S3 build in this tree.

## Toolchain
- `arduino-cli` with the `esp32:esp32` core **pinned to `3.0.0-alpha3`** (IDF 5.1). This is REQUIRED:
  the EspTinyUSB fork's `esptinyusb.cpp` uses `hal/usb_hal.h` + `usb_hal_init()`, which were **removed
  in IDF 5.x** (core `3.1+`). On a newer core the build fails with `fatal error: hal/usb_hal.h`.
- **Core-drift gotcha (2026-07):** other work on this machine bumped the shared arduino-cli esp32 core
  to `3.3.10`, which breaks this build. arduino-cli keeps only ONE core version, so to build:
  `arduino-cli core install esp32:esp32@3.0.0-alpha3` (from staging cache, fast), build, then
  `arduino-cli core install esp32:esp32@3.3.10` to restore. `~/firmware-lab/.../scratchpad/build_all.sh`
  does the alpha3 install + compile + merge; restore 3.3.10 afterward. `alpha3` ships `usb_hal.h` under
  `tools/esp32-arduino-libs/idf-release_v5.1-*/esp32s3/include/hal/include/hal/`.
- USB stack = RetiaLLC/EspTinyUSB fork installed under `~/Documents/Arduino/libraries/EspTinyUSB`
  (has S3 support; the pinned submodule commit `8eef676` is gone — use fork HEAD).
- **OS-detection patch (2026-07):** `src/usb_descriptors.cpp` in the fork carries a one-line Retia patch —
  a `volatile uint32_t g_usbCfgDescReqs` counter incremented in `tud_descriptor_configuration_cb`, read by
  the firmware via `extern` for `$_OS` fingerprinting. **Re-apply this if the EspTinyUSB fork is reinstalled**
  (otherwise the build fails to link `g_usbCfgDescReqs`).
- **Typing-reliability patch (2026-07):** `src/device/hid/hidcomposite.cpp` `HIDcomposite::sendKey()` now
  spins (bounded, ~60ms) for `tud_hid_ready()` instead of returning false. The stock version dropped the
  key when the endpoint was busy, and `sendString()` bailed on that, so on slower hosts a `STRING`/`TYPE`
  typed only its first character. Re-apply if the fork is reinstalled.

## FQBN + build
```bash
FQBN="esp32:esp32:lolin_s3_mini:USBMode=default,CDCOnBoot=default,MSCOnBoot=default,PartitionScheme=defaultffat"
cd src/RubberNugget
arduino-cli compile --fqbn "$FQBN" --output-dir /tmp/badnugget-out .
# -> /tmp/badnugget-out/RubberNugget.ino.bin (the app), .bootloader.bin, .partitions.bin
```
- `USBMode=default` = TinyUSB-OTG (the composite CDC + MSC + HID). **Not** `hwcdc` — the whole point
  is the composite. Enumerates as `05ac:020b` (Apple VID from the HID); `303a:1001` JTAG disappears.
- `CDCOnBoot=default` / `MSCOnBoot=default`: the sketch's EspTinyUSB drives CDC/MSC/HID itself; do
  **not** let the Arduino core also bring them up.
- Keyboard + mouse must stay on ONE HID interface (`HIDcomposite`) — a second HID interface blows the
  OTG endpoint budget (duplicate EP `0x84` → `SET_CONFIGURATION` STALL → every interface dies). See
  the `esp32-native-usb-recovery` skill.

## Two deploy targets

**A. Standalone factory image** (flash at `0x0`, replaces everything):
```bash
BOOT_APP0=$(find ~/.platformio/packages/framework-arduinoespressif32*/tools/partitions/boot_app0.bin | head -1)
esptool --chip esp32s3 merge-bin -o dist/badnugget-bluetoothnugget.factory.bin \
  --flash-mode dio --flash-freq 80m --flash-size 4MB \
  0x0 /tmp/badnugget-out/RubberNugget.ino.bootloader.bin \
  0x8000 /tmp/badnugget-out/RubberNugget.ino.partitions.bin \
  0xe000 "$BOOT_APP0" \
  0x10000 /tmp/badnugget-out/RubberNugget.ino.bin
```

**B. Auto-recovering candidate in `ota_0`** (the R2 architecture — preferred for the bench):
the app runs from `ota_0` under the `partitions_ota.csv` scheme (rescue in `factory`). Deploy the raw
`RubberNugget.ino.bin` to `0x100000` and boot via the rescue's `B ota`. `src/RubberNugget/src/
recovery_fixed.cpp` gives it the boot-counter + watchdog auto-revert. Full flow: **nugget-autorecovery**
skill. The app finds its data partitions (`ffat`) by name, so the `defaultffat` build table above is
fine even though the on-chip table is `partitions_ota.csv`.

## Reflash caveat
TinyUSB composite → the workbench portal's `default-reset` can't flash it (RTS/DTR is a no-op on native
USB). Recover/reflash via the **`BCM17`→EN reset wire** + `~/dut_reset.sh`, a **1200-baud touch** to
reach ROM download, or the resident rescue. See the `nugget-autorecovery` + `esp32-native-usb-recovery`
skills.
