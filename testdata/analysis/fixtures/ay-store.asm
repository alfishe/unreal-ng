; AnalysisDetectorMusic, a register store helper: tick reads 14 tune bytes and stores each through store (A = register,
; E = value) into regs; frame calls out, which sends regs to the AY. Neither out, frame nor store reads anything
; nothing wrote: tick, the caller of store, is the player, and its tune is music.
        org $8000
start:
        ld sp,$9F00
        di
main:
        call tick
        call frame
        jr main
frame:
        call out
        ret
tick:
        ld hl,(pos)
        xor a
fill:
        ld e,(hl)
        push hl
        call store
        pop hl
        inc hl
        inc a
        cp 14
        jr nz,fill
        ld hl,(pos)
        inc hl
        inc hl
        ld de,tune+32
        or a
        sbc hl,de
        add hl,de
        jr c,keep
        ld hl,tune
keep:
        ld (pos),hl
        ret
store:
        push af
        ld hl,regs
        add a,l
        ld l,a
        jr nc,stored
        inc h
stored:
        ld (hl),e
        pop af
        ret
out:
        ld hl,regs
        xor a
send:
        ld bc,$FFFD
        out (c),a
        ld b,$BF
        ld e,(hl)
        out (c),e
        inc hl
        inc a
        cp 14
        jr nz,send
        ret
pos:    dw tune
regs:   ds 14
tune:   db $10,$01,$20,$01,$30,$01,$08,$38,$0F,$0E,$0D,$40,$02,$50,$02,$60
        db $11,$02,$21,$02,$31,$02,$09,$39,$0E,$0D,$0C,$41,$03,$51,$03,$61
        db $12,$03,$22,$03,$32,$03,$0A,$3A,$0D,$0C,$0B,$42,$04,$52,$04,$62
