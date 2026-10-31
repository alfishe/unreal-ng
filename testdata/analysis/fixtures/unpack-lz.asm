; AnalysisDetectorUnpackers: an LZ unpacker (literal runs, back references copied with LDIR from the output
; already written) unpacks an 80-byte program from 26 packed bytes to $A000 once, then the program is called on
; every pass; and the same packed bytes only copied to $B000 and read there (negative: no packed claim).
; Packed format: a control byte n: 0 ends; 1..127: n literal bytes follow; 128 + n: copy n bytes from `distance`
; (the next byte) back in the output.
        org $8000
start:
        ld sp,$9F00
        di
        ld hl,packed
        ld de,$A000
        call unpack
        ld hl,packed
        ld de,$B000
        ld bc,packed_end-packed
        ldir
main:
        call $A000
        ld a,($B000)
        ld (seen),a
        jr main
unpack:
        ld a,(hl)
        inc hl
        or a
        ret z
        jp m,backref
        ld c,a
        ld b,0
        ldir
        jr unpack
backref:
        and $7F
        ld c,a
        ld a,(hl)
        inc hl
        push hl
        ld l,a
        ld a,e
        sub l
        ld l,a
        ld a,d
        sbc a,0
        ld h,a
        ld b,0
        ldir
        pop hl
        jr unpack
seen:   db 0
packed:
        db $12,$21,$00,$58,$06,$20,$3A,$40,$A0,$77,$23,$10,$FC,$3C,$32,$40,$A0,$C9,$00,$AE,$01,$01,$07,$8F,$10,$00
packed_end:
