# Bad Nugget — finish-the-port checklist

Single repo, `src/board_config.h` selects the variant. Targets:
**USB Nugget** (ESP32-S2, auto by chip), **Bluetooth Nugget** (S3 default), **Nibble Zero** (`-DNIBBLE_ZERO`).
Goal of this pass: finish + bench-verify the **Nibble Zero** variant and make the whole thing reliable.

Legend: `[x]` done · `[~]` done, needs hardware confirmation · `[ ]` to do · 🌅 = needs the connected board (morning)

---

## 0. Architecture (confirmed)
- [x] One tree, `board_config.h` defines pins / `NuggetDisplay` typedef / `BOARD_HAS_BLE` / `BOARD_HAS_AB` / `OLED_FLIP` per board.
- [x] Every source keys off those macros; no per-board inline edits left.

## 1. Nibble Zero pins & board config
- [x] OLED: SSD1306, `OLED_ADDR 0x3C`, `SDA 8 / SCL 7`, `OLED_FLIP 1` (mounted 180° — confirmed upside-down then fixed).
- [x] **U/D/L/R = Meshtastic `nibble-zero-connect` trackball**: `UP 42 · DOWN 41 · LEFT 40 · RIGHT 45`.
      (Was UP=40/LEFT=42 — **UP↔LEFT swap fixed** this pass.)
- [x] **A = SW1/GPIO1** (operator confirmed "GPIO1 is the button"), **B = SW2/GPIO2**. Matches Meshtastic `BUTTON_PIN 1` / `TB_PRESS 2` + KiCad SW1/SW2.
- [x] `NUG_BTN_COUNT 6`, `BOARD_HAS_AB 1`, `BOARD_HAS_BLE 1`.
- [x] NeoPixel **GPIO21** — the base ESP32-S3-Zero onboard WS2812 (operator's call; matches Meshtastic `NEOPIXEL_DATA 21`), count 1.
- [x] GPIO45 (RIGHT) strapping-pin note (VDD_SPI; internal pull-up ok at runtime).
- [ ] Official pinout saved for reference (REV01): SW1..6 = GPIO 1,2,40,41,42,45; ear LED = GPIO39; radio pins unused by BadUSB.

## 2. USB enumeration race (the one real defect)
- [x] Root cause: `fat1.begin()` (MSC) fires `tusb_init()` **before** CDC+HID register + before `deviceID(05ac:020b)`.
      A host enumerating in that window sees an MSC-only/partial composite ⇒ `303a:0002`, "invalid descriptor", dead USB.
- [x] Fix implemented: `tud_disconnect(); delay(150); tud_connect();` after **all** classes register (RubberNugget.cpp).
- [ ] 🌅 **Flash-verify reliability**: reboot the board ≥5× ⇒ **always** `05ac:020b` (4 interfaces, CDC+MSC+HID). No `303a:0002`.
- [ ] Check the **shipped** `dist/badnugget-bluetoothnugget-v1.factory.bin` for the same race; rebuild + republish if so.
- [x] Display geometry: all three boards are **128×64** ⇒ no screen reflow, `~S` dump is 1024 B ⇒ phone mirror unaffected. (Skill's "128×32" was wrong.)

## 3. Build verification (no hardware needed — done tonight)
- [x] **Nibble Zero** rebuild with corrected pins ⇒ compiles clean (94%); `dist/badnugget-nibble-zero.factory.bin` ready.
- [x] **Bluetooth Nugget** (S3 default, no `-D`) builds clean (94%) ⇒ refactor regression check PASSED.
- [ ] **USB Nugget** (S2) build — separate FQBN (esp32s2) + bootloader **0x1000** + SH1106; skill §6 says done+verified. Smoke-build to confirm the refactor. 🌅/later.
- [x] Build recipe: FQBN `esp32:esp32:lolin_s3_mini:USBMode=default,CDCOnBoot=default,MSCOnBoot=default,PartitionScheme=defaultffat`,
      core **3.0.0-alpha3**, `-DNIBBLE_ZERO` (nibble) / none (bt-nugget), merge at 0x0/0x8000/0xe000/0x10000, restore core 3.3.10 after.

## 4. Bench verification — 🌅 morning, board connected
Flash by **MAC**, never blind. Reliable reflash path once running: firmware `~R` (sets FORCE_DOWNLOAD_BOOT) → esptool.
- [ ] Flash `badnugget-nibble-zero.factory.bin` (erase) to the connected board; MAC-guarded.
- [ ] OLED: menu renders **right-side-up**, readable (nugremote `png` dump).
- [ ] HID: run `hello.txt` → nugwatch shows full string typed (no first-char-only bug).
- [ ] Buttons (physical press-test): far-left=A=select, next=B=back, d-pad U/D/L/R scroll correctly per Meshtastic mapping.
- [ ] NeoPixel lights (LED command / boot), visible pixel confirmed.
- [ ] MSC drive mounts (payloads present); CDC remote (`~L/~S/~P`) works; BLE remote (`Nugget`, PIN) connects.
- [ ] Enumeration reliability (§2) across ≥5 reboots.
- [ ] Web UI + captive portal + `nugget.local` reachable over the AP.

## 5. Release / publish (after bench pass)
- [ ] Commit `board_config.h` + race fix; PR on `RetiaLLC/USB-Nugget`.
- [ ] Cut a GitHub Release with the three verified factory bins.
- [ ] scriptkitty card: `product_line: nibble` / model "Nibble Zero", `mcu: esp32-s3`, `binary_source: release` pinned to sha256.
      Bench-validate the exact merged bin **before** the public card.
- [ ] Update memory (`nibble-zero-port`, `bad-nugget-badusb`) + the `bad-nugget-port` skill (pins, race fix, reflash).

## Resolved
- NeoPixel = **GPIO21** (base ESP32-S3-Zero onboard pixel). ✅
- A = **GPIO1** ("GPIO1 is the button"), B = GPIO2. ✅
- U/D/L/R = Meshtastic trackball (UP42/DOWN41/LEFT40/RIGHT45). ✅

## Still open
- Ship all three variants in one Release, or Nibble Zero first?
- 🌅 physical press-test is now just a sanity check (not a decision) — mapping is locked to operator's confirmations.
