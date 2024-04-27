#ifndef UTILS_H
#define UTILS_H

#include "Arduino.h"
#include <base64.h>
// #include "base64.hpp"
#include "arduino_base64.hpp"
#include "cdcusb.h"
#include "mscusb.h"
#include "flashdisk.h"

struct fileOp {
  bool ok;
  String result;
};

// saveFile attempts to write the file, creating parent directories as
// needed. the path should start with '/' and not end with '/'.
fileOp saveFile(String path, String contents);
fileOp readFile(String path);
fileOp base64Decode(String encoded);

#endif