; FinishUnused: a copy of a ROM routine is code that did not run, not a leftover. loader holds the 48K ROM's LD-BYTES
; start ($0556-$0586, 49 bytes: its relative jumps land on its own instructions, its calls on the ROM), as a program
; leaves it to patch and call later; nothing reads, writes or runs it in the session. Positive: the copy is inferred
; Code ("ROM copy, code", evidence "rom-copy" naming $0556) with its instructions in the code map, not Unused. The
; loop reads a ROM byte (L1 maps the ROM only when the session touched it).
        org $8000
start:
        di
main:
        ld hl,$4000
        ld a,($0556)            ; the ROM: L1 maps it
        ld (hl),a
        jr main
        db $AA
loader:                         ; the ROM's $0556-$0586
        db $14,$08,$15,$F3,$3E,$0F,$D3,$FE,$21,$3F,$05,$E5,$DB,$FE,$1F,$E6
        db $20,$F6,$02,$4F,$BF,$C0,$CD,$E7,$05,$30,$FA,$21,$15,$04,$10,$FE
        db $2B,$7C,$B5,$20,$F9,$CD,$E3,$05,$30,$EB,$06,$9C,$CD,$E3,$05,$30
        db $E4
loader_end:
        db $AA
