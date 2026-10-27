; AnalysisDetectorMusic, a register write helper: aywrite only selects the register (A) and writes the value (E); the
; player reads its tune (register, value pairs) and calls it. The helper reads nothing, its caller's tune is the music.
        org $8000
start:
        ld sp,$9F00
        di
main:
        call play
        jr main
play:
        ld hl,(pos)
        ld a,(hl)
        inc hl
        ld e,(hl)
        inc hl
        call aywrite
        ld de,tune+32
        or a
        sbc hl,de
        add hl,de
        jr c,keep
        ld hl,tune
keep:
        ld (pos),hl
        ret
aywrite:
        ld bc,$FFFD
        out (c),a
        ld b,$BF
        out (c),e
        ret
pos:    dw tune
tune:   db 0,$10,1,$01,2,$20,3,$01,4,$30,5,$01,6,$08,7,$38
        db 8,$0F,9,$0E,10,$0D,0,$40,1,$02,2,$50,3,$02,4,$60
