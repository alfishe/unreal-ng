# Minimal MCS-48 (8035/8048) disassembler
import sys
R=lambda o:o&7
def build():
    t={}
    one={0x00:'NOP',0x02:'OUTL BUS,A',0x05:'EN I',0x07:'DEC A',0x08:'INS A,BUS',0x09:'IN A,P1',0x0A:'IN A,P2',
     0x15:'DIS I',0x17:'INC A',0x25:'EN TCNTI',0x27:'CLR A',0x35:'DIS TCNTI',0x37:'CPL A',0x39:'OUTL P1,A',0x3A:'OUTL P2,A',
     0x42:'MOV A,T',0x45:'STRT CNT',0x47:'SWAP A',0x55:'STRT T',0x57:'DA A',0x62:'MOV T,A',0x65:'STOP TCNT',0x67:'RRC A',
     0x75:'ENT0 CLK',0x77:'RR A',0x80:'MOVX A,@R0',0x81:'MOVX A,@R1',0x83:'RET',0x85:'CLR F0',0x90:'MOVX @R0,A',0x91:'MOVX @R1,A',
     0x93:'RETR',0x95:'CPL F0',0x97:'CLR C',0xA3:'MOVP A,@A',0xA5:'CLR F1',0xA7:'CPL C',0xB3:'JMPP @A',0xB5:'CPL F1',
     0xC5:'SEL RB0',0xC7:'MOV A,PSW',0xD5:'SEL RB1',0xD7:'MOV PSW,A',0xE3:'MOVP3 A,@A',0xE5:'SEL MB0',0xE7:'RL A',0xF5:'SEL MB1',0xF7:'RLC A'}
    for k,v in one.items(): t[k]=(v,1)
    imm={0x03:'ADD A,#',0x13:'ADDC A,#',0x23:'MOV A,#',0x43:'ORL A,#',0x53:'ANL A,#',0xD3:'XRL A,#',0x88:'ORL BUS,#',0x89:'ORL P1,#',0x8A:'ORL P2,#',0x98:'ANL BUS,#',0x99:'ANL P1,#',0x9A:'ANL P2,#'}
    for k,v in imm.items(): t[k]=(v,2,'imm')
    for i,r in ((0,'@R0'),(1,'@R1')):
        for b,n in ((0x10,'INC '),(0x20,'XCH A,'),(0x30,'XCHD A,'),(0x40,'ORL A,'),(0x50,'ANL A,'),(0x60,'ADD A,'),(0x70,'ADDC A,'),(0xA0,'MOV %s,A'),(0xD0,'XRL A,'),(0xF0,'MOV A,')):
            t[b+i]=((n%r) if '%' in n else n+r,1)
        t[0xB0+i]=('MOV %s,#'%r,2,'imm')
    for r in range(8):
        for b,n in ((0x18,'INC R%d'),(0x28,'XCH A,R%d'),(0x48,'ORL A,R%d'),(0x58,'ANL A,R%d'),(0x68,'ADD A,R%d'),(0x78,'ADDC A,R%d'),(0xA8,'MOV R%d,A'),(0xC8,'DEC R%d'),(0xD8,'XRL A,R%d'),(0xF8,'MOV A,R%d')):
            t[b+r]=(n%r,1)
        t[0xB8+r]=('MOV R%d,#'%r,2,'imm')
        t[0xE8+r]=('DJNZ R%d,'%r,2,'jc')
    for p in range(4):
        t[0x0C+p]=('MOVD A,P%d'%(p+4),1);t[0x3C+p]=('MOVD P%d,A'%(p+4),1);t[0x8C+p]=('ORLD P%d,A'%(p+4),1);t[0x9C+p]=('ANLD P%d,A'%(p+4),1)
    for b in range(8):
        t[0x12+b*0x20]=('JB%d '%b,2,'jc')
        t[0x04+b*0x20]=('JMP ',2,'jmp')
        t[0x14+b*0x20]=('CALL ',2,'jmp')
    for k,v in {0x16:'JTF ',0x26:'JNT0 ',0x36:'JT0 ',0x46:'JNT1 ',0x56:'JT1 ',0x76:'JF1 ',0x86:'JNI ',0x96:'JNZ ',0xB6:'JF0 ',0xC6:'JZ ',0xE6:'JNC ',0xF6:'JC '}.items():
        t[k]=(v,2,'jc')
    return t
T=build()
def dis(rom,pc):
    op=rom[pc];e=T.get(op)
    if not e: return ('DB %02Xh'%op,1,None)
    if e[1]==1: return (e[0],1,None)
    b=rom[(pc+1)&0x7ff];kind=e[2]
    if kind=='imm': return (e[0]+'%02Xh'%b,2,None)
    if kind=='jmp':
        tgt=((op>>5)<<8)|b|(pc&0x800); return (e[0]+'%03Xh'%tgt,2,tgt)
    tgt=((pc+1)&0xF00)|b; return (e[0]+'%03Xh'%tgt,2,tgt)
if __name__=='__main__':
    rom=open(sys.argv[1],'rb').read()
    s=int(sys.argv[2],16);n=int(sys.argv[3],16)
    pc=s
    while pc<s+n:
        txt,l,_=dis(rom,pc)
        print('%03X: %-6s %s'%(pc,' '.join('%02X'%rom[pc+i] for i in range(l)),txt));pc+=l
