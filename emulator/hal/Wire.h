#pragma once
struct TwoWire {int addr=0;void begin(int=0,int=0){} void setClock(int){} void beginTransmission(int a){addr=a;} int endTransmission(){return addr==0x3c||addr==0x49?0:2;}};
inline TwoWire Wire;
