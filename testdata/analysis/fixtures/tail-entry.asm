; AnalysisDetectorCodeMap tail entries: the target of a conditional JP whose code never comes back starts a routine
; of its own when the instruction before it is a tail jump to a routine and that JP is the only way in (`tail`). Code
; that returns (`done`) or jumps back into the code before it (`back`) stays inside its routine. The same closed code
; reached from two sites (`shared`) and the straight code that runs into a routine and is jumped to from two sites
; (`pre`) are entry candidates for the review. `n` stays 0: no conditional JP here is ever taken.
        org $8000
start:
        ld sp,$9F00
        di
main:
        call work
        call rejoin
        call twice
        call quit
        call pass
        jr main
work:
        ld a,(n)
        or a
        jp nz,tail
        jp m,pre
        ld (n+2),a
        jp other
tail:
        ld (n+1),a
        jp other
rejoin:
        ld a,(n)
rj:
        or a
        jp nz,back
        jp m,pre
        jp other
back:
        inc a
        jp rj
twice:
        ld a,(n)
        or a
        jp nz,shared
        jp m,shared
        jp other
shared:
        ld (n+1),a
        jp other
quit:
        ld a,(n)
        or a
        jp nz,done
        jp other
done:
        inc a
        ret
pass:
        ld a,(n)
        cp 7
pre:
        ld (n+2),a
        inc hl
other:
        ld a,(n+1)
        ret
n:      db 0,0,0
