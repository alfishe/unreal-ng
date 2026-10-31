; AnalysisL3 (review): a mode byte the loop compares with CP 5 and nothing writes, so the branch to `rare` is never
; taken in the session; an experiment (or the coverage driver) that holds mode at 5 reaches the rare code, which
; marks `seen`. `ticks` counts loop passes with INC (HL) and wraps (a candidate counter-wrap bug).
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld hl,ticks
        inc (hl)
        ld a,(mode)
        cp 5
        jr z,rare
        jr main
rare:
        ld a,$AA
        ld (seen),a
        ld hl,rarecount
        inc (hl)
        jr main
mode:      db 0
seen:      db 0
ticks:     db 0
rarecount: db 0
