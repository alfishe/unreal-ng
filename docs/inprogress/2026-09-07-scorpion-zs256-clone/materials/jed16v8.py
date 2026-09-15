import re,sys
t=open(sys.argv[1],'rb').read().decode('latin1')
fuses=[0]*2194
for m in re.finditer(r'L(\d+)\s+([01\s]+)\*',t):
    a=int(m.group(1)); bits=re.sub(r'\s','',m.group(2))
    for i,b in enumerate(bits): fuses[a+i]=int(b)
names={}
for m in re.finditer(r'NOTE PINS ([^*]+)\*',t):
    for p in m.group(1).split():
        n,num=p.split(':'); names[int(num)]=n
syn,ac0=fuses[2192],fuses[2193]
print('SYN',syn,'AC0',ac0,'-> mode', {(1,0):'simple',(1,1):'complex',(0,1):'registered'}.get((syn,ac0)))
colsrc=[2,1,3,19,4,18,5,17,6,16,7,15,8,14,9,13,11,12] # pairs: (pin2),(pin1/fb19)...
# 16V8 columns: 0-1 pin2, 2-3 pin1|fb19, 4-5 pin3, 6-7 pin18, 8-9 pin4,10-11 pin17,12-13 pin5,14-15 pin16,16-17 pin6,18-19 pin15,20-21 pin7,22-23 pin14,24-25 pin8,26-27 pin13,28-29 pin9,30-31 pin11|fb12
cols=[2,1,3,18,4,17,5,16,6,15,7,14,8,13,9,11]
if syn==1: cols[1]=1; cols[15]=11  # simple/complex: pin1, pin11 inputs
def term(row):
    lits=[]
    for k,src in enumerate(cols):
        tt=fuses[row*32+2*k]; c=fuses[row*32+2*k+1]
        n=names.get(src,f'pin{src}')
        if tt==0 and c==0: return None
        if tt==0: lits.append(n)
        elif c==0: lits.append('!'+n)
    return ' & '.join(lits) if lits else '1'
for i,p in enumerate(range(19,11,-1)):
    xor=fuses[2048+i]; ac1=fuses[2120+i]
    pts=[]
    for j in range(8):
        r=i*8+j
        if fuses[2128+r]==0: continue  # PT disabled
        tt=term(r)
        if tt is not None: pts.append(tt)
    n=names.get(p,f'pin{p}')
    print(f'\n-- pin {p} ({n}) XOR={xor} AC1={ac1} ({"input/feedback" if (syn==1 and ac1==1) else "output"})')
    if pts: print(('!' if xor==0 else '')+n,'=\n    '+'\n  + '.join(pts))
