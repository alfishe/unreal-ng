; paging_test.asm - Paging lock detection for Scroller loader
; Assemble to $5CD0 (printer buffer, safe in BASIC)
; Returns: A=0 paging works, A=1 paging locked
;
; Usage in BASIC:
;   IF USR 23760 THEN PRINT "PAGING LOCKED!": STOP

        ORG $5CD0       ; 23760 - printer buffer

start:
        ; Save original 4 bytes from $C000
        LD HL, $C000
        LD DE, saved
        LDI
        LDI
        LDI
        LDI

        ; Write signature $DE $AD $BE $EF
        LD HL, $C000
        LD (HL), $DE
        INC HL
        LD (HL), $AD
        INC HL
        LD (HL), $BE
        INC HL
        LD (HL), $EF

        ; Try switching to page 1
        LD BC, $7FFD
        LD A, 1
        OUT (C), A

        ; Check if signature still visible (= locked)
        LD HL, $C000
        LD A, (HL)
        CP $DE
        JR NZ, paging_ok
        INC HL
        LD A, (HL)
        CP $AD
        JR NZ, paging_ok
        INC HL
        LD A, (HL)
        CP $BE
        JR NZ, paging_ok
        INC HL
        LD A, (HL)
        CP $EF
        JR NZ, paging_ok

        ; Signature still there = PAGING LOCKED
        ; Try restore page 0 (won't work but try)
        LD BC, $7FFD
        XOR A
        OUT (C), A
        ; Restore original bytes
        LD HL, saved
        LD DE, $C000
        LDI
        LDI
        LDI
        LDI
        LD A, 1         ; Return 1 = locked
        RET

paging_ok:
        ; Paging works - restore page 0
        LD BC, $7FFD
        XOR A
        OUT (C), A
        ; Restore original bytes
        LD HL, saved
        LD DE, $C000
        LDI
        LDI
        LDI
        LDI
        XOR A           ; Return 0 = OK
        RET

saved:  DS 4            ; Storage for original 4 bytes

        END start
