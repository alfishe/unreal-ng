; CallGraphTailJump: main calls sub_a and sub_b; sub_a ends in a JP to sub_b (a tail call), so sub_b runs from two
; places and returns to main for both.
        org $8000
start:
        ld sp,$9F00
        di
main:
        call sub_a
        call sub_b
        jr main
sub_a:
        ld a,1
        jp sub_b
filler:
        ds 80          ; sub_b in a run of its own
sub_b:
        inc a
        ret
