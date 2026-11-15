10 gfx 0
20 cls
30 let x=10000
40 let y=0
50 let c=1
100 plot 160+x/256,100+y/256,4
250 let u=x
260 let v=y
270 let x=-(u+v/3)
280 let y=-(v-u/5)
290 line 160+x/256,100+y/256,c
291 let c=c+1
292 if c>15 let c=1
300 goto 200
