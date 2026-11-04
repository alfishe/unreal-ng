; FinishUnused: the stack and a ROM copy. The code sets a 16-byte stack of its own and draws the ROM's "!" glyph
; through a subroutine (CALL + PUSH: the four bytes under stack_top are touched only by the stack; reading the glyph
; maps the ROM in L1). font_copy holds the 48K ROM's letters A-H ($3E08-$3E47, 64 bytes), as BASIC's user-defined
; graphics leave them in RAM, and nothing reads or writes them. Positive: the stack bytes are Unused (evidence
; "stack"), the copy is Unused (evidence "rom-copy" naming $3E08); neither is an unknown area.
        org $8000
start:
        di
        ld sp,stack_top
main:
        ld hl,$3D08             ; the ROM's "!" glyph
        ld de,$4000
        call glyph
        jr main
glyph:
        push hl
        ld b,8
glyph_loop:
        ld a,(hl)
        ld (de),a
        inc hl
        inc d
        djnz glyph_loop
        pop hl
        ret
        db $AA
stack:
        ds 16
stack_top:
        db $AA
font_copy:                      ; the ROM's $3E08-$3E47: A B C D E F G H
        db $00,$3C,$42,$42,$7E,$42,$42,$00
        db $00,$7C,$42,$7C,$42,$42,$7C,$00
        db $00,$3C,$42,$40,$40,$42,$3C,$00
        db $00,$78,$44,$42,$42,$44,$78,$00
        db $00,$7E,$40,$7C,$40,$40,$7E,$00
        db $00,$7E,$40,$7C,$40,$40,$40,$00
        db $00,$3C,$42,$40,$4E,$42,$3C,$00
        db $00,$42,$42,$7E,$42,$42,$42,$00
font_copy_end:
        db $AA
