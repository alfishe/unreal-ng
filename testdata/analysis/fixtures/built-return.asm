; A return address built from two byte immediates: LD (HL),target/256 and LD (HL),target%256 into the stack slots,
; then RET (Jet Set Willy's game start at $8400). The RET is a dispatch to target, and the draft writes both
; immediates as halves of target's label (the relocation moves them with it).
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld sp,$9EFE
        ld hl,$9EFF
        ld (hl),target/256
        dec hl
        ld (hl),target%256
        ret
count:  db 0
target:
        ld a,(count)
        inc a
        ld (count),a
        jr main
