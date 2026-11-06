; RecordTemplates negative: the code keeps one working copy of an object (9 bytes) and updates a byte of it every
; pass, then copies it (LDIR) into the next of 8 records of 16 bytes walked through IX. One source copied into more
; records than there are sources looks like a table of one template, but code other than the copies writes it: a
; working copy, no template (no record array of 9-byte records at it).
        org $8000
start:
        ld sp,$7F00
        di
main:
        ld a,(phase)
        inc a
        ld (phase),a
        ld (current+2),a        ; the working copy changes every pass
        and 7                   ; the record: phase mod 8
        add a,a
        add a,a
        add a,a
        add a,a
        ld e,a
        ld d,0
        ld hl,records
        add hl,de
        ex de,hl
        ld hl,current
        ld bc,9
        ldir
        ld ix,records           ; the walk
        ld b,8
walk:   ld a,(ix+0)
        add a,(ix+1)
        add a,(ix+2)
        add a,(ix+3)
        add a,(ix+4)
        add a,(ix+5)
        add a,(ix+6)
        add a,(ix+7)
        add a,(ix+8)
        ld (sink),a
        ld de,16
        add ix,de
        djnz walk
        jr main
phase:  db 0
sink:   db 0
        org $9000
current:
        db 1,2,3,4,5,6,7,8,9
after:  db $AA,$BB,$CC,$DD
        org $9100
records:
        ds 8*16
