#pragma once
// Adaptador Arduino para host/WASM: el firmware original conserva su lógica.
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <deque>
#include <array>
#include <algorithm>
#include <cstdarg>
#define PI 3.14159265358979323846
#define PROGMEM
#define INPUT_PULLUP 2
#define HIGH 1
#define LOW 0
#define F(x) (reinterpret_cast<const __FlashStringHelper*>(x))
#define pgm_read_byte(p) (*(const uint8_t*)(p))
#define pgm_read_word(p) (*(const uint16_t*)(p))
#define pgm_read_dword(p) (*(const uintptr_t*)(p))
class __FlashStringHelper;
using String = std::string;
using std::min; using std::max;
extern double emuTimeUs;
extern int emuPins[64];
extern int16_t emuAdc[2];
inline uint32_t millis(){return (uint32_t)(emuTimeUs/1000.0);}
inline uint32_t micros(){return (uint32_t)emuTimeUs;}
inline int64_t esp_timer_get_time(){return (int64_t)emuTimeUs;}
void delay(uint32_t ms);
inline void pinMode(int,int){}
inline int digitalRead(int pin){return emuPins[pin];}
inline void* ps_malloc(size_t n){return malloc(n);}
inline void yield(){}
class Print {
public:
 virtual ~Print()=default;
 virtual size_t write(uint8_t)=0;
 size_t print(const char* s){size_t n=0;while(*s)n+=write((uint8_t)*s++);return n;}
 size_t print(char* s){return print((const char*)s);}
 size_t print(const String& s){return print(s.c_str());}
 size_t print(const __FlashStringHelper* s){return print((const char*)s);}
 size_t print(char c){return write(c);}
 template<class T> size_t print(T x){char b[64];snprintf(b,sizeof(b),"%ld",(long)x);return print(b);}
 size_t println(const char* s=""){return print(s)+write('\n');}
 int printf(const char* f,...){char b[1024];va_list a;va_start(a,f);int n=vsnprintf(b,sizeof b,f,a);va_end(a);print(b);return n;}
};
class EmuSerial : public Print {
public:
 std::string output;
 void begin(int){}
 size_t write(uint8_t c) override {output+=(char)c;if(output.size()>32768)output.erase(0,16384);return 1;}
 int available(){return 0;}
 int read(){return -1;}
};
extern EmuSerial Serial;
struct EmuQueue {size_t max,size;std::deque<std::vector<uint8_t>> q;};
using QueueHandle_t=EmuQueue*;
#define pdTRUE 1
inline QueueHandle_t xQueueCreate(size_t n,size_t s){return new EmuQueue{n,s,{}};}
inline int xQueueSend(QueueHandle_t q,const void* p,int){if(q->q.size()>=q->max)return 0;q->q.emplace_back((const uint8_t*)p,(const uint8_t*)p+q->size);return 1;}
inline int xQueueReceive(QueueHandle_t q,void* p,int){if(q->q.empty())return 0;memcpy(p,q->q.front().data(),q->size);q->q.pop_front();return 1;}
extern void(*emuAudioTask)(void*);
inline int xTaskCreatePinnedToCore(void(*fn)(void*),const char*,int,void*,int,void*,int){emuAudioTask=fn;return 1;}
