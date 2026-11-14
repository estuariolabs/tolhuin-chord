#pragma once
#include "Arduino.h"
inline std::deque<std::array<uint8_t,4>> emuMidiIn,emuMidiOut;
struct Adafruit_USBD_MIDI {
 void setStringDescriptor(const char*){}void begin(){}
 bool readPacket(uint8_t* p){if(emuMidiIn.empty())return false;memcpy(p,emuMidiIn.front().data(),4);emuMidiIn.pop_front();return true;}
 bool writePacket(const uint8_t* p){emuMidiOut.push_back({p[0],p[1],p[2],p[3]});return true;}
};
struct EmuUSB {void setManufacturerDescriptor(const char*){}void setProductDescriptor(const char*){}bool mounted(){return false;}void detach(){}void attach(){}};
inline EmuUSB TinyUSBDevice;
inline int tud_cdc_available(){return 0;}
inline int32_t tud_cdc_read_char(){return -1;}
