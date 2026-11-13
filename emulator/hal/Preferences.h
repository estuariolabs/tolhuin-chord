#pragma once
#include "Arduino.h"
#include <map>
inline std::map<std::string,std::vector<uint8_t>> emuNvs;
class Preferences {
public:
 bool begin(const char*,bool){return true;} void end(){}
 size_t putBytes(const char* k,const void* p,size_t n){emuNvs[k]={(const uint8_t*)p,(const uint8_t*)p+n};return n;}
 size_t getBytes(const char* k,void* p,size_t n){auto i=emuNvs.find(k);if(i==emuNvs.end())return 0;size_t s=std::min(n,i->second.size());memcpy(p,i->second.data(),s);return s;}
 bool isKey(const char* k){return emuNvs.count(k);}
 bool remove(const char* k){return emuNvs.erase(k);}
 size_t putUShort(const char* k,uint16_t x){return putBytes(k,&x,sizeof x);}
 size_t putUChar(const char* k,uint8_t x){return putBytes(k,&x,sizeof x);}
 size_t putFloat(const char* k,float x){return putBytes(k,&x,sizeof x);}
 uint16_t getUShort(const char* k,uint16_t x=0){getBytes(k,&x,sizeof x);return x;}
 uint8_t getUChar(const char* k,uint8_t x=0){getBytes(k,&x,sizeof x);return x;}
 float getFloat(const char* k,float x=0){getBytes(k,&x,sizeof x);return x;}
 size_t putString(const char* k,const char* s){return putBytes(k,s,strlen(s));}
 String getString(const char* k){auto& v=emuNvs[k];return String(v.begin(),v.end());}
};
