#include "runner.h"

#include "hidcomposite.h"

#include "../../RubberNugget.h"
#include "../../../keyboardlayout.h"
#include "../graphics.h"
#include "../../recovery_fixed.h"   // R2/F4: feed the watchdog during payload execution

#include <vector>

extern HIDcomposite hid;   // keyboard + mouse on one interface

// --- Abort: hold B (or send ~PB over serial) to stop a running payload ---
// A payload runs inside one blocking runPayload() call, so getInput() isn't polled.
// wantStop() reads the B button GPIO directly (and honours a ~PB remote press) at the
// same slices where we already feed the watchdog, so a held B stops within ~250ms.
extern volatile bool g_abortReq;    // set by ~PB/~PL over the serial remote (remote.cpp)
static bool g_payloadStop = false;
static bool wantStop() {
  if (g_payloadStop) return true;
#if BOARD_HAS_AB
  if (digitalRead(BTN_B) == BTN_PRESS) g_payloadStop = true;      // hold physical B
#else
  if (digitalRead(BTN_LEFT) == BTN_PRESS) g_payloadStop = true;   // d-pad-only (S2): hold LEFT (back) = abort
#endif
  else if (g_abortReq) g_payloadStop = true;                   // ~PB/~PL over serial
  return g_payloadStop;
}

// F4: a payload runs entirely inside one screen->update() call, so the main loop's recoveryKick()
// never runs while it executes. Any blocking wait longer than REC_HANG_MS would look like a wedge
// and revert a perfectly healthy board mid-payload. Sleep in short slices and kick between them.
static void kickingDelay(long ms) {
  recoveryKick();
  while (ms > 0) {
    if (wantStop()) return;   // abort long WAITs promptly
    long chunk = ms > 250 ? 250 : ms;
    delay(chunk);
    ms -= chunk;
    recoveryKick();
  }
}

// Mouse deltas are int8; split a larger move into <=120px steps so long moves work.
static void moveMouseBy(int x, int y) {
  while (x != 0 || y != 0) {
    if (wantStop()) return;
    int8_t sx = x >  120 ? 120 : (x < -120 ? -120 : x);
    int8_t sy = y >  120 ? 120 : (y < -120 ? -120 : y);
    hid.move(sx, sy);
    x -= sx; y -= sy;
    delay(6);
  }
}

ScriptRunnerScreen::ScriptRunnerScreen(String payload) {
  this->has_run = false;
  this->payload = payload;
  Serial.println("[ScriptRunnerInit]");
  Serial.println(payload);
}

int ScriptRunnerScreen::update(int btn) {
  if (!has_run) {
    runPayload(this->payload, this->display, this->strip);
    has_run = true;
    return SCREEN_REDRAW;
  }
  if (btn==BTN_LEFT){
    return SCREEN_BACK;
  }
  return SCREEN_REDRAW;
}

bool ScriptRunnerScreen::draw() {
  if (!has_run) {
    display->clear();
    return true;
  }
  display->drawXbm(0, 0, 128, 64, cat_with_exclamation_points_image_bits);
  display->drawString(3,9,"Press LEFT");
  display->drawString(3,19,"to go back");
  display->drawLine(0, 54, 127, 54);
  display->drawLine(0, 53, 127, 53);
  display->drawString(0, 54, "FINISHED PAYLOAD");
  return true;
}

void resetPayloadScreen(NuggetDisplay* display);   // defined below; used by runPayload/processDuckyScript

