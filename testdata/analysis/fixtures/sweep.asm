; AnalysisL1 (passes over the image): `sweep` reads every byte of $A000-$AFFF and writes it back unchanged (JSW's
; start-up pause); `decrypt` XORs $B000-$BFFF, changing every byte. Only `main` uses `table`; the rest of $A000-$AFFF
; is never used, whatever the sweep did with it.
        org $8000
start:
        ld sp,$9F00
        di
        ld hl,$A000
sweep:
        ld c,(hl)
        ld a,l
        xor c
        xor h
        ld (hl),c
        inc hl
        ld a,h
        cp $B0
        jr nz,sweep
        ld hl,$B000
decrypt:
        ld a,(hl)
        xor $55
        ld (hl),a
        inc hl
        ld a,h
        cp $C0
        jr nz,decrypt
main:
        ld hl,table
        ld b,16
readtab:
        ld a,(hl)
        inc hl
        djnz readtab
        jr main

        org $A000
table:     db 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16
