; AnalysisDetectorCodeMap: a clip pointer aimed at the ROM. Bytes of ROM 0 only written (the writes are lost) are a
; sink, no data (positive: $0000 and $0400, inside the reset and BEEP signatures); a ROM byte read and a RAM byte
; only written stay plain data (negative).
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld a,$AA
        ld ($0000),a
        ld ($0400),a
        ld a,($0010)
        ld (sink),a
        jr main
sink:   db 0
