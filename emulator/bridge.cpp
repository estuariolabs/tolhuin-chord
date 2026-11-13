// ABI del navegador. La aplicación, MIDI, DSP, controles y OLED son originales.
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif
#include "hal/Arduino.h"
#include "hal/Preferences.h"
#include "hal/Adafruit_TinyUSB.h"
#include "../tolhuin_chord_v2.ino"
#include "display240.h"
double emuTimeUs=0;
int emuPins[64];
int16_t emuAdc[2]={13200,13200};
uint8_t emuOled[128*64];
EmuSerial Serial;
void(*emuAudioTask)(void*)=nullptr;
static bool emuBoot=true;
void delay(uint32_t ms){if(!emuBoot)return;double target=emuTimeUs+ms*1000.;static double next=0;while(emuAudioTask&&next<target){emuAudioTask(nullptr);next+=256.*1000000./44100.;}emuTimeUs=target;}
// El audioTask original escribe pcm por I2S; su salida DSP se copia por este hook.
extern float* emuAudioLeft();
extern float* emuAudioRight();
static float output[512],scope[128];
static double nextControlUs=0;
static std::string text;
static std::string jsonEscape(const char* s){std::string out;for(;*s;s++){unsigned char c=*s;if(c=='"'||c=='\\'){out+='\\';out+=(char)c;}else if(c<32){char b[7];snprintf(b,sizeof b,"\\u%04x",c);out+=b;}else out+=(char)c;}return out;}

