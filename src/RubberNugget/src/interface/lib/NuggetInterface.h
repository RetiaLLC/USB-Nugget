#ifndef NUGGET_INTERFACE_H
#define NUGGET_INTERFACE_H

#include <Adafruit_NeoPixel.h>
#include "../../board_config.h"   // per-board pins, the NuggetDisplay type (SH1106/SSD1306), feature flags

//----------------------------------------
// NuggetInputs

#define BTN_NONE   -1
#define BTN_PRESS   0
#define BTN_NPRESS  1

// Button + NeoPixel pins come from board_config.h (per-board). A/B only exist on boards with
// BOARD_HAS_AB (the S2 USB Nugget is d-pad-only). getInput() maps A->RIGHT, B->LEFT where present.
#define BTN_COUNT  NUG_BTN_COUNT
#define BTN_UP     NUG_BTN_UP
#define BTN_DOWN   NUG_BTN_DOWN
#define BTN_LEFT   NUG_BTN_LEFT
#define BTN_RIGHT  NUG_BTN_RIGHT
#if BOARD_HAS_AB
#define BTN_A      NUG_BTN_A
#define BTN_B      NUG_BTN_B
#endif
#define NEOPIXEL_PIN        NUG_NEOPIXEL_PIN
#define NEOPIXEL_PIN_CNT    NUG_NEOPIXEL_CNT
#define NEOPIXEL_TYPE       NUG_NEOPIXEL_TYPE        // NEO_RGB (Nuggets) vs NEO_GRB (Newsheen WS2812B)
// Per-board brightness (0-255): dim on the Nuggets' bare ears, brighter on the diffused Newsheen ring.
#define NEOPIXEL_BRIGHTNESS NUG_NEOPIXEL_BRIGHTNESS

#define EVENT_INIT 100

class NuggetInputs {
   public:
      NuggetInputs();
      int getInput();
   private:
      void addButton(int);
      int buttons[BTN_COUNT];
      int pressedButton;
      int lastBtn;
};

//----------------------------------------
// NuggetScreen

class NuggetInterface;

#define SCREEN_NONE   1
#define SCREEN_BACK   2
#define SCREEN_REDRAW 3
#define SCREEN_PUSH   4

class NuggetScreen {
   public:
      NuggetScreen();
      virtual ~NuggetScreen();
      virtual bool draw() = 0;
      virtual int update(int){return SCREEN_NONE;};
      void setDisplay(NuggetDisplay*);
      void setInputs(NuggetInputs*);
      void setStrip(Adafruit_NeoPixel*);
      void setNuggetInterface(NuggetInterface*);
      int _update();
   protected:
      NuggetDisplay* display;
      NuggetInputs* inputs;
      Adafruit_NeoPixel* strip;
      void pushScreen(NuggetScreen*);
      void alwaysUpdates(bool);
   private:
      NuggetInterface* nuggetInterface;
      bool alwaysUpdate;
};

//----------------------------------------
// ScreenNode

struct ScreenNode {
   NuggetScreen* screen;
   volatile ScreenNode* prev;
};


//----------------------------------------
// NuggetInterface

class NuggetInterface {
  public:
    NuggetInterface();
    ~NuggetInterface();
    bool start();
    bool draw();
    bool pushScreen(NuggetScreen*);
    bool popScreen();
    bool injectScreen(NuggetScreen*);
    void idleLeds();   // screenless boards (Newsheen): set the whole ring to warm-white idle
  private:
    NuggetDisplay* display;
    NuggetInputs* inputs;
    Adafruit_NeoPixel* strip;
    volatile ScreenNode* currentScreenNode;
    volatile bool* currentScreenHasRendered;
    volatile SemaphoreHandle_t screenLock;
};


#endif

