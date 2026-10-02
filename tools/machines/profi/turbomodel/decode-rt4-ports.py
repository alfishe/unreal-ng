# Decode Profi v3.2 interface-board port decoder U5 (556RT4) dump Profi_RT4_line.BIN
# U5 wiring (MDESK re-trace, interface list 1): A0=ADR5 A1=ADR6 A2=X (line from U30 latch area) A3=ADR15
# A4=ADR7 A5=ADR1 A6=ADR0 A7=CP/M ; /CS0=/IORQ ; D0->F2 D1->F1 D2->F4 D3->F5 (active low)
import sys
d=open(sys.argv[1],'rb').read()
names=['F2','F1','F4','F5']
for a in range(256):
    v=d[a]&15
    if v==15: continue
    A5,A6,X,A15,A7,A1,A0,CPM=[(a>>i)&1 for i in range(8)]
    act=[names[i] for i in range(4) if not (v>>i)&1]
    port=(A15<<15)|(A7<<7)|(A6<<6)|(A5<<5)|(A1<<1)|A0
    print(f'addr {a:02X} CPM={CPM} X={X} A15={A15} A7={A7} A6={A6} A5={A5} A1={A1} A0={A0} -> pattern {port:04X} (other bits x) active {act}')
