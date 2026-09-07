; ==============================================================================
; "Scroller by Demarche" (1996) — Covox Menu & Runtime Engine
; Address: $8000 - $BFFF (Bank 2)
; Components: Menu Entry ($9B6B), Keyboard Poll, STARTDEMO ($9CD6),
;             Engine Entry ($8000), IM2 Setup ($BF02), IM2 Handler ($BFBF)
; ==============================================================================

                ORG     $8000

; ==============================================================================
; 1. Main Demo Engine Entry ($8000)
; Jumped to by $9D48 after user exits the Covox menu
; ==============================================================================
DEMO_ENTRY:
8000: CD 93 85          CALL    INIT1           ; Initialize main scroller visual engine
8003: FB                EI                      ; Enable interrupts for frame sync
8004: 76                HALT                    ; Wait for next vertical blank
8005: 3E 01             LD      A, 1
8007: 32 C1 9F          LD      ($9FC1), A      ; Set engine running flag
...
; Paging loops: Switches pages 3, 5, 7 into Bank 3 for sample streaming and double buffering
8022: 01 FD 7F          LD      BC, $7FFD
8025: 3E 1D             LD      A, $1D          ; Shadow screen (Page 7) + Page 5 bank
8027: ED 79             OUT     (C), A
...
804E: 3E 17             LD      A, $17          ; Page 7 bank
8050: ED 79             OUT     (C), A

; ==============================================================================
; 2. Covox Menu Entry Point ($9B6B)
; Jumped to by $6203 (USR 25088) after decrunching completes
; ==============================================================================
MENU_ENTRY:
9B6B: FB                EI                      ; Sync with ULA frame
9B6C: 76                HALT
9B6D: AF                XOR     A               ; A = 0
9B6E: D3 FE             OUT     ($FE), A        ; Set border to black
9B70: 21 00 58          LD      HL, $5800       ; Attribute area start
9B73: 11 01 58          LD      DE, $5801
9B76: 01 FF 02          LD      BC, 767         ; 32x24 attribute cells - 1
9B79: 36 07             LD      (HL), $07       ; Black paper, white ink
9B7B: ED B0             LDIR                    ; Clear attributes

; Print Covox Setup Menu Banner ("COVOX ON PORT #FB", etc.)
9B7D: CD xx xx          CALL    PRINT_MENU_TEXT

; ------------------------------------------------------------------------------
; Menu Input Loop: Polls Space Key to Start Demo
; ------------------------------------------------------------------------------
MENU_KEY_LOOP:
9B80: 01 FE 7F          LD      BC, $7FFE       ; Half-row: Space, Symbol, M, N, B
9B83: ED 78             IN      A, (C)          ; Read keyboard port
9B85: E6 01             AND     $01             ; Bit 0 = Space key
9B87: 28 08             JR      Z, STARTDEMO    ; If Space pressed (low), launch demo!

; Check alternate keys (Sound test, Covox port select, AY toggle)
9B89: ...
9B90: 18 EE             JR      MENU_KEY_LOOP

; ==============================================================================
; 3. STARTDEMO Routine ($9CD6)
; Invoked when Space key is pressed in the menu
; ==============================================================================
STARTDEMO:
9CD6: FB                EI
9CD7: 76                HALT
9CD8: FB                EI
9CD9: 76                HALT
9CDA: 3E 00             LD      A, $00
9CDC: D3 FE             OUT     ($FE), A        ; Ensure black border

; Screen Attribute Fade-Out Loop:
9CDE: 21 00 58          LD      HL, $5800
9CE1: 01 00 03          LD      BC, 768
FADE_LOOP:
9CE4: CB BE             RES     6, (HL)         ; Clear bright bit
9CE6: 7E                LD      A, (HL)
9CE7: E6 07             AND     $07             ; Ink color
9CE9: 28 01             JR      Z, $+3
9CEB: 35                DEC     (HL)            ; Fade ink towards black
9CEC: 23                INC     HL
9CED: 0B                DEC     BC
9CEE: 78                LD      A, B
9CEF: B1                OR      C
9CF0: 20 F2             JR      NZ, FADE_LOOP

; Clear Screen Bitmap:
9CF2: 21 00 40          LD      HL, $4000
9CF5: 11 01 40          LD      DE, $4001
9CF8: 01 FF 17          LD      BC, 6143
9CFB: 36 00             LD      (HL), 0
9CFD: ED B0             LDIR

; Initialize Sound Engine Pointers & Jump to Engine ($8000):
9D48: C3 00 80          JP      DEMO_ENTRY      ; Jump to $8000!

; ==============================================================================
; 4. Interrupt Mode 2 (IM2) Initialization ($BF02)
; ==============================================================================
IM2INI:
BF02: F3                DI                      ; Disable interrupts while rebuilding vector table
BF03: 3E BE             LD      A, $BE          ; Interrupt vector table at $BE00 - $BF01
BF05: ED 47             LD      I, A            ; Load Z80 Interrupt register I = $BE
BF07: ED 5E             IM      2               ; Set CPU Interrupt Mode 2

; Build 257-byte Vector Table: All vectors point to address $BFBF
BF09: 21 00 BE          LD      HL, $BE00       ; Table base
BF0C: 11 01 BE          LD      DE, $BE01
BF0F: 01 01 01          LD      BC, 257         ; 257 bytes covers all possible bus bytes (0..255 + 1)
BF12: 36 BF             LD      (HL), $BF       ; High byte of handler = $BF, Low byte = $BF -> Target = $BFBF
BF14: ED B0             LDIR

BF16: FB                EI                      ; Enable interrupts
BF17: C9                RET

; ==============================================================================
; 5. IM2 50Hz Interrupt Service Routine ($BFBF)
; Handles Covox audio frame streaming, pattern updates, and scroll synchronization
; ==============================================================================
IM2_HANDLER:
BFBF: F5                PUSH    AF
BFC0: C5                PUSH    BC
BFC1: D5                PUSH    DE
BFC2: E5                PUSH    HL
BFC3: 08                EX      AF, AF'
BFC4: D9                EXX
BFC5: F5                PUSH    AF
BFC6: C5                PUSH    BC
BFC7: D5                PUSH    DE
BFC8: E5                PUSH    HL

; Stream digital audio to Covox port #FB:
BFD0: 01 FD 7F          LD      BC, $7FFD
BFD3: 3E 10             LD      A, $10          ; Page 0 (Sample block 1)
BFD5: ED 79             OUT     (C), A

; Read next sample and output to Covox DAC:
BFD7: 2A xx xx          LD      HL, (SAMPLE_PTR)
BFDA: 7E                LD      A, (HL)
BFDB: D3 FB             OUT     ($FB), A        ; Covox 8-bit DAC output!

; Switch to secondary sample bank (Page 7):
BFDD: 3E 17             LD      A, $17
BFDF: ED 79             OUT     (C), A
BFE1: ...

; Restore CPU state:
BFF0: E1                POP     HL
BFF1: D1                POP     DE
BFF2: C1                POP     BC
BFF3: F1                POP     AF
BFF4: D9                EXX
BFF5: 08                EX      AF, AF'
BFF6: E1                POP     HL
BFF7: D1                POP     DE
BFF8: C1                POP     BC
BFF9: F1                POP     AF
BFFA: FB                EI                      ; Re-enable interrupts
BFFB: C9                RET                     ; Return from interrupt
