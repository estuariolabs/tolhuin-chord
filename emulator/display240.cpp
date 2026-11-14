#include "display240.h"
#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <cstdio>
#include <cmath>
#include <algorithm>
namespace {
GFXcanvas16 frame(240,240);
struct Palette{uint16_t bg,panel,muted,ink,accent;};
constexpr uint16_t rgb(int r,int g,int b){return ((r>>3)<<11)|((g>>2)<<5)|(b>>3);}
const Palette palettes[]={
 {rgb(17,25,19),rgb(31,43,29),rgb(112,132,87),rgb(215,231,177),rgb(186,216,108)},
 {rgb(25,20,14),rgb(43,33,20),rgb(142,110,61),rgb(241,219,169),rgb(236,169,69)},
 {rgb(13,23,32),rgb(23,39,53),rgb(86,122,140),rgb(193,225,236),rgb(101,207,226)}
};
void text(const char* s,int x,int y,uint16_t c,int size=1){frame.setFont(nullptr);frame.setTextSize(size);frame.setTextColor(c);frame.setCursor(x,y);frame.print(s);}
void center(const char* s,int baseline,uint16_t c,int size){frame.setFont(&FreeSansBold12pt7b);frame.setTextSize(size);frame.setTextColor(c);int16_t x,y;uint16_t w,h;frame.getTextBounds(s,0,baseline,&x,&y,&w,&h);frame.setCursor((240-w)/2-x,baseline);frame.print(s);frame.setFont(nullptr);frame.setTextSize(1);}
void wave(const Display240View& v,int x,int y,int w,int h,Palette p,bool xy){frame.drawFastHLine(x,y+h/2,w,p.panel);if(!v.scope)return;float peak=.005f;for(int i=0;i<128;i++)peak=std::max(peak,std::fabs(v.scope[i]));float gain=std::min(12.f,.85f/peak);for(int i=1;i<128;i++){float a=v.scope[i-1]*gain,b=v.scope[i]*gain;if(xy){int j=(i+23)%128,jp=(i+22)%128;frame.drawLine(x+w/2+(int)(a*w*.43f),y+h/2+(int)(v.scope[jp]*gain*h*.43f),x+w/2+(int)(b*w*.43f),y+h/2+(int)(v.scope[j]*gain*h*.43f),p.accent);}else{frame.drawLine(x+(i-1)*w/128,y+h/2-(int)(a*h*.45f),x+i*w/128,y+h/2-(int)(b*h*.45f),p.accent);}}}
}
const uint16_t* display240Render(const Display240View& v,int theme){
 theme=std::max(0,std::min(2,theme));auto p=palettes[theme];const auto& a=*v.app;char b[80];frame.setTextWrap(false);frame.fillScreen(p.bg);
 text("TOLHUIN",12,12,p.ink);snprintf(b,sizeof b,"%03u BPM",a.bpm);text(b,180,12,p.muted);frame.drawFastHLine(12,29,216,p.panel);
 if(v.menu){text(v.title,12,43,p.accent,2);text("EDIT  < >     SELECT  ^ v",12,69,p.muted);for(int i=0;i<v.rows;i++){int y=91+i*37;bool selected=i==v.selected;frame.fillRoundRect(10,y,220,31,3,selected?p.accent:p.panel);uint16_t col=selected?p.bg:p.ink;text(v.names[i],17,y+6,col);text(v.values[i],17,y+18,col);}text("MENU: SALIR    /    AUTO 8s",12,223,p.muted);return frame.getBuffer();}
 const char* keys[]={"C","C#","D","Eb","E","F","F#","G","Ab","A","Bb","B"};snprintf(b,sizeof b,"%s %s",keys[a.tonic%12],a.mode?"MINOR":"MAJOR");text(b,12,42,p.muted);text(v.timbre,137,42,p.accent);
 if(theme==0){
  center(v.chord,112,p.ink,strlen(v.chord)>5?1:2);text(v.active?"VOICES ACTIVE":"READY TO PLAY",12,128,p.muted);frame.drawFastHLine(12,147,216,p.panel);wave(v,12,157,216,37,p,false);
 }else if(theme==1){
  center(v.chord,99,p.ink,strlen(v.chord)>5?1:2);text("RHYTHM / 16 STEPS",12,117,p.muted);int step=(int)((double)v.samples*a.bpm*4./(60.*44100.))%16;for(int i=0;i<16;i++){int x=12+(i%8)*27,y=137+(i/8)*22;frame.fillRoundRect(x,y,22,16,2,a.drumsOn&&i==step?p.accent:p.panel);snprintf(b,sizeof b,"%02d",i+1);text(b,x+5,y+4,a.drumsOn&&i==step?p.bg:p.muted);}text(a.drumsOn?"DRUMS ON":"DRUMS OFF",12,187,p.accent);snprintf(b,sizeof b,"LOOP %d",v.loopState);text(b,155,187,p.muted);
 }else{
  text(v.chord,12,64,p.ink,2);frame.drawRoundRect(12,91,216,106,4,p.panel);wave(v,19,98,202,92,p,true);text("XY / LIVE DSP",95,72,p.muted);
 }
 frame.drawFastHLine(12,207,216,p.panel);const char* degrees[]={"I","II","III","IV","V","VI","VII"};for(int i=0;i<7;i++){int x=12+i*31;bool active=v.active&&a.degree==i;frame.fillRoundRect(x,216,27,14,2,active?p.accent:p.panel);text(degrees[i],x+3,219,active?p.bg:p.muted);}
 if(a.hold)text("H",222,61,p.accent);if(a.arpMode)text("A",222,74,p.accent);if(a.strum)text("S",222,87,p.accent);if(v.loopState){frame.fillRect(12,237,(int)(216*v.loopPhase),2,p.accent);}
 return frame.getBuffer();
}
