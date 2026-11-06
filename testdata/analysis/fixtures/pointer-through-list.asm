; AnalysisDataFlow_PointerThroughAList: a sprite table of four words is read by an index (LD L,(slot) / LD H,table/256
; / LD A,(HL) / INC L / LD H,(HL) / LD L,A), the word goes through the stack into a draw list (LD (HL),E / LD (HL),D)
; and a second loop reads it back from the list and draws through it: the table's words are pointers, not only the
; list's (positive). A word the table holds that only a byte copy moves, never used as an address, is no pointer
; (negative: the plain bytes at `bytes`).
        org $8000
start:
        di
        ld sp,$8F00
main:
        ld a,(slot)
        add a,2
        and 7
        ld (slot),a
        ld l,a
        ld h,table/256
        ld a,(hl)
        inc l
        ld h,(hl)
        ld l,a
        push hl
        ld hl,list
        pop de
        ld (hl),e
        inc l
        ld (hl),d
        ld a,(bytes)            ; a plain byte copied into the list's spare slot, never an address
        ld (list+2),a
        ld hl,list              ; the drawing loop: the list's word is the address
        ld e,(hl)
        inc l
        ld d,(hl)
        ld a,(de)
        ld (sink),a
        jr main
slot:   db 0
sink:   db 0
bytes:  db $34,$12
        org $9000
table:  dw block0,block1,block2,block3
        org $9100
list:   ds 4
        org $9200
block0: db 1,2,3,4
block1: db 5,6,7,8
block2: db 9,10,11,12
block3: db 13,14,15,16
