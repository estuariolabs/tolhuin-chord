#include <cstdio>
#include <cstdint>
extern "C" {void th_init();void th_pin(int,int);void th_joy(float,float);float* th_audio();const char* th_state();void th_command(const char*);}
int main(){th_init();for(int b=0;b<600;b++){
 if(b==10)th_pin(8,1);if(b==80)th_joy(0,1);if(b==150)th_joy(0,0);
 if(b==180)th_pin(8,0);if(b==220)th_command("#timbre 1");
 if(b==230)th_pin(5,1);if(b==320)th_pin(5,0);
 if(b==380)th_command("#drums 1 120");if(b==520)th_command("#drums 0 120");
 auto p=th_audio();fwrite(p,sizeof(float),512,stdout);
 }fprintf(stderr,"%s\n",th_state());}
