; PatchIsNotOverlay: an operand patched and run again is self-modified code; code that ran once and is then written
; over by a buffer fill (LD (HL),C, never run again) is an overlay, not a patch.
        org $8000
start:
        ld sp,$9F00
        di
        call once            ; runs once, then the buffer below is laid over it
main:
        ld a,(count)
        inc a
        ld (count),a
        ld (patched+1),a     ; patch the operand of the LD C,n below: it runs again every pass
patched:
        ld c,0
        ld hl,once           ; the buffer over the code that ran once
        ld b,once_end-once
fill:
        ld (hl),c
        inc hl
        djnz fill
        jr main
once:
        ld a,1
        ld (flag),a
        ret
once_end:
count:  db 0
flag:   db 0
