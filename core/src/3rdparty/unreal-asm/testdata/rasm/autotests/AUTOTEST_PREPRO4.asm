glop=0
 nop ; glop=1
 assert glop==0
 nop
 glop=0
 nop // glop=1
 assert glop==0
 nop
 glop=0 : /* glop=1 */ nop
assert glop==0
 nop
 
 glop=0 : /* glop=1 // */ nop
 assert glop==0
 nop
 glop=0 : /* glop=1 ; */ nop
assert glop==0
 nop
 glop=0 ; /* glop=1
 nop // glop=2 /*
 assert glop==0
 nop
