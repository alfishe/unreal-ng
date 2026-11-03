; DrawnBackBuffer: a 2 K back buffer at $9000 the code rewrites every pass and puts on the screen with plain loads
; and stores (no block copy), and a 2 K picture at $A000 put on the screen the same way but never written (a
; picture's source, no back buffer). The buffer's first bytes are code that runs once before the loop: an overlay, no
; conflict between the code and the buffer.
        org $8000
start:
        ld sp,$9F00
        di
        call init
main:
        ld a,(phase)
        inc a
        ld (phase),a
        ld hl,buffer            ; draw into the buffer: every byte, every pass
        ld bc,2048
fill:   ld (hl),a
        inc hl
        inc a
        dec bc
        ld e,a
        ld a,b
        or c
        ld a,e
        jr nz,fill
        ld hl,buffer            ; the buffer to the top third of the screen
        ld de,$4000
        call show
        ld hl,picture           ; the picture to the middle third
        ld de,$4800
        call show
        jr main
show:   ld bc,2048
show_loop:
        ld a,(hl)
        ld (de),a
        inc hl
        inc de
        dec bc
        ld a,b
        or c
        jr nz,show_loop
        ret
phase:  db 0
        org $9000
buffer:
init:   ld a,7                  ; runs once, then the buffer is drawn over it
        ld (phase),a
        ret
        ds 2048-6
        org $A000
picture:
        ds 2048,$5A
