#pragma once
#include <Adafruit_GFX.h>
#include "Wire.h"
#define SSD1306_WHITE 1
#define SSD1306_BLACK 0
#define SSD1306_INVERSE 2
#define SSD1306_SWITCHCAPVCC 2
extern uint8_t emuOled[128*64];
class Adafruit_SSD1306 : public Adafruit_GFX {
public:
 Adafruit_SSD1306(int w,int h,TwoWire*,int):Adafruit_GFX(w,h){}
 bool begin(int,int){return true;}
 void clearDisplay(){memset(emuOled,0,128*64);}
 void display(){}
 void drawPixel(int16_t x,int16_t y,uint16_t c) override {if(x<0||x>=128||y<0||y>=64)return;auto& p=emuOled[y*128+x];p=c==2?!p:!!c;}
};
