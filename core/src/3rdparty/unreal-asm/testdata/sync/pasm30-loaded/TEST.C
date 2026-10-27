	ORG #6000
START   LD A,1
        LD B,2
LOOP    DJNZ LOOP
        RET