void runPayload(String payload, NuggetDisplay* display, Adafruit_NeoPixel* strip) {
    g_payloadStop = false; g_abortReq = false;   // fresh abort state each run
    resetPayloadScreen(display);                 // show "RUNNING PAYLOAD" at once (ScriptRunnerScreen cleared it)
    strip->fill(strip->Color(255,0, 0));
    strip->show(); strip->show(); strip->show();

    // split payload into lines
    std::vector<String> lines;
    String cur;
    for (int i = 0; i < payload.length(); i++) {
        char c = payload.charAt(i);
        if (c == '\n') { lines.push_back(cur); cur = ""; }
        else if (c != '\r') { cur += c; }
    }
    if (cur.length()) lines.push_back(cur);

    // execute with REPEAT (repeat previous line) + LOOP n / ENDLOOP (nestable block)
    struct LoopFrame { int startPc; int remaining; };
    std::vector<LoopFrame> loops;
    String prevLine = "";
    bool ifActive = false, ifTaken = false, skipping = false;   // single-level IF_OS
    for (int pc = 0; pc <= (int)lines.size(); pc++) {
        if (wantStop()) break;   // hold B (or ~PB) to abort the payload
        if (pc == (int)lines.size()) {
            // End of script: auto-close any LOOP left open (beginner-friendly — a
            // LOOP n without a matching ENDLOOP just repeats everything after it).
            if (loops.empty()) break;
            if (--loops.back().remaining > 0) { pc = loops.back().startPc; }  // pc++ -> loop body
            else { loops.pop_back(); pc = (int)lines.size() - 1; }            // pop, re-check outer
            continue;
        }
        recoveryKick();   // F4: keep the heartbeat alive across many-line / looping payloads
        String u = lines[pc]; u.trim();
        int sp = u.indexOf(' ');
        String cmd = (sp >= 0 ? u.substring(0, sp) : u);
        cmd.toUpperCase();

        // IF_OS <name> / ELSE / END_IF: branch on the detected OS (single level, non-nested)
        if (cmd == "IF_OS") {
            String want = (sp >= 0 ? u.substring(sp + 1) : String("")); want.trim(); want.toUpperCase();
            String os = nuggetOS();
            ifTaken = (os == want) || (want.startsWith("MAC") && os == "MACOS")
                      || (want.startsWith("WIN") && os == "WINDOWS") || (want == "OSX" && os == "MACOS");
            ifActive = true; skipping = !ifTaken;
            continue;
        }
        if (cmd == "ELSE" && ifActive) { skipping = ifTaken; continue; }
        if (cmd == "END_IF" || cmd == "ENDIF") { ifActive = false; skipping = false; continue; }
        if (skipping) continue;   // inside a non-matching IF_OS branch

        if (cmd == "REPEAT") {
            int n = u.substring(sp + 1).toInt();
            for (int r = 0; r < n; r++) processDuckyScript(prevLine, display, strip);
            continue;
        }
        if (cmd == "LOOP") {
            int n = (sp >= 0 ? u.substring(sp + 1).toInt() : 0); if (n <= 0) n = 1;
            loops.push_back({pc, n});           // body runs from pc+1 to the matching ENDLOOP
            continue;
        }
        if (cmd == "ENDLOOP") {
            if (!loops.empty()) {
                if (--loops.back().remaining > 0) pc = loops.back().startPc;  // pc++ lands on body start
                else loops.pop_back();
            }
            continue;
        }
        processDuckyScript(lines[pc], display, strip);
        prevLine = lines[pc];
    }
    hid.sendRelease();   // release any key left held — important on abort (avoids stuck keys)
    display->clear();
    if (g_payloadStop) {
      display->drawString(28, 24, "STOPPED");
      display->drawString(3, 40, "Press LEFT");
    } else {
      display->drawXbm(0, 0, 128, 64, cat_with_exclamation_points_image_bits);
    }
    display->display();
    strip->fill(strip->Color(0,0, 0));
    strip->show(); strip->show();
}

bool keyKnown(String keyPress) {
  Serial.print("looking for: ");
  Serial.println(keyPress);
  for (int i=0; i< (sizeof(keyMapRN)/sizeof(keyMapRN[0])); i++) {
    if (keyPress.equals(keyMapRN[i].title)) {
      Serial.print(keyMapRN[i].title);
      Serial.println(" found!");
      return true;
    }
  }
  return false;
}

void pressNamedKey(String keyPress, uint8_t modifiers) {
  for (int i=0; i< (sizeof(keyMapRN)/sizeof(keyMapRN[0])); i++) {
    if (keyPress.equals(keyMapRN[i].title)) {
      hid.sendPress(keyMapRN[i].key, modifiers);
    }
  }
}

void resetPayloadScreen(NuggetDisplay* display) {
  display->clear();
  display->drawLine(0, 54, 127, 54);
  display->drawLine(0, 53, 127, 53);
  display->drawString(0, 54, "RUNNING PAYLOAD");
  display->display();
}

