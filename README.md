# 🐱 Bad Nugget — BadUSB for the ESP32-S3 Bluetooth Nugget

The **Bad Nugget** is the [HakCat USB Nugget](https://usbnugget.com) / RubberNugget BadUSB firmware
**ported to the ESP32-S3 "Bluetooth Nugget"** (LOLIN S3 Mini). It's a cat-themed keystroke-injection
platform — plug it into a computer and it acts as a USB keyboard + mouse — with the ESP32-S3's native
USB and radio unlocking a wireless remote you don't get on the S2.

It enumerates as a composite **CDC + MSC + HID** device (`05ac:020b`), so it's a keyboard, a mouse, a
serial control channel, and a small USB drive all at once.

> ⚠️ **Authorized use only.** This is a security-research and red-team training tool. Only use it on
> systems you own or are explicitly authorized to test. You are responsible for how you use it.

## What's new on the S3

- **📡 Wireless remote (BLE + USB).** Drive the on-device menus, mirror the OLED live, and manage
  payloads over **Bluetooth LE** (Nordic UART) or **USB serial** — no cable to a host required. The
  companion **[Nugget Remote Android app](https://github.com/skickar/nugget-android)** gives you a
  d-pad, A/B buttons, a live screen view, and a full script editor on your phone.
- **🔒 Per-device PIN.** BLE control is gated behind a random 6-digit PIN generated on first boot,
  shown on the Connect screen, and persisted. USB is trusted (physical access). Rotatable in-app.
- **📝 Remote script manager.** List, read, **write, run, and delete** payloads over BLE or USB —
  author and fire payloads from your phone.
- **🧭 OS fingerprinting (`$_OS`).** Detects `WINDOWS` / `MACOS` / `IOS` / `LINUX` / `ANDROID` from the
  host's USB-enumeration behavior, with a manual override, so one payload can branch per target.
- **Reliability + UX fixes:** correct `STRING`/`TYPE` typing on slow hosts, `LOOP` auto-close, hold-**B**
  to abort a running payload, a `WAIT` status screen, and a cleaned-up payload list.

## Flash it

**Easiest — browser flasher (Chrome/Edge):** open **[scriptkitty.sh](https://scriptkitty.sh)**, pick
the **Bad Nugget** card, and click flash. No tools to install.

**Or with esptool** (grab `badnugget-bluetoothnugget-v1.factory.bin` from
[Releases](https://github.com/RetiaLLC/USB-Nugget/releases)):
```bash
esptool --chip esp32s3 write-flash 0x0 badnugget-bluetoothnugget-v1.factory.bin
```
To enter download mode: hold **BOOT**, tap **RESET**, release BOOT. After flashing, unplug/replug.

**Android app:** sideload `app-debug.apk` from the release (or build it from
[skickar/nugget-android](https://github.com/skickar/nugget-android)).

## Use it

- **On the device:** the d-pad browses payloads; **A** selects/runs, **B** goes back / aborts. The
  **Connect** screen (press **B** from the list) shows the Wi-Fi AP, `nugget.local`, the **BLE PIN**,
  and the detected OS.
- **Over Wi-Fi:** join the `Nugget AP` (password `nugget123`) and open **http://nugget.local** (or
  `192.168.4.1`) to create/edit/deploy payloads.
- **Over Bluetooth:** open the Android app, connect **BLE**, enter the PIN shown on the Connect screen,
  and drive everything wirelessly.

Payloads are plain `.txt` files on the Nugget's USB drive (or written over the remote).

## Scripting (DuckyScript / CatSpeak)

```
REM comment
STRING Hello, world!          // type text  (TYPE also works)
STRINGLN log in               // type + Enter
GUI r                         // modifier keys: GUI/CMD, CONTROL/CTRL, ALT, SHIFT, ENTER…
WAIT 2000                     // wait 2000 ms (DELAY is an alias) — shows a WAIT screen now
DEFAULT_WAIT 20               // delay inserted between every command

LOOP 3                        // repeat the block; auto-closes at end-of-file if no ENDLOOP
  STRING spam
  ENTER
ENDLOOP

IF_OS WINDOWS                 // branch on the detected host ($_OS)
  GUI r
  STRING powershell
ELSE
  STRING $_OS                 // $_OS expands to WINDOWS/MACOS/IOS/LINUX/ANDROID
END_IF

SCREEN status text            // draw text on the OLED
LED R                         // NeoPixel color: R G B C Y M W
LOCALE ES                     // keyboard layout: EN DE ES FR PT
MOUSEMOVE 40 0                // move the mouse (dx dy); MOUSECLICK / MOUSESCROLL / JIGGLE too
```

Hold **B** (or send an abort over the remote) to stop a running payload.

## Config

Create `.usbnugget.conf` on the Nugget drive to change the AP creds / keyboard identity:
```
network  = "Nugget AP"
password = "nugget123"
vid      = "0x05ac"
pid      = "0x20b"
```

## Build from source

This is an ESP32-S3 retarget with a **pinned toolchain** — see **[BUILD-S3.md](BUILD-S3.md)** for the
full recipe and **[PORT-S2-TO-S3.md](PORT-S2-TO-S3.md)** for the port write-up. In short: the
`esp32:esp32` Arduino core is pinned to **3.0.0-alpha3** (IDF 5.1 — newer cores drop `hal/usb_hal.h`
and the build fails), and the USB stack is the patched **[RetiaLLC/EspTinyUSB](https://github.com/RetiaLLC/EspTinyUSB)**
fork (submodule; install it into your Arduino `libraries/`).

```bash
git clone --recursive https://github.com/RetiaLLC/USB-Nugget
# see BUILD-S3.md for the core pin + EspTinyUSB install, then:
arduino-cli compile --fqbn esp32:esp32:lolin_s3_mini:USBMode=default,PartitionScheme=defaultffat \
  src/RubberNugget
```

## Credits & license

- Original **USB Nugget / RubberNugget** firmware and hardware by **[HakCat](https://hakcat.com)**
  ([HakCat-Tech/USB-Nugget](https://github.com/HakCat-Tech/USB-Nugget), MIT). Payload library:
  [USB-Nugget-Payloads](https://github.com/HakCat-Tech/USB-Nugget-Payloads).
- ESP32-S3 port, BLE remote, PIN auth, script manager, and OS detection by **Retia**.
- Licensed **MIT** (see [LICENSE](LICENSE)) — © 2022 HakCat Hardware, with Retia additions.
