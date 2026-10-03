# MCS-48 (8035) simulator + PROFI-XT board model (schematic PROFI-XT.PDF)
import sys
from mcs48 import T as OPT
class CPU:
    def __init__(s,rom):
        s.rom=rom; s.ram=[0]*64; s.reset()
        s.addrHigh=0xFF; s.waitActive=False; s.latch=0xFF; s.t0=1; s.intPin=1; s.cycles=0
        s.trace=None; s.latchWrites=[]
    def reset(s):
        s.pc=0;s.a=0;s.c=0;s.ac=0;s.f0=0;s.f1=0;s.bs=0;s.sp=0;s.mb=0
        s.p1=0xFF;s.p2=0xFF;s.ie=0;s.inIsr=0;s.bus=0
    def r(s,n): return s.ram[(s.bs*24)+n]
    def wr(s,n,v): s.ram[(s.bs*24)+n]=v&0xFF
    def psw(s): return (s.c<<7)|(s.ac<<6)|(s.f0<<5)|(s.bs<<4)|0x08|s.sp
    def setpsw(s,v): s.c=v>>7&1;s.ac=v>>6&1;s.f0=v>>5&1;s.bs=v>>4&1;s.sp=v&7
    def push(s):
        a=8+2*s.sp; s.ram[a]=s.pc&0xFF; s.ram[a+1]=((s.pc>>8)&0x0F)|(s.psw()&0xF0); s.sp=(s.sp+1)&7
    def pop(s,restore):
        s.sp=(s.sp-1)&7;a=8+2*s.sp;s.pc=s.ram[a]|((s.ram[a+1]&0x0F)<<8)
        if restore: v=s.ram[a+1]; s.c=v>>7&1;s.ac=v>>6&1;s.f0=v>>5&1;s.bs=v>>4&1
    def fetch(s):
        v=s.rom[s.pc&0x7FF]; s.pc=(s.pc&0x800)|((s.pc+1)&0x7FF); return v
    def movxw(s,addr,v):
        if not (addr&0x80):
            s.latch=v; s.latchWrites.append(v)
        if not (addr&0x20):
            s.waitActive=False
    def t1(s): return 0 if s.waitActive else 1
    def p1in(s): return s.p1 & s.addrHigh
    def step(s):
        if s.ie and not s.inIsr and s.intPin==0:
            s.intPin=1  # edge consumed (monostable pulse)
            s.push(); s.pc=3; s.inIsr=1; s.cycles+=2; return
        pc0=s.pc; op=s.fetch(); s.cycles+=1
        if s.trace is not None: s.trace.append(pc0)
        name=OPT.get(op,('DB',1))[0]
        two=OPT.get(op,('',1))[1]==2
        b=s.fetch() if two else None
        if two: s.cycles+=1
        A=s.a
        def at(i): return s.r(i)&0x3F
        def jc(cond):
            if cond: s.pc=((pc0+1)&0xF00)|b
        def add(v,cy=0):
            t=s.a+v+cy; s.ac=1 if ((s.a&0xF)+(v&0xF)+cy)>0xF else 0; s.c=1 if t>0xFF else 0; s.a=t&0xFF
        lo=op&7
        if op==0x00: pass
        elif op in (0x04,0x24,0x44,0x64,0x84,0xA4,0xC4,0xE4): s.pc=(s.mb<<11)|((op>>5)<<8)|b
        elif op in (0x14,0x34,0x54,0x74,0x94,0xB4,0xD4,0xF4): s.push(); s.pc=(s.mb<<11)|((op>>5)<<8)|b
        elif op==0x83: s.pop(False)
        elif op==0x93: s.pop(True); s.inIsr=0
        elif op==0x03: add(b)
        elif op==0x13: add(b,s.c)
        elif 0x68<=op<=0x6F: add(s.r(lo))
        elif op in (0x60,0x61): add(s.ram[at(op&1)])
        elif 0x78<=op<=0x7F: add(s.r(lo),s.c)
        elif op in (0x70,0x71): add(s.ram[at(op&1)],s.c)
        elif op==0x23: s.a=b
        elif op==0x43: s.a|=b
        elif op==0x53: s.a&=b
        elif op==0xD3: s.a^=b
        elif 0x48<=op<=0x4F: s.a|=s.r(lo)
        elif 0x58<=op<=0x5F: s.a&=s.r(lo)
        elif 0xD8<=op<=0xDF: s.a^=s.r(lo)
        elif op in (0x40,0x41): s.a|=s.ram[at(op&1)]
        elif op in (0x50,0x51): s.a&=s.ram[at(op&1)]
        elif op in (0xD0,0xD1): s.a^=s.ram[at(op&1)]
        elif 0xF8<=op<=0xFF: s.a=s.r(lo)
        elif op in (0xF0,0xF1): s.a=s.ram[at(op&1)]
        elif 0xA8<=op<=0xAF: s.wr(lo,s.a)
        elif op in (0xA0,0xA1): s.ram[at(op&1)]=s.a
        elif 0xB8<=op<=0xBF: s.wr(lo,b)
        elif op in (0xB0,0xB1): s.ram[at(op&1)]=b
        elif 0x28<=op<=0x2F: t=s.r(lo); s.wr(lo,s.a); s.a=t
        elif op in (0x20,0x21): i=at(op&1); t=s.ram[i]; s.ram[i]=s.a; s.a=t
        elif op in (0x30,0x31): i=at(op&1); t=s.ram[i]; s.ram[i]=(t&0xF0)|(s.a&0xF); s.a=(s.a&0xF0)|(t&0xF)
        elif 0x18<=op<=0x1F: s.wr(lo,s.r(lo)+1)
        elif op in (0x10,0x11): i=at(op&1); s.ram[i]=(s.ram[i]+1)&0xFF
        elif 0xC8<=op<=0xCF: s.wr(lo,s.r(lo)-1)
        elif op==0x17: s.a=(s.a+1)&0xFF
        elif op==0x07: s.a=(s.a-1)&0xFF
        elif op==0x27: s.a=0
        elif op==0x37: s.a^=0xFF
        elif op==0x47: s.a=((s.a<<4)|(s.a>>4))&0xFF
        elif op==0x77: s.a=((s.a>>1)|(s.a<<7))&0xFF
        elif op==0xE7: s.a=((s.a<<1)|(s.a>>7))&0xFF
        elif op==0x67: c=s.a&1; s.a=(s.a>>1)|(s.c<<7); s.c=c
        elif op==0xF7: c=s.a>>7; s.a=((s.a<<1)|s.c)&0xFF; s.c=c
        elif op==0x97: s.c=0
        elif op==0xA7: s.c^=1
        elif op==0x85: s.f0=0
        elif op==0x95: s.f0^=1
        elif op==0xA5: s.f1=0
        elif op==0xB5: s.f1^=1
        elif op==0xC5: s.bs=0
        elif op==0xD5: s.bs=1
        elif op==0xE5: s.mb=0
        elif op==0xF5: s.mb=1
        elif op==0xC7: s.a=s.psw()
        elif op==0xD7: s.setpsw(s.a)
        elif op==0x05: s.ie=1
        elif op==0x15: s.ie=0
        elif op in (0x25,0x35,0x45,0x55,0x65,0x75,0x62): pass
        elif op==0x42: s.a=0
        elif 0xE8<=op<=0xEF:
            v=(s.r(lo)-1)&0xFF; s.wr(lo,v); jc(v!=0)
        elif op in (0x12,0x32,0x52,0x72,0x92,0xB2,0xD2,0xF2): jc(s.a>>(op>>5)&1)
        elif op==0xC6: jc(s.a==0)
        elif op==0x96: jc(s.a!=0)
        elif op==0xF6: jc(s.c)
        elif op==0xE6: jc(not s.c)
        elif op==0xB6: jc(s.f0)
        elif op==0x76: jc(s.f1)
        elif op==0x36: jc(s.t0)
        elif op==0x26: jc(not s.t0)
        elif op==0x56: jc(s.t1())
        elif op==0x46: jc(not s.t1())
        elif op==0x86: jc(s.intPin==0)
        elif op==0x16: jc(False)
        elif op==0xA3: s.a=s.rom[(pc0&0xF00)|s.a]
        elif op==0xE3: s.a=s.rom[0x300|s.a]
        elif op==0xB3: s.pc=(pc0&0xF00)|s.rom[(pc0&0xF00)|s.a]
        elif op==0x09: s.a=s.p1in()
        elif op==0x0A: s.a=s.p2
        elif op==0x39: s.p1=s.a
        elif op==0x3A: s.p2=s.a
        elif op==0x89: s.p1|=b
        elif op==0x99: s.p1&=b
        elif op==0x8A: s.p2|=b
        elif op==0x9A: s.p2&=b
        elif op in (0x90,0x91): s.movxw(s.r(op&1),s.a)
        elif op in (0x80,0x81): s.a=0xFF
        elif op==0x02: s.bus=s.a
        else: raise Exception('op %02X at %03X'%(op,pc0))
# ---- board model
class Board:
    def __init__(s,rom):
        s.cpu=CPU(rom)
    def run(s,n):
        for _ in range(n): s.cpu.step()
    def sendByte(s,v,gap=40):
        bits=[1]+[(v>>i)&1 for i in range(8)]   # XT: start bit 1, then LSB first
        for d in bits:
            s.cpu.t0 = 0 if d else 1   # T0 = /Q of DD5 (inverted data)
            s.cpu.intPin=0
            s.run(gap)
        s.run(300)
    def readFE(s,high,maxc=2000):
        c=s.cpu
        if not (c.p2&0x80):        # P27=0: no WAIT, latch outputs off -> pull-ups
            return 0x3F, 0
        c.addrHigh=high; c.waitActive=True; n=0
        while c.waitActive and n<maxc: c.step(); n+=1
        c.addrHigh=0xFF
        return (c.latch&0x3F) if not c.waitActive else None, n
