#include <cstdio>
#include <vector>
#include "../project/ay8910.cpp"
namespace Log { void write(Level,const char*,const char*,int,const char*,...){} }
static int position=0, submitted=0;
static std::vector<short> pcm;
int cpu_scale_by_cycles(int,int){return position;}
int mixer_alloc_channel(int,int){return 0;}
void stream_start(int,int,int,int,bool){}
void stream_stop(int,int){}
void sample_set_volume_mixer(int,int){}
void stream_update(int,short* data){submitted+=882;pcm.assign(data,data+882);}
void reg(int r,int v){ay8910_write(0,0,(uint8_t)r);ay8910_write(0,1,(uint8_t)v);}
int main(){
 AY8910Config cfg={};cfg.num_chips=1;cfg.base_clock=1500000;cfg.mixing_level[0]=255;
 ay8910_sh_start(&cfg);
 reg(7,0x3f);reg(8,0);position=1;reg(8,15);position=5;reg(8,0);ay8910_sh_update();
 int nonzero=0;for(short x:pcm)nonzero+=x!=0;
 std::printf("Pulse between sample 1 and 5: nonzero samples=%d (expected nonzero)\n",nonzero);
 AY8910Chip chip;chip.configure(0,1500000,44100,nullptr,nullptr,nullptr,nullptr);chip.reset();
 auto wr=[&](int r,int v){chip.write_addr((uint8_t)r);chip.write_data((uint8_t)v);};
 wr(7,0x3f);wr(8,0x10);wr(11,100);wr(12,0);wr(13,8);
 // Undo the known DC-block recurrence to inspect the underlying DAC staircase.
 short out;int previous=0,raw=0,changes=0;
 for(int i=0;i<700;i++){chip.render(&out,1);int next=out+raw-(int)(((long long)previous*4076)>>12);
   if(i>0 && next!=raw)changes++;raw=next;previous=out;}
 std::printf("Envelope level changes in 700 samples: %d (AY expected about 14, YM about 29)\n",changes);
 ay8910_sh_stop();
 return nonzero > 0 && changes == 14 ? 0 : 1;
}
