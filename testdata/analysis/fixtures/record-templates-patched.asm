; RecordTemplates with a field of one template patched: the code writes +4 of the second template every pass (a level
; setting kept in the table). One source of three written by other code is still a table of templates, not a working
; copy.
        org $8000
start:
        ld sp,$7F00
        di
main:
        ld a,(phase)
        inc a
        ld (phase),a
        ld (templates+13),a     ; the patched field
        ld b,a                  ; the record: phase mod 8
        and 7
        add a,a
        add a,a
        add a,a
        add a,a
        ld e,a
        ld d,0
        ld hl,records
        add hl,de
        ex de,hl
        ld a,b                  ; the template: phase mod 3
mod3:   cp 3
        jr c,pick
        sub 3
        jr mod3
pick:   ld l,a
        add a,a
        add a,a
        add a,a
        add a,l                 ; 9 x template
        ld l,a
        ld h,0
        ld bc,templates
        add hl,bc
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
templates:
        db 1,2,3,4,5,6,7,8,9
        db 11,12,13,14,15,16,17,18,19
        db 21,22,23,24,25,26,27,28,29
after:  db $AA,$BB,$CC,$DD
        org $9100
records:
        ds 8*16