void processDuckyScript(String ducky, NuggetDisplay* display, Adafruit_NeoPixel* strip) {
  uint16_t defaultDelay = 10;
  // Strip leading/trailing whitespace FIRST. Lines inside a LOOP/ENDLOOP block
  // are indented; without this, indexOf(' ') would be 0 on an indented line and
  // the command would parse as "" -> "Command not found", so every indented
  // body line (all the mouse/jiggle work in the default loops) was skipped.
  ducky.trim();
  String tCommand = ducky.substring(0, ducky.indexOf(' ')); // get command
  tCommand.toUpperCase(); tCommand.trim();
  const KEYMAP* keymap = hid.getKeymap();

  if (tCommand.equals("//") || tCommand.equals("REM")) {
    Serial.println("Comment");
  }
  else if (tCommand.equals("LOCALE")) {
    String locale = ducky.substring(ducky.indexOf(' ')+1, ducky.length());
    Serial.printf("Locale:[%s]\n", locale);
    if (locale == "EN") {
        hid.setKeymap(keymap_us);
    }
    else if (locale=="ES") {
        hid.setKeymap(keymap_es);
    }
    else if (locale=="DE") {
        hid.setKeymap(keymap_de);
    }
    else if (locale=="FR") {
        hid.setKeymap(keymap_fr);
    }
    else if (locale=="PT") {
        hid.setKeymap(keymap_pt);
    }
    else {
        Serial.printf("cannot find keyset for: %s\n", locale);
    }
  }
  else if (tCommand.equals("WAIT") || tCommand.equals("DELAY")) {
    long ms = ducky.substring(ducky.indexOf(' ')+1, ducky.length()).toInt();
    resetPayloadScreen(display);                       // don't leave the OLED blank during the delay
    display->drawString(3,12,"WAIT");
    display->drawString(3,22,String(ms)+" ms");
    display->drawXbm(0, 0, 128, 64, cat_with_reload_spinner_image_bits);
    display->display();
    kickingDelay(ms);                                  // F4: watchdog-safe delay
    Serial.println("Delayed!");
  }
  else if (tCommand.equals("DEFAULT_WAIT") or tCommand.equals("DEFAULTWAIT")) {
    defaultDelay = ducky.substring(ducky.indexOf(' ')+1, ducky.length()).toInt();
  }
  else if (tCommand.equals("SCREEN")) {
    resetPayloadScreen(display);
    if (String(ducky.substring(ducky.indexOf(' ')+1, ducky.length())).length() > 9) {
      display->drawString(3,22,String(ducky.substring(ducky.indexOf(' ')+1, ducky.length())).substring(0,10)+"...");
    }
    else {
      display->drawString(3,22,String(ducky.substring(ducky.indexOf(' ')+1, ducky.length())));
    }
    display->drawXbm(0, 0, 128, 64, cat_with_one_exclamation_point_image_bits);
    display->display();
  }
  else if (tCommand.equals("LED")) {
    resetPayloadScreen(display);
    display->drawString(3,12,"COLOR:");
    display->drawString(3,22,(String) ducky.substring(ducky.indexOf(' ')+1, ducky.length())); // accept single color parameter
    display->drawXbm(0, 0, 128, 64, cat_with_reload_spinner_image_bits);
    display->display();
    String color = (String) ducky.substring(ducky.indexOf(' ')+1, ducky.length());
    color.toUpperCase();
    
    if (color.equals("R")) { strip->fill(strip->Color(255,0, 0)); }
    else if (color.equals("G")) {
      strip->fill(strip->Color(0,255, 0));
    }
     else if (color.equals("B")) {
      strip->fill(strip->Color(0,0, 255));
    }
     else if (color.equals("Y")) {
      strip->fill(strip->Color(255,255, 0));
    }
     else if (color.equals("C")) {
      strip->fill(strip->Color(0,255, 255));
    }
     else if (color.equals("M")) {
      strip->fill(strip->Color(255,0, 255));
    }
     else if (color.equals("W")) {
      strip->fill(strip->Color(120,120, 120));
    }
    strip->show(); strip->show();
  }
  else if (tCommand.equals("TYPE") || tCommand.equals("STRING") || tCommand.equals("STRINGLN")) {
    int spc = ducky.indexOf(' ');
    String tmpString = (spc >= 0) ? ducky.substring(spc + 1) : String("");  // no arg -> type nothing
    tmpString.replace("$_OS", nuggetOS());   // OS-detection token -> WINDOWS/MACOS/LINUX/?
    resetPayloadScreen(display);
    display->drawString(3,12,"TYPE: ");
    display->drawString(3,22, tmpString.length() > 9 ? tmpString.substring(0,10)+"..." : tmpString);
    display->drawXbm(0, 0, 128, 64, cat_with_one_exclamation_point_image_bits);
    display->display();
    hid.sendString(tmpString);
    if (tCommand.equals("STRINGLN")) { pressNamedKey("ENTER", 0); delay(2); hid.sendRelease(); }  // STRINGLN = type + Enter
  }
  
  else if (tCommand.equals("MOUSE_MOVE") || tCommand.equals("MOUSEMOVE")) {
    String args = ducky.substring(ducky.indexOf(' ')+1); args.trim();
    int sp = args.indexOf(' ');
    int x = args.substring(0, sp).toInt();
    int y = args.substring(sp+1).toInt();
    moveMouseBy(x, y);
  }
  else if (tCommand.equals("MOUSE_CLICK") || tCommand.equals("MOUSECLICK")) {
    String b = ducky.substring(ducky.indexOf(' ')+1); b.toUpperCase(); b.trim();
    if (b.startsWith("R"))      hid.pressRight();
    else if (b.startsWith("M")) hid.pressMiddle();
    else                        hid.pressLeft();
  }
  else if (tCommand.equals("MOUSE_DOUBLE") || tCommand.equals("MOUSEDOUBLE")) {
    hid.doublePressLeft();
  }
  else if (tCommand.equals("MOUSE_SCROLL") || tCommand.equals("MOUSESCROLL")) {
    int n = ducky.substring(ducky.indexOf(' ')+1).toInt();
    hid.wheel((int8_t)n, 0);
  }
  else if (tCommand.equals("JIGGLE")) {
    int n = ducky.substring(ducky.indexOf(' ')+1).toInt(); if (n <= 0) n = 10;
    resetPayloadScreen(display);
    display->drawString(3,12,"JIGGLE"); display->display();
    for (int i = 0; i < n; i++) { if (wantStop()) break; recoveryKick(); hid.move(6, 0); delay(150); hid.move(-6, 0); delay(150); }
  }
  else if (keyKnown(tCommand)) {
    resetPayloadScreen(display);
    display->drawString(3,12,"KEY PRESS");
    ducky.trim(); // remove leading, trailing whitespace
    int currentTokenLeftIndex = 0;
    int currentTokenRightIndex = 0;
    String currentToken;
    uint8_t modifiers = 0;

    while (currentTokenLeftIndex < ducky.length()) {
      int nextSpace = ducky.indexOf(' ', currentTokenLeftIndex);
      if (nextSpace==-1){
        currentTokenRightIndex = ducky.length();
      } else {
        currentTokenRightIndex = nextSpace;
      }
      currentToken = ducky.substring(currentTokenLeftIndex, currentTokenRightIndex);
      if (currentToken == "CTRL" || currentToken == "CONTROL"){
        modifiers += KEY_MOD_LCTRL;
      } else if (currentToken == "SHIFT"){
        modifiers += KEY_MOD_LSHIFT;
      } else if (currentToken == "ALT") {
        modifiers += KEY_MOD_LALT;
      } else if (currentToken== "GUI" or currentToken =="WINDOWS"){
        modifiers += KEY_MOD_LMETA;
      }
      
      else if (currentToken.length() != 1) {
        // Search for named key, e.g. DELETE or TAB
        if (keyKnown(currentToken)){
          pressNamedKey(currentToken, modifiers);
          delay(2);
        } else {
          // unknown named key
          display->drawString(3,22,String("ERROR"));
          display->display();
          delay(1000);
        }
      } else {
        // Single letter like the 's' in: CTRL s
        unsigned char keycode = keymap[currentToken[0]].usage;
        hid.sendPress(keycode, modifiers);
        delay(2);
      }
      currentTokenLeftIndex = currentTokenRightIndex + 1;
    }
    hid.sendRelease();
  }
  else {
    Serial.println("Command not found");
  }

  display->display();
}
