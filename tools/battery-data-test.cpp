#include <cassert>
#include "../kernel/include/BluejayBatteryData.h"
int main() {
 BluejayBatterySample s={};
 auto decode=[&](unsigned soc,unsigned v,unsigned t,unsigned ok,unsigned state,unsigned path) {
  return bluejay_battery_decode(0x4040,soc,v,t,ok,state,path,&s);
 };
 assert(decode(50*256,51200,25*256,0x40,1,0x20));
 assert(s.percent==50 && s.mv==4000 && s.temperature==2500 && s.external && s.charging && !s.full);
 assert(decode(0,40000,0xfb00,0,8,0));
 assert(s.percent==0 && s.temperature==-500 && !s.external && !s.charging);
 assert(decode(100*256,55040,25*256,0x40,4,0x20));
 assert(s.percent==100 && s.full && !s.charging);
 for (unsigned state=6;state<16;state++) {
  assert(decode(99*256,51200,25*256,0x40,state,0x20)); assert(!s.charging && !s.full);
 }
 assert(decode(50*256,51200,25*256,0x40,1,0)); assert(!s.charging);
 assert(!decode(0xffff,51200,25*256,0x40,1,0x20));
 assert(!decode(50*256,0xffff,25*256,0x40,1,0x20));
 assert(!decode(50*256,0,25*256,0x40,1,0x20));
 assert(!decode(50*256,51200,0x7fff,0x40,1,0x20));
 assert(!bluejay_battery_decode(0xffff,12800,51200,6400,0x40,1,0x20,&s));
}
