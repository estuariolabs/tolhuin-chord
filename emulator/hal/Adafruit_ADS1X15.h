#pragma once
#include "Arduino.h"
#define GAIN_ONE 1
#define RATE_ADS1115_860SPS 860
struct Adafruit_ADS1115 {void begin(int){}void setGain(int){}void setDataRate(int){}int16_t readADC_SingleEnded(int c){return emuAdc[c];}};