extern "C" {
EMSCRIPTEN_KEEPALIVE void th_init(){std::fill(emuPins,emuPins+64,HIGH);setup();emuBoot=false;nextControlUs=emuTimeUs;}
EMSCRIPTEN_KEEPALIVE void th_pin(int pin,int down){if(pin>=0&&pin<64)emuPins[pin]=down?LOW:HIGH;}
EMSCRIPTEN_KEEPALIVE void th_joy(float x,float y){ // Coordenadas lógicas; deshace JOY_SWAP_XY original.
 emuAdc[0]=(int16_t)(13200+y*2000);emuAdc[1]=(int16_t)(13200+x*2000);
}
EMSCRIPTEN_KEEPALIVE float* th_audio(){
 double end=emuTimeUs+256.0*1000000.0/44100.0;
 // Conserva el loop de control a 1 ms y el audio a bloques de 256, sin hilos.
 while(nextControlUs<end){emuTimeUs=nextControlUs;loop();nextControlUs+=1000.0;}
 emuTimeUs=end;emuAudioTask(nullptr);
 float* l=emuAudioLeft();float* r=emuAudioRight();
 for(int i=0;i<256;i++){ // Cuantización PCM5102 original, estéreo planar para Web Audio.
  output[i]=(int16_t)(std::max(-1.f,std::min(1.f,l[i]))*32767.f)/32768.f;
  output[256+i]=(int16_t)(std::max(-1.f,std::min(1.f,r[i]))*32767.f)/32768.f;
 }
 return output;
}
EMSCRIPTEN_KEEPALIVE void th_panic(){
 std::fill(emuPins,emuPins+64,HIGH);emuAdc[0]=emuAdc[1]=13200;
 app.hold=false;app.arpMode=0;app.drumsOn=false;app.metro=false;chordActive=false;
 applyArp();synthDrumsSet(0,app.bpm);synthMetroSet(false,app.bpm);synthReleaseAll();midiOut(nullptr,0);midiClockOutSet(false);app.clockOut=false;
 if(evloopState()==EVL_REC)evloopRecToggle(loopPos());if(evloopState()==EVL_PLAY)evloopPlayToggle(loopPos());loopMelNote=-1;
}
EMSCRIPTEN_KEEPALIVE void th_command(const char* s){webcfgFeedMidi(s,strlen(s));}
EMSCRIPTEN_KEEPALIVE const char* th_log(){text.swap(Serial.output);Serial.output.clear();return text.c_str();}
EMSCRIPTEN_KEEPALIVE uint8_t* th_oled(){return emuOled;}
EMSCRIPTEN_KEEPALIVE const uint16_t* th_screen(int theme){
 char sym[48];harmonyChordSymbolJoy(app.degree,app.zone,app.tonic,app.mode,app.preset,app.chordQual[app.degree%7],app.joyMap,sym,sizeof sym);
 synthGetScope(scope,128);int pg=0;for(int p=0;p<FX_NPAGES;p++)if(fxSel>=FX_PAGES[p].first&&fxSel<FX_PAGES[p].first+FX_PAGES[p].count)pg=p;
 char vals[3][32];Display240View v{};v.app=&app;v.chord=sym;v.timbre=synthTimbreName(synthTimbre());v.active=chordActive;v.menu=menuActive;v.title=FX_PAGES[pg].title;v.rows=FX_PAGES[pg].count;v.selected=fxSel-FX_PAGES[pg].first;
 for(int i=0;i<v.rows;i++){fxValue(FX_PAGES[pg].first+i,vals[i]);v.names[i]=FX_NAME[FX_PAGES[pg].first+i];v.values[i]=vals[i];}
 v.scope=scope;v.peak=synthGetLevel();v.loopPhase=evloopPhase(loopPos());v.loopState=evloopState();v.samples=synthSamplePos();return display240Render(v,theme);
}
EMSCRIPTEN_KEEPALIVE float* th_scope(){synthGetScope(scope,128);return scope;}
EMSCRIPTEN_KEEPALIVE const char* th_state(){
 static char b[4096];char sym[48];harmonyChordSymbolJoy(app.degree,app.zone,app.tonic,app.mode,app.preset,app.chordQual[app.degree%7],app.joyMap,sym,sizeof sym);
 char rows[1600]="";int pg=0;for(int p=0;p<FX_NPAGES;p++)if(fxSel>=FX_PAGES[p].first&&fxSel<FX_PAGES[p].first+FX_PAGES[p].count)pg=p; // Usa el mismo agrupamiento y valores del menú físico.
 for(int i=0;i<FX_PAGES[pg].count;i++){char val[32],row[180];int it=FX_PAGES[pg].first+i;fxValue(it,val);snprintf(row,sizeof row,"%s{\"id\":%d,\"name\":\"%s\",\"value\":\"%s\"}",i?",":"",it,FX_NAME[it],jsonEscape(val).c_str());strcat(rows,row);}
 snprintf(b,sizeof b,"{\"key\":%u,\"mode\":%u,\"degree\":%u,\"zone\":%u,\"voicing\":%u,\"octave\":%d,\"hold\":%d,\"strum\":%d,\"arp\":%u,\"bpm\":%u,\"timbre\":\"%s\",\"timbreId\":%u,\"chord\":\"%s\",\"active\":%d,\"drums\":%d,\"lead\":%u,\"loop\":%d,\"loopEvents\":%d,\"loopPhase\":%.4f,\"peak\":%.5f,\"samples\":%u,\"menu\":%d,\"selection\":%d,\"page\":%d,\"pageTitle\":\"%s\",\"rows\":[%s]}",app.tonic,app.mode,app.degree,app.zone,app.voicing,app.octaveOffset,app.hold,app.strum,app.arpMode,app.bpm,synthTimbreName(synthTimbre()),synthTimbre(),sym,chordActive,app.drumsOn,app.voiceLeadMode,evloopState(),evloopCount(),evloopPhase(loopPos()),synthGetLevel(),synthSamplePos(),menuActive,fxSel,pg,FX_PAGES[pg].title,rows);
 return b;
}
EMSCRIPTEN_KEEPALIVE void th_midi(int a,int b,int c,int n){uint8_t cin=n==1?15:((a>>4)&15);emuMidiIn.push_back({cin,(uint8_t)a,(uint8_t)b,(uint8_t)c});}
EMSCRIPTEN_KEEPALIVE void th_usb_packet(int cin,int a,int b,int c){emuMidiIn.push_back({(uint8_t)cin,(uint8_t)a,(uint8_t)b,(uint8_t)c});}
EMSCRIPTEN_KEEPALIVE int th_midi_out(){if(emuMidiOut.empty())return -1;auto p=emuMidiOut.front();emuMidiOut.pop_front();return p[0]|p[1]<<8|p[2]<<16|p[3]<<24;}
EMSCRIPTEN_KEEPALIVE const char* th_nvs(){static std::string s;s="{";const char* h="0123456789abcdef";for(auto& [k,v]:emuNvs){if(s.size()>1)s+=",";s+='"'+k+"\":\"";for(auto c:v){s+=h[c>>4];s+=h[c&15];}s+='"';}s+='}';return s.c_str();}
EMSCRIPTEN_KEEPALIVE void th_restore(const char* key,const char* hex){auto& v=emuNvs[key];v.clear();for(size_t i=0;i+1<strlen(hex);i+=2){char b[3]={hex[i],hex[i+1],0};v.push_back((uint8_t)strtoul(b,nullptr,16));}}
}
