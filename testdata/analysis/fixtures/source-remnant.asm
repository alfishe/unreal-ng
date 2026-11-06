; AnalysisDetectorText: leftover tokenized assembler source nobody reads ([line lo, hi][length][text], rising line
; numbers, starting inside a line) is Unused with the decoded lines; the same bytes read by the code (negative) are not.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld hl,read_copy
        ld b,82
        xor a
loop:
        add a,(hl)
        inc hl
        djnz loop
        jr main
remnant:
        db 9,"DEC",9,"E"
        dw 3960
        db 11,9,9,"LD",9,"A,(HL)"
        dw 3970
        db 12,9,"CP",9,"$FF",9,";END"
        dw 3980
        db 11,9,9,"JR",9,"Z,DONE"
        dw 3990
        db 7,9,"INC",9,"HL"
        dw 4000
        db 9,9,9,"JR",9,"LOOP"
        dw 4010
        db 8,"DONE",9,"RET"
read_copy:
        db 9,"DEC",9,"E"
        dw 3960
        db 11,9,9,"LD",9,"A,(HL)"
        dw 3970
        db 12,9,"CP",9,"$FF",9,";END"
        dw 3980
        db 11,9,9,"JR",9,"Z,DONE"
        dw 3990
        db 7,9,"INC",9,"HL"
        dw 4000
        db 9,9,9,"JR",9,"LOOP"
        dw 4010
        db 8,"DONE",9,"RET"
