; sjasmplus rules a conversion keeps (relocfake.bin: the bytes sjasmplus 1.21 builds from #8000): a HIGH relocation
; table (word: its high byte; a byte of a moving word: none; NEXTREG r,A; PUSH nn big-endian; EQU of a high byte),
; RELOCATE_TABLE +offset, an EQU as the parent of the .labels after it (its own .t: the previous parent),
; D24 / TEXT fields, fake and undocumented instructions
        OPT --zxnext
        ORG #8000
        RELOCATE_START HIGH
start   ld hl,start
hi      EQU high start
        ld b,high start
        ld b,start
        db high start
        nextreg 7,a
        nextreg 7,high start
        push start
        ld c,hi
        RELOCATE_END
        RELOCATE_TABLE
        RELOCATE_TABLE +1
        dw relocate_count,relocate_size
p       nop
.t      nop
q       EQU .t
.u      nop
b       EQU 5
.c      nop
        dw q,q.u,b.c,p.t
        STRUCT st
w       D24 #123456
t       TEXT 4,{"ab",1}
        ENDS
s       st {,{"z"}}
        ld bc,(ix+2)
        ld (iy),de
        ldi a,(hl)
        ldd (hl),a
        sub de,bc
        adc de,hl
        rlc (ix+3),b
        set 5,(iy-1),e
        ld hl,ix
        ld ix,iy
