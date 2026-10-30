; Text versus pixels through one blitter (Hungry Horace): `blit` draws 8 bytes from HL to the screen, for the
; characters of `message` (HL from the ROM character set) and for `sprite`, whose bytes happen to be printable.
; message is text; sprite is read by the glyph reader (the instruction that reads the font), so it is pixels.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld ix,message
        ld de,$4000
print:
        ld a,(ix+0)
        and $7F
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        ld bc,$3C00
        add hl,bc
        call blit
        bit 7,(ix+0)
        inc ix
        jr z,print
        ld hl,sprite
        ld de,$4010
        call blit
        jr main
blit:
        push de
        ld b,8
blit_loop:
        ld a,(hl)
        ld (de),a
        inc hl
        inc d
        djnz blit_loop
        pop de
        inc e
        ret
message: db "HELL",$CF
sprite:  db "<~~ZZ~~<"
