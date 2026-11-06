; SkoolDraftIsub / CommentAddressesToRefs: LD HL,nn used as an address (a label in the draft), LD BC,nn with the
; same value used as a count (stays a number: @keep), and LD (nn),A into an operand (@isub label+1).
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld hl,buffer
        ld a,(hl)
        inc a
        ld (hl),a
        ld bc,buffer
        ld a,c
        add a,b
        ld (target+1),a
target:
        ld e,0
        jr main
        ds 32
buffer: db 0
