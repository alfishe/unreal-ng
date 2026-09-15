#include "ymfm_opn.h"
#include <chrono>
#include <cstdio>
struct Intf : ymfm::ymfm_interface {};
struct Eng : ymfm::ym2203 { using ymfm::ym2203::ym2203; int16_t once(){ clock_fm(); return (int16_t)m_last_fm.data[0]; } };
static void w(Eng&e,int a,int d){ e.write_address(a); e.write_data(d);}
int main(){
  for(int mode=0; mode<2; mode++){
    Intf i; Eng e(i); e.reset();
    if(mode){ for(int ch=0;ch<3;ch++){ w(e,0xB0+ch,4); for(int op=0;op<4;op++){int o=op*4+ch; w(e,0x30+o,1+op); w(e,0x40+o,op==3?0:30); w(e,0x50+o,0x1f); w(e,0x60+o,2); w(e,0x70+o,1); w(e,0x80+o,0x28);} w(e,0xA4+ch,0x22); w(e,0xA0+ch,0x69); w(e,0x28,0xF0|ch);} }
    const int N=48611*20; volatile int64_t acc=0;
    auto t0=std::chrono::steady_clock::now(); for(int k=0;k<N;k++) acc+=e.once(); auto t1=std::chrono::steady_clock::now();
    double ns=std::chrono::duration<double,std::nano>(t1-t0).count()/N;
    printf("%s: %.1f ns/clock_fm  -> 2 chips per Pentagon frame (2x996 calls): %.1f us\n", mode?"3 ch x 4 op keyed":"silent (after reset)", ns, ns*2*996/1000);
  }
}
