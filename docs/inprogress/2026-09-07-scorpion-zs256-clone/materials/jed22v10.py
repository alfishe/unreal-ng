import re,sys
def parse(fn):
    t=open(fn,'rb').read().decode('latin1')
    qf=int(re.search(r'QF(\d+)',t).group(1))
    f0=re.search(r'F(\d)\*',t); default=int(f0.group(1)) if f0 else 0
    fuses=[default]*qf
    for m in re.finditer(r'L(\d+)\s+([01\s]+)\*',t):
        a=int(m.group(1)); bits=re.sub(r'\s','',m.group(2))
        for i,b in enumerate(bits): fuses[a+i]=int(b)
    names={}
    for m in re.finditer(r'NOTE (?:PINS|NODES) ([^*]+)\*',t):
        for p in m.group(1).split():
            n,num=p.split(':'); names[int(num)]=n
    return fuses,names
# GAL22V10: 44 columns, column pair k -> source
colsrc=[1,2,23,3,22,4,21,5,20,6,19,7,18,8,17,9,16,10,15,11,14,13]
olmc=[23,22,21,20,19,18,17,16,15,14]
npt=[8,10,12,14,16,16,14,12,10,8]
def term(row,fuses,names):
    lits=[]
    for k,src in enumerate(colsrc):
        t=fuses[row*44+2*k]; c=fuses[row*44+2*k+1]
        n=names.get(src,f'pin{src}')
        if t==0 and c==0: return None  # always false
        if t==0: lits.append(n)
        elif c==0: lits.append('!'+n)
    return ' & '.join(lits) if lits else '1'
fuses,names=parse(sys.argv[1])
print('AR =',term(0,fuses,names))
row=1
for i,p in enumerate(olmc):
    n=names.get(p,f'pin{p}')
    s1=fuses[5808+2*i]; s0=fuses[5808+2*i+1]
    oe=term(row,fuses,names); row+=1
    pts=[]
    for j in range(npt[i]):
        tt=term(row,fuses,names); row+=1
        if tt is not None: pts.append(tt)
    mode='registered' if s1==0 else 'combinational'
    pol='active-high' if s0==1 else 'active-low (inverted)'
    if not pts and (oe is None or oe=='1'):
        # unused output, likely input
        pass
    print(f'\n-- pin {p} ({n}): S1={s1} S0={s0} -> {mode}, {pol}; OE = {oe}')
    if pts:
        op = ':=' if s1==0 else '='
        lhs = n if s0==1 else '!'+n
        print(f'{lhs} {op}\n    ' + '\n  + '.join(pts))
print('\nSP =',term(131,fuses,names))
