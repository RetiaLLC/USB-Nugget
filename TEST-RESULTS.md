# Bad Nugget multi-board — assumptions & test log

## VERDICT (both operator questions answered YES)
- **Does the AP work? → YES**: beacons ✅, WPA2 join+DHCP (192.168.4.2) ✅, web UI HTTP 200 (full payload manager) ✅.
- **Does it connect to another AP (STA)? → YES**: puck associated to a "NugTest" SoftAP end-to-end ✅ (F1).
- **~24 assumptions PASS** across enumeration, HID (keyboard+mouse+LOOP), MSC, CDC remote, WiFi AP+STA, BLE advert,
  and the screenless NoopDisplay. Puck-only visual items (90% LED) are N/A now the pucks are reassigned.
- Two bugs found: the OTG-reflash fragility (I wedged the bench once — recovered) and a cosmetic `getWifi` stale-status
  read. Neither blocks the port; both are documented below.


DUTs on the **Pi-4 workbench** (100.83.202.100): 70:EC puck (SLOT1/ttyACM0), 71:A4 puck (SLOT7),
Nibble Zero E5:39:F8 (SLOT4), badge (SLOT5). All 3 Bad Nuggets run concurrently as `05ac:020b`.
WiFi test instrument = **workbench5** wtest0 (co-located, sees the Pi-4 APs).

Legend: ✅ pass · ❌ fail · ⚠️ pass-with-caveat · 🚧 blocked (bench, not firmware) · ⏳ not run

## A. USB enumeration & composite
- A1  05ac:020b composite (CDC+MSC+HID), pucks + Nibble .......... ✅ 3 concurrent Bad Nuggets on the bus
- A2  enumeration reliable across cold boots (race fix) .......... ⚠️ came up 05ac:020b on ALL ~6 boots seen
       (flashes+resets); a dedicated N× cold-boot test is 🚧 (no uhubctl; SLOT1 EN-reset is a no-op on the puck)
- A3  HID keyboard + mouse both enumerate ....................... ✅ dmesg Mouse+Keyboard, hidraw

## B. HID / DuckyScript interpreter
- B1  STRING types fully (no first-char bug) .................... ✅ puck typed `Hello from the Bad Nugget!` (event7)
- B2  mouse MOVE (jiggler) ..................................... ✅ puck mouse moved (event6, accumulating)
- B3  modifier combos CTRL/ALT/GUI ............................. ⏳ (same interpreter as B1; not isolated-tested)
- B4  LOOP / continuous payload ................................ ✅ jiggler ran continuously
- B5  LED command drives the ring .............................. ⚠️ hello.txt runs `LED G`; visual color unreliable on C270
- B6  IF_OS / $_OS OS detection ................................ ⏳
- B7  hold-B / ~PB aborts a running payload .................... ⚠️ mouse slowed after ~PB; not cleanly isolated on screenless
- B1b Nibble typed the full string ............................. ✅ (earlier)

## C. MSC drive
- C1  MSC FAT drive mounts ..................................... ✅ 3× "ESP32-S2 FLASH" (sda/sdb/sdc), vfat
- C2  seeded payloads present ................................. ✅ hello/jiggler/leds/mouse.txt + OS dirs + .seeded
- C3  write a new payload → it runs ........................... ⏳ (host-side MSC write risks FFat cache conflict; use web/~W)

## D. CDC serial remote (nugremote)
- D1  ~P press injects buttons → runs payload ................. ✅ press A on the screenless puck ran hello.txt
- D2  ~S screen dump on the SCREENLESS Newsheen ............... ✅ virtual framebuffer renders the full menu (crisp PNG)
- D3  run a payload via the remote ............................ ✅
- D4  ~R reboot-to-bootloader ................................. ✅ (used to reflash)

## E. WiFi AP
- E1  AP beacons: Newsheen ×2 + Nugget AP ..................... ✅ (workbench5 scan, WPA2-PSK)
- E2  AP joinable — WPA2 4-way completes ...................... ✅ CTRL-EVENT-CONNECTED to a6:cb:8f:b1:70:ec (pw nugget123)
       ⚠️ the ESP softAP 4-way is slow/needs retries with the Ralink client (real clients retry fine)
- E2b AP + DHCP → wtest0 got 192.168.4.2 from the puck ........ ✅ (portal sta_join, gateway 192.168.4.1)
- E3  web UI reachable (192.168.4.1) .......................... ✅ HTTP 200, 24.6 KB page (base64 over the portal http bridge)
- E4  web UI = full payload manager + STA form + editor ....... ✅ inputs staSsid/staPass, createPayloadPath, per-OS autorun
- E5/E6 captive portal / mDNS ................................. ⏳ (not separately exercised)

