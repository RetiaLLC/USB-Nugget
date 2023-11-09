#include "runner.h"

#include "hidkeyboard.h"

#include "../../RubberNugget.h"
#include "../../../keyboardlayout.h"
#include "../graphics.h"

extern HIDkeyboard keyboard; //TODO: remove this

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

void runPayload(String payload, SH1106Wire* display, Adafruit_NeoPixel* strip) {
    strip->setPixelColor(0, strip->Color(255,0, 0));
    strip->show(); strip->show(); strip->show();

    String command;

    for (int i=0; i < payload.length(); i++) {
        if (payload.charAt(i) == '\n') {
          Serial.println(command);
          processDuckyScript(command, display, strip);
          command = "";
        }
        command += payload[i];
    }
    processDuckyScript(command, display, strip);
    display->clear();

    //manually update display
    display->drawXbm(0, 0, 128, 64, cat_with_exclamation_points_image_bits);
    display->display();
    strip->setPixelColor(0, strip->Color(0,0, 0));
    strip->show(); strip->show();
}

bool keyKnown(String keyPress) {
  Serial.print("looking for: ");
  Serial.println(keyPress);
  for (int i=0; i < (sizeof(keyMapRN)/sizeof(keyMapRN[0])); i++) {
    if (keyPress.equals(keyMapRN[i].title)) {
      Serial.print(keyMapRN[i].title);
      Serial.println(" found!");
      return true;
    }
  }
  return false;
}

void pressNamedKey(String keyPress, uint8_t modifiers) { //sends keystrokes to target
  for (int i=0; i< (sizeof(keyMapRN)/sizeof(keyMapRN[0])); i++) {
    if (keyPress.equals(keyMapRN[i].title)) {
      keyboard.sendPress(keyMapRN[i].key, modifiers);
    }
  }
}

void resetPayloadScreen(SH1106Wire* display) {
  display->clear();
  display->drawLine(0, 54, 127, 54);
  display->drawLine(0, 53, 127, 53);
  display->drawString(0, 54, "RUNNING PAYLOAD");
  display->display();
}
bool boundsChecking(String ducky, String tCommand, int op_length, SH1106Wire* display) {  //TODO: rewrite to use dict for each func & check compliance at top of proccess_ducky_script for better readablity

  String options = ducky.substring(tCommand.length()+ 1,ducky.length()+1);
  display->clear();
  if(options.length() > op_length) {
    Serial.println("Incorect Formating: Long");
    display->drawString(3,12,"Options Too Long");
    display->display();
    delay(200);
    return true;
  }
  else if (options.length() < op_length) {
    Serial.println("Incorect Formating: Short");
    display->drawString(3,12,"Options Too Short");
    display->display();
    delay(200);
    return true;
  }  
  else {
    return false;
  } 
}

void processDuckyScript(String ducky, SH1106Wire* display, Adafruit_NeoPixel* strip) {
  uint16_t defaultDelay = 10;
  String tCommand = ducky.substring(0, ducky.indexOf(' ')); // get command
  tCommand.toUpperCase(); tCommand.trim();
  const KEYMAP* keymap = keyboard.getKeymap();

  if (tCommand.equals("//")) {
    Serial.println("Comment");
  }
  else if (tCommand.equals("LOCALE")) {       //sets keymap lang
    if (boundsChecking(ducky, tCommand, 3, display)) {} else {
    String locale = ducky.substring(ducky.indexOf(' ')+1, ducky.length());
    Serial.printf("Locale:[%s]\n", locale);
    if (locale == "EN") {
        keyboard.setKeymap(keymap_us);
    }
    else if (locale=="ES") {
        keyboard.setKeymap(keymap_es);
    }
    else if (locale=="DE") {
        keyboard.setKeymap(keymap_de);
    }
    else if (locale=="FR") {
        keyboard.setKeymap(keymap_fr);
    }
    else if (locale=="PT") {
        keyboard.setKeymap(keymap_pt);
    }
    else {
        Serial.printf("cannot find keyset for: %s\n", locale);
    }
    }
  }
  else if (tCommand.equals("WAIT")) {     //delays script
    delay(ducky.substring(ducky.indexOf(' ')+1, ducky.length()).toInt()); //delay in MS
    Serial.println("Delayed!");       
  }
  else if (tCommand.equals("DEFAULT_WAIT") or tCommand.equals("DEFAULTWAIT")) {   //sets default delay
    defaultDelay = ducky.substring(ducky.indexOf(' ')+1, ducky.length()).toInt();
  }
  else if (tCommand.equals("SCREEN")) {    //prints text to the screen
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
  else if (tCommand.equals("LED")) {      //sets neopixel color
    resetPayloadScreen(display);
    if (boundsChecking(ducky, tCommand, 2, display)) {} else {
    display->drawString(3,12,"COLOR:");
    display->drawString(3,22,(String) ducky.substring(ducky.indexOf(' ')+1, ducky.length())); //accept single color parameter
    display->drawXbm(0, 0, 128, 64, cat_with_reload_spinner_image_bits);
    display->display();
    String color = (String) ducky.substring(ducky.indexOf(' ')+1, ducky.length());
    color.toUpperCase();
    
    if (color.equals("R")) { strip->setPixelColor(0, strip->Color(255,0, 0)); }    //TODO: may be better to replace with switch at some point
    else if (color.equals("G")) {
      strip->setPixelColor(0, strip->Color(0,255, 0));
    }
     else if (color.equals("B")) {
      strip->setPixelColor(0, strip->Color(0,0, 255));
    }
     else if (color.equals("Y")) {
      strip->setPixelColor(0, strip->Color(255,255, 0));
    }
     else if (color.equals("C")) {
      strip->setPixelColor(0, strip->Color(0,255, 255));
    }
     else if (color.equals("M")) {
      strip->setPixelColor(0, strip->Color(255,0, 255));
    }
     else if (color.equals("W")) {
      strip->setPixelColor(0, strip->Color(120,120, 120));
    }
    strip->show(); strip->show();
    }
  }
  else if (tCommand.equals("LED+RGB")) {  //accept grb colorcodes as LED+RGB xxx xxx xxx
    resetPayloadScreen(display);
    if (boundsChecking(ducky, tCommand, 12, display)) {} else {
    display->drawString(3,12,"COLOR:");
    display->drawString(3,22,(String) ducky.substring(8, ducky.length())); 
    display->drawXbm(0, 0, 128, 64, cat_with_reload_spinner_image_bits);
    display->display();
      
    uint32_t color = strip->Color(ducky.substring(13,16).toInt(), ducky.substring(9,12).toInt(), ducky.substring(17,20).toInt()); //extracts color
    strip->setPixelColor(0, color);
    strip->show();
    }
  }
  else if (tCommand.equals("LED+HSV")) { //accept hsv colorcodes as LED+HSV xxxxx xxx xxx
    resetPayloadScreen(display);
    if (boundsChecking(ducky, tCommand, 14, display)) {} else {
    display->drawString(3,12,"COLOR:");
    display->drawString(3,22,(String) ducky.substring(8,21));     //TODO: replace this with color matching
    display->drawXbm(0, 0, 128, 64, cat_with_reload_spinner_image_bits);
    display->display();
    
    uint32_t rgbcolor = strip->gamma32(strip->ColorHSV(ducky.substring(9,14).toInt(), ducky.substring(15,18).toInt(), ducky.substring(19,22).toInt())); //extracts hsv color  and processes
    uint32_t color = (((rgbcolor >> 24) & 0xFF) << 24) | (((rgbcolor >> 8) & 0xFF) << 16) | (((rgbcolor >> 16) & 0xFF) << 8) | (rgbcolor & 0xFF);  //converts packed wrgb to packed wgrb

    strip->setPixelColor(0, color);
    strip->show(); strip->show();
    }
  }
  else if (tCommand.equals("TYPE")) {  //types strings to target
    resetPayloadScreen(display);
    display->drawString(3,12,"TYPE: ");
    if (String(ducky.substring(ducky.indexOf(' ')+1, ducky.length())).length() > 9) {
      display->drawString(3,22,String(ducky.substring(ducky.indexOf(' ')+1, ducky.length())).substring(0,10)+"...");
    }
    else {
      display->drawString(3,22,String(ducky.substring(ducky.indexOf(' ')+1, ducky.length())));
    }
    display->drawXbm(0, 0, 128, 64, cat_with_one_exclamation_point_image_bits);
    display->display();
    String tmpString = String(ducky.substring(ducky.indexOf(' ')+1, ducky.length()));
    keyboard.sendString(tmpString);    
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
        keyboard.sendPress(keycode, modifiers);
        delay(2);
      }
      currentTokenLeftIndex = currentTokenRightIndex + 1;
    }
    keyboard.sendRelease();
  }
  else {
    Serial.println("Command not found");
  }

  display->display();
}
