; AnalysisDetectorText: a message drawn with the ROM character set ($3D00, the rom48.charset signature; bit 7 ends
; it), and the same characters only summed (negative: no text claim).
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld hl,message
        ld de,$4000
print:
        ld a,(hl)
        and $7F
        call glyph
        bit 7,(hl)
        inc hl
        jr z,print
        call checksum
        jr main
checksum:
        ld hl,plain
        ld b,5
        xor a
sum:
        add a,(hl)
        inc hl
        djnz sum
        ret
glyph:
        push hl
        push de
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        ld bc,$3C00
        add hl,bc
        ld b,8
glyph_loop:
        ld a,(hl)
        ld (de),a
        inc hl
        inc d
        djnz glyph_loop
        pop de
        inc e
        pop hl
        ret
glyph_copy:                     ; the first instructions of glyph again, never run (the signature negative)
        push hl
        push de
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        ret
message: db "HELL",$CF            ; "O" + $80
plain:   db "HELLO"