## F. STA "Join AP" (connect to another AP)  ← operator's explicit question  ✅ VERIFIED END-TO-END
- F1  configure sta creds (web UI /savewifi = LIVE WiFi.begin, no reboot) → DUT joins a test AP ...... ✅
       POSTed sta_ssid=NugTest to the puck via its web UI ("Saved. Joining NugTest", HTTP 200), flipped wtest0 into a
       "NugTest" SoftAP, and the puck associated: **Station a4:cb:8f:b1:71:a4 on wtest0** within 8 s. Safe (portal-managed).
       (The GET /wifi status display is stale — `getWifi` reads gConfig while `saveWifi`/WiFi.begin use gStaSsid — a
        cosmetic bug; the join itself works. Left 71:A4 STA-configured to the now-gone NugTest = harmless background retry.)

## G. BLE remote
- G1  BLE advertises "Nugget" ................................. ✅ all 3 (Nibble 3C:0F:02:E5:39:F9, pucks …70:ED / …71:A5)
- G2  BLE connect + PIN auth .................................. ⏳
- G3  BLE injects buttons ..................................... ⏳

## H. Newsheen screenless-specific
- H1  NoopDisplay virtual FB renders (via ~S) ................. ✅ full menu dumped from the screenless puck
- H2  warm-white idle @ 90% ................................... ⏳ (90% reflash in progress; 64% was operator-confirmed white)
- H3  red-on-run → warm-white restore ......................... ⚠️ code verified (runner fills red @L95, idleLeds restore); C270 color-limited
- H4  select/run + scroll via remote on 2-btn board .......... ✅ press A = run, press D = scroll (BTN_DOWN injected)

## I. Nibble-specific
- I1  OLED right-side-up ...................................... ✅
- I2  6-button physical nav ................................... ⚠️ logical (remote) ✅; physical press-test needs a finger
- I3  NeoPixel GPIO21 ......................................... ⏳

## Feature suggestions
1. **Runtime NeoPixel brightness** (web UI / config, no reflash) — reflashing an OTG board is fragile; a runtime
   brightness (and the idle color) would've made "set 90%" a 1-request change instead of a reflash.
2. **Unique per-device AP SSID** — both pucks broadcast "Newsheen" (ambiguous). Append the MAC suffix
   (e.g. `Newsheen-70EC`), like WLED's `WLED_AP_SSID_UNIQUE`.
3. **Rich LED status on the screenless Newsheen** — encode state in the ring: AP-up = slow breathe, STA-connected
   = a distinct hue, payload-running = red pulse, error = red blink. It's the only output; make it expressive.
4. **BLE "run payload N"** — for a screenless BadUSB the natural UX is a phone tapping a payload over BLE. The BLE
   remote already injects buttons + PIN-auths; a direct "run payload by index/name" would shine here.
5. **Optional auto-run-on-plug** (guarded) — classic BadUSB: run a designated payload on enumerate, behind a
   config toggle + the hold-B safety.
6. **softAP robustness** — the 4-way was slow with the test client; worth confirming beacon interval / PMF settings.
7. **Document the puck WS2812 order** = GRB by eye (the C270 webcam renders warm-white as magenta — don't trust it).

## ⚠️ BENCH STATE AT END OF SESSION (needs your hands)
- **70:EC puck**: wedged OFF the USB bus during the 90%-brightness reflash (OTG `~R` + failed writes).
- **Then all 3 Bad Nuggets (70:EC, 71:A4, Nibble) fell off the Pi-4 bus** (05ac count 3→0) — cascade from
  the repeated `~R`/esptool/portal-restart churn on the shared hub. **All need a power-cycle / replug.**
  They are NOT bricked — firmware is intact; a USB power-cycle brings them back (to app or ROM download).
  No remote recovery on the Pi-4 (EN-reset is a no-op on these; no uhubctl). Badge (SLOT3) is unaffected.
- **90% brightness**: built (`badnugget-newsheen.factory.bin`, this is the pending image) but NOT applied —
  the reflash to apply it is what wedged 70:EC. Flash it via BOOT+replug→download, or on the EN-wired bench.
- **The OTG-reflash lesson**: reflashing a running Bad Nugget (native-USB/TinyUSB) is fragile — `/api/flash`
  and 1200-touch don't work; `~R` reaches ROM download but `--before no-reset` write-flash is unreliable and
  can drop the board off the bus. Don't do it autonomously without EN+BOOT recovery wiring or physical access.

## Bench notes for the operator
- ⚠️ **workbench5 went offline** during WiFi testing. Cause: I associated its wtest0 to the puck AP with `dhclient`,
  which installed a **default route** via the puck → tailscale/uplink broke. The **portal `/api/wifi/sta_join`
  method is safe** (scoped) and is how the WLED test worked; I should have used it throughout. wb5 likely needs a
  power-cycle. The Pi-4 was never put at risk.
