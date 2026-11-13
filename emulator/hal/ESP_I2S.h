#pragma once
#include "Arduino.h"
#define I2S_MODE_STD 0
#define I2S_DATA_BIT_WIDTH_16BIT 0
#define I2S_SLOT_MODE_STEREO 0
struct I2SClass {void setPins(int,int,int,int,int){}bool begin(int,int,int,int){return true;}size_t write(uint8_t*,size_t n){return n;}};
