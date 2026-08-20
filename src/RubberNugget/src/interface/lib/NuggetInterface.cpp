#include "NuggetInterface.h"
#include "dejavu.h"
#include "../../remote.h"
#include "../../recovery_fixed.h"   // R2 (corrected): recoveryKick() heartbeat

//----------------------------------------
// NuggetInputs

NuggetInputs::NuggetInputs() {
  this->lastBtn = 0;
  this->pressedButton = -1;

#if BOARD_HAS_DPAD
  this->addButton(BTN_UP);
  this->addButton(BTN_DOWN);
#endif
  this->addButton(BTN_LEFT);
  this->addButton(BTN_RIGHT);
#if BOARD_HAS_AB
  this->addButton(BTN_A);      // A = select/enter (mapped to RIGHT in getInput)
  this->addButton(BTN_B);      // B = back        (mapped to LEFT  in getInput)
#endif
}

void NuggetInputs::addButton(int pin) {
  pinMode(pin, INPUT_PULLUP);
  this->buttons[this->lastBtn++] = pin;
}

int NuggetInputs::getInput() {
  int buttonState;
  int qb = remotePopBtn();                 // queued remote press (serial remote-control)
  if (qb != BTN_NONE) return qb;
  if (this->pressedButton != BTN_NONE){
    buttonState = digitalRead(this->pressedButton);
    if (buttonState==BTN_PRESS){
      return BTN_NONE;
    }
    this->pressedButton = BTN_NONE;
    return BTN_NONE;
  }

  for (int i = 0; i < BTN_COUNT; i++){
    int btn = this->buttons[i];
    buttonState = digitalRead(btn);
    if (buttonState==BTN_PRESS){
      this->pressedButton = btn;
#if BOARD_HAS_AB
      if (btn == BTN_A) return BTN_RIGHT;   // A = select/enter
      if (btn == BTN_B) return BTN_LEFT;    // B = back
#endif
      return btn;                            // d-pad-only boards (S2): RIGHT = select, LEFT = back
    }
  }

  this->pressedButton = -1;
  return BTN_NONE;
}

//----------------------------------------
// NuggetScreen

NuggetScreen::NuggetScreen(){
  this->alwaysUpdate = false;
}
NuggetScreen::~NuggetScreen(){
}
void NuggetScreen::setDisplay(NuggetDisplay* display){
  this->display = display;
}
void NuggetScreen::setInputs(NuggetInputs* inputs){
  this->inputs = inputs;
}
void NuggetScreen::setStrip(Adafruit_NeoPixel* strip){
  this->strip = strip;
}
void NuggetScreen::setNuggetInterface(NuggetInterface* nI){
  this->nuggetInterface = nI;
}
void NuggetScreen::pushScreen(NuggetScreen* screen){
  this->nuggetInterface->pushScreen(screen);
}
void NuggetScreen::alwaysUpdates(bool set){
  this->alwaysUpdate = set;
}
int NuggetScreen::_update(){
  int btn = this->inputs->getInput();
  if (btn == BTN_NONE && !(this->alwaysUpdate)){
    delay(2);
    return SCREEN_NONE;
  }
  return this->update(btn);
}

//----------------------------------------
// NuggetInterface
NuggetInterface::NuggetInterface(){
  // Reuse the display RubberNugget::init() already brought up for boot-progress; only create one
  // here if it somehow wasn't (defensive).
  NuggetDisplay* nDisplay = g_display;
  if (!nDisplay) {
    nDisplay = new NuggetDisplay(OLED_ADDR, OLED_SDA, OLED_SCL);   // driver+pins per board_config.h
    nDisplay->init();
#if OLED_FLIP
    nDisplay->flipScreenVertically();   // this board's OLED is mounted 180° vs the Nugget
#endif
    g_display = nDisplay;                // expose for the serial remote's screen dump
  }
  this->inputs = new NuggetInputs();
  this->screenLock = xSemaphoreCreateMutex();
  if (this->screenLock == nullptr) {
    Serial.println("[NuggetInterface] mutex could not be created");
  }
  nDisplay->setTextAlignment(TEXT_ALIGN_LEFT);
  nDisplay->setFont(DejaVu_Sans_Mono_10);
  this->display = nDisplay;
  this->currentScreenNode = nullptr;

  pinMode(NEOPIXEL_PIN, OUTPUT);
  this->strip = new Adafruit_NeoPixel(NEOPIXEL_PIN_CNT, NEOPIXEL_PIN, NEOPIXEL_TYPE);
  this->strip->begin();
  this->strip->setBrightness(NEOPIXEL_BRIGHTNESS);
#if BOARD_HAS_DISPLAY
  this->strip->show();               // screen boards: ring starts off (only lit as run feedback)
#else
  this->idleLeds();                  // screenless (Newsheen): warm-white idle = the "alive" indicator
#endif
}

// Screenless boards (Newsheen): warm-white idle on the whole ring — the "alive, no payload running"
// state the operator wants. Payloads' LED commands override it; it's restored when the menu redraws.
void NuggetInterface::idleLeds(){
  if (!this->strip) return;
  for (int i = 0; i < NEOPIXEL_PIN_CNT; i++)
    this->strip->setPixelColor(i, this->strip->Color(255, 160, 60));   // warm white (~2600K)
  this->strip->show();
}

NuggetInterface::~NuggetInterface(){
  while (this->currentScreenNode != nullptr){
    this->popScreen();
  }
  delete this->display;
  delete this->inputs;
  delete this->strip;
}

bool NuggetInterface::start(){
  while (true) {
    if (g_testHang) { for (;;) { /* deliberate wedge: stop feeding recoveryKick() -> R2 reverts */ } }
    recoveryKick();   // R2 heartbeat: feed the watchdog; a wedge here -> auto-revert to rescue
    if (xSemaphoreTake(this->screenLock, 0)==pdFALSE) {
      delay(10);
      continue;
    }
    int action = SCREEN_NONE;
    if (!currentScreenHasRendered) {
      this->draw();
      action = this->currentScreenNode->screen->update(EVENT_INIT);
      this->currentScreenHasRendered = (volatile bool*) true;
    } else {
      action = this->currentScreenNode->screen->_update();
    }

    if (action==SCREEN_BACK){
      this->popScreen();
#if !BOARD_HAS_DISPLAY
      // screenless: a payload/submenu just exited — restore the warm-white idle ring at the home screen
      if (this->currentScreenNode && this->currentScreenNode->prev == nullptr) this->idleLeds();
#endif
    }
    if (action==SCREEN_REDRAW){
      this->draw();
    }
    if (action==SCREEN_PUSH){
      this->draw();
      this->currentScreenNode->screen->update(EVENT_INIT);
    }
    xSemaphoreGive(this->screenLock);
  }
}

// injectScreen pushes a screen onto the stack e.g. from a thread that did
// not call NuggetInterface::start. This function returns false if the screen
// could not be locked in wait TICKS
bool NuggetInterface::injectScreen(NuggetScreen* screen){
  if (xSemaphoreTake(this->screenLock, 200)==pdFALSE) {
    // could not acquire lock
    return false;
  }
  this->pushScreen(screen);
  //this->draw();
  //this->currentScreenNode->screen->update(EVENT_INIT);
  xSemaphoreGive(this->screenLock);
}

bool NuggetInterface::pushScreen(NuggetScreen* screen){
  if (screen==nullptr) {
      return false;
  }
  screen->setDisplay(this->display);
  screen->setInputs(this->inputs);
  screen->setStrip(this->strip);
  screen->setNuggetInterface(this);
  ScreenNode* nextScreenNode = new ScreenNode;
  nextScreenNode->prev = this->currentScreenNode;
  nextScreenNode->screen = screen;

  this->currentScreenNode = nextScreenNode;
  this->currentScreenHasRendered = (volatile bool*) false;
  return true;
}

bool NuggetInterface::popScreen(){
  if (this->currentScreenNode->prev == nullptr){
    return false;
  }
  volatile ScreenNode* popped = this->currentScreenNode;
  this->currentScreenNode = this->currentScreenNode->prev;
  this->currentScreenHasRendered = (volatile bool*) false;
  delete popped->screen;
  delete popped;

  return true;
}

bool NuggetInterface::draw() {
  if (this->currentScreenNode == nullptr){
    return false;
  }
  this->display->clear();
  this->currentScreenNode->screen->draw();
  this->display->display();
  return true;
}
