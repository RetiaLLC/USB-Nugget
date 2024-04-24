#pragma once

#include "Arduino.h"
#include "cdcusb.h"
#include "mscusb.h"
#include "flashdisk.h"

enum LoraModuleType {RFM95, RYLR998};

class RubberNugget {
  public:
    RubberNugget();
    static void init();
    static String* allPayloadPaths(const char* path="/");
};

struct NuggetConfig {
  String locale;
  String network;
  String password;
  long pid;
  long vid;
  uint8_t lora_module_type;
  int lora_RFM_syncword;

};

FILINFO* newFileList(const char* path, int& numFiles);
NuggetConfig getConfig();
