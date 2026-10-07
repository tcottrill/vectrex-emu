#include <cstdio>
#include <cstring>
#include "cpu_m6809.h"
#include "cpu_control.h"
#include "vecx.h"
#include "via6522.h"
#include "ay8910.h"
#include "sys_log.h"
namespace Log { void write(Level,const char*,const char*,int,const char*,...){} }
static int samples=0,blocks=0,failures=0;
int mixer_alloc_channel(int,int){return 0;}
void stream_start(int,int,int,int,bool){}
void stream_stop(int,int){}
void sample_set_volume_mixer(int,int){}
void stream_update(int,short*){samples+=882;}
void mixer_update_sync(){blocks++;}
void osint_render(){}
void check(const char* name,int actual,int expected){std::printf("%s: %d expected %d\n",name,actual,expected);failures+=actual!=expected;}
int main(){
 AY8910Config cfg={};cfg.num_chips=1;cfg.base_clock=1500000;ay8910_sh_start(&cfg);
 const int rates[]={30,50,60,144,480};
 for(int rate:rates){
   std::memset(g_cpu_mem,0x12,65536);g_cpu_mem[0xfffe]=0;g_cpu_mem[0xffff]=0;
   // Three-cycle infinite branch loop; adjust per-call budget from actual execution.
   g_cpu_mem[0]=0x20;g_cpu_mem[1]=0xfe;vecx_reset();samples=blocks=0;
   int executed=0;
   for(int frame=1;frame<=rate;frame++){
     int target=(int)((1500000LL*frame)/rate);vecx_emu(target-executed);executed+=g_cpu->get_ticks(0);
   }
   std::printf("refresh %d: ",rate);check("sample count",samples,44100);check("block count",blocks,50);
 }
 via_hook_on_orb_written(0x18,1);via_hook_on_orb_written(0x10,0xff);
 check("VIA reads masked AY R1",via_hook_read_port_a(8,0),15);
 snd_regs[14]=0xa5;via_hook_on_orb_written(0x18,14);
 check("Button port survives",via_hook_read_port_a(8,0),0xa5);
 ay8910_sh_stop();return failures?1:0;
}
