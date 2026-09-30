# Decode an Intel 85C220 / Altera EP220 JED (QF2916) into sum-of-products.
# Layout: 8 macrocells x 9 rows (8 PT rows + OE row), MC1 = pin 19 ... MC8 = pin 12.
# 18 input pairs (complement, true): pins 1..9, 11, then feedbacks of pins 12..19. Fuse '0' = connected.
import re, sys
names=['CLK7','IORQ_','WR_EN','RAM_','INT','TRB_IN','BORDER_','M1_','H0','H1',
       'H1M','Pin13','WR_BUFF','RAS_','TRB','WE','CLK_CPU','WAIT_']
pins=[19,18,17,16,15,14,13,12]
s=open(sys.argv[1],encoding='latin1').read()
bits=re.sub(r'\s','',re.search(r'L0\s*(.*?)\*',s,re.S).group(1))
rows=[bits[i:i+36] for i in range(0,len(bits),36)]
def term(r):
    if r=='0'*36: return None
    lits=[]
    for n in range(18):
        c,t=r[2*n],r[2*n+1]
        if c=='0' and t=='0': return 'FALSE'
        if c=='0': lits.append('!'+names[n])
        if t=='0': lits.append(names[n])
    return ' & '.join(lits) if lits else 'TRUE'
for mc in range(8):
    blk=rows[mc*9:mc*9+9]
    pts=[p for p in (term(r) for r in blk[:8]) if p]
    print(f'pin{pins[mc]}  OE={term(blk[8])}\n    ' + '\n  # '.join(pts))
