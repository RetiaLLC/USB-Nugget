#pragma once
#ifndef RUBBERNUGGET_H
#define RUBBERNUGGET_H

#include "Arduino.h"
#include "cdcusb.h"
#include "mscusb.h"
#include "flashdisk.h"
#include "nuggetLora/nuggetLora.h"
#include "nuggetConf.h"



class RubberNugget {
  public:
    RubberNugget(){};
    static void init();
    static String* allPayloadPaths(const char* path="/");
};


FILINFO* newFileList(const char* path, int& numFiles);
NuggetConfig getConfig();


#endif