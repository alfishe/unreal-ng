// TTD stress: randomized YM2203 register traffic driven on a T-state (=master clock) axis.
// Checks (see verification-report.md §D):
//   P1  a chip that saves state behaves identically to one that never saves
//   P2  a fresh chip restored from any checkpoint continues identically to the never-saved chip
// Build against a ymfm/src copy (patched or upstream):
//   c++ -std=c++17 -O2 -I<ymfm/src> stress.cpp <ymfm/src>/ymfm_opn.cpp <ymfm/src>/ymfm_ssg.cpp <ymfm/src>/ymfm_adpcm.cpp -o stress
// Prints PASS with verification/ymfm-ttd.patch applied, FAIL on upstream 81aec25c.
#include "ymfm_opn.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <vector>
struct Intf : ymfm::ymfm_interface {
  int32_t tr[2]={-1,-1}; int32_t busy=0;
  void ymfm_set_timer(uint32_t t,int32_t d) override { tr[t]=d; }
  void ymfm_set_busy_end(uint32_t c) override { busy=(int32_t)c; }
  bool ymfm_is_busy() override { return busy>0; }
  void adv(int32_t c){ for(int t=0;t<2;t++) if(tr[t]>0 && (tr[t]-=c)<=0){ int32_t over=-tr[t]; tr[t]=0; m_engine->engine_timer_expired(t); if(tr[t]>0) tr[t]-=over; } if(busy>0) busy-=c; }
};
struct Eng : ymfm::ym2203 { using ymfm::ym2203::ym2203; int16_t once(){ clock_fm(); return (int16_t)m_last_fm.data[0]; } uint32_t pre() const { return m_fm.clock_prescale(); } };
struct Chip { Intf i; Eng e{i}; int32_t phase=0; Chip(){ e.reset(); } };
static void save(Chip&c,std::vector<uint8_t>&b){ ymfm::ymfm_saved_state st(b,true); c.e.save_restore(st); }
static void restore(Chip&c,std::vector<uint8_t>&b,const Chip&src){ ymfm::ymfm_saved_state st(b,false); c.e.save_restore(st); c.i.tr[0]=src.i.tr[0]; c.i.tr[1]=src.i.tr[1]; c.i.busy=src.i.busy; c.phase=src.phase; }
// one T-state step of 8 clocks (a half AY tick); clock FM every 12*prescale clocks
static int step(Chip&c, uint64_t& hash){ c.i.adv(8); c.phase+=8; int n=0; while(c.phase >= (int32_t)(12*c.e.pre())){ c.phase-=12*c.e.pre(); int16_t w=c.e.once(); hash=hash*1000003u ^ (uint16_t)w; n++; } hash = hash*31 ^ c.e.read_status(); return n; }
struct Ev { uint64_t at; uint8_t a,d; };
static std::vector<Ev> gen(uint32_t seed, uint64_t steps){
  std::mt19937 r(seed); std::vector<Ev> v; uint64_t t=0;
  auto W=[&](uint8_t a,uint8_t d){ v.push_back({t,a,d}); };
  // voices
  while(t<steps){
    t += r()%600;
    int k=r()%100;
    if(k<30){ W(0x28, (uint8_t)((r()%16)<<4 | (r()%3))); }
    else if(k<55){ uint8_t base[]={0x30,0x40,0x50,0x60,0x70,0x80,0x90}; W(base[r()%7] + (uint8_t)(r()%16), (uint8_t)r()); }
    else if(k<70){ W(0xA0 + (uint8_t)(r()%3), (uint8_t)r()); W(0xA4 + (uint8_t)(r()%3), (uint8_t)(r()&0x1F)); }
    else if(k<78){ W(0xA8 + (uint8_t)(r()%3), (uint8_t)r()); W(0xAC + (uint8_t)(r()%3), (uint8_t)(r()&0x1F)); }
    else if(k<85){ W(0xB0 + (uint8_t)(r()%3), (uint8_t)r()); }
    else if(k<93){ W(0x24,(uint8_t)r()); W(0x25,(uint8_t)r()); W(0x26,(uint8_t)r()); }
    else if(k<98){ static const uint8_t m[]={0x00,0x15,0x3F,0x85,0x8F,0x45,0x2A,0xBF}; W(0x27, m[r()%8]); }
    else { W(r()%2?0x2F:0x2D, 0); if(r()%4==0) W(0x2E,0); W(0x2D,0); }
  }
  return v;
}
static void apply(Chip&c,const Ev&e){ c.e.write_address(e.a); if(e.a<0x2D || e.a>0x2F) c.e.write_data(e.d); }
int main(){
  const uint64_t STEPS = 4'000'000; // 8 clocks each = 32M clocks ≈ 9 s of chip time
  int fails=0;
  for(uint32_t seed=1; seed<=6; seed++){
    auto ev=gen(seed,STEPS);
    // Run A: never saves. Run B: saves every `every` steps (side-effect check).
    // Run C: at checkpoints, restore a fresh chip from B's save and continue it to the next checkpoint, compare.
    Chip A, B; uint64_t hA=0,hB=0; size_t ia=0; std::vector<uint8_t> buf; buf.reserve(1024);
    uint64_t every = 1 + (seed*7919)%5000; if(seed==1) every=1;
    uint64_t restoreDiff=0, sideDiff=0; size_t stSize=0; bool sizeStable=true;
    std::unique_ptr<Chip> C; uint64_t hC=0;
    for(uint64_t s=0; s<STEPS; s++){
      while(ia<ev.size() && ev[ia].at==s){ apply(A,ev[ia]); apply(B,ev[ia]); if(C) apply(*C,ev[ia]); ia++; }
      step(A,hA); step(B,hB); if(C) step(*C,hC);
      if(s % every == 0){
        save(B,buf); if(!stSize) stSize=buf.size(); else if(stSize!=buf.size()) sizeStable=false;
        if(C){ // compare C (restored at previous checkpoint) against B now
          if(hC!=hA) restoreDiff++;  // output+status stream of restored chip vs never-saved reference
        }
        C.reset(new Chip()); restore(*C,buf,B); hC=hA;
        { Chip D; restore(D,buf,B); std::vector<uint8_t> again; save(D,again); if(again!=buf) restoreDiff++; }  // byte round-trip on a throwaway chip, so C is never saved
      }
    }
    if(hA!=hB) sideDiff=1;
    printf("seed %u every %llu events %zu: save side-effect %s | restore-continue mismatches %llu | state %zu bytes %s\n",
      seed,(unsigned long long)every,ev.size(), sideDiff?"DIFFERS":"none", (unsigned long long)restoreDiff, stSize, sizeStable?"fixed":"VARIES");
    fails += sideDiff + (restoreDiff?1:0) + (sizeStable?0:1);
  }
  printf(fails? "FAIL\n":"PASS\n"); return fails?1:0;
}
