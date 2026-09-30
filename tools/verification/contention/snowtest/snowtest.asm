; snowtest - ULA snow, expected and live side by side (docs/inprogress/2026-09-29-ula-snow)
;
; Draws the same band of 32 characters twice. The top band (EXPECTED) is drawn with the characters the ULA
; snow model predicts: some cells show another column's character (snow), some repeat their left neighbour
; (double). The bottom band (LIVE) holds the plain characters, and a loop locked to the frame runs while the
; picture is drawn, with I pointing into slow memory (#40) during the band's 32 lines: on a 48K, 128K or +2
; the ULA's snow changes the same cells as in the EXPECTED band. The +2A / +3 and the clones do not snow: there
; the LIVE band stays plain. SPACE stops the test.
;
; How the prediction works: each screen line of the band runs 32 x LD A,0 (7 T each, 224 T: one line of the
; 48K; on a 228 T line the last one of each line becomes OUT (#FF),A, 11 T). So R advances by exactly 32 per
; line, and a cell that snows shows the same column (R bits 4..0) on every line. The band covers a whole group
; of four character rows with the same characters, so R bits 6..5 (which pick the row) do not matter either.
;
; Loads and runs at 36000. Standalone: CLEAR 35999, load the code, RANDOMIZE USR 36000. The expected band and
; the text below it are filled in by the generator (core/tests/emulator/video/snowtest_test.cpp) from the model;
; in the plain source the expected band is plain and the text is empty.

        org 36000

START:
        jp MAIN
HOSTENTRY:
        call MAIN
HostSpin:
        jr HostSpin

; ---- settings and outputs ----

R0:       db 0            ; R set before the band (the generator's prediction uses it)
ONEPASS:  db 0            ; 1: the band loop returns after one frame (to measure it)
DONE:     db 0            ; 1 when stopped
FRAMELEN: dw 0            ; the frame length measured, minus 32768 (FRAMET: a frame does not fit 16 bits)
LINELEN:  db 0            ; 224 or 228: the band's line length
BODYLEN:  dw 0            ; one pass of the band loop up to its return, as CODETIME measured it
DELAYLEN: dw 0            ; the DELAY that makes the loop last exactly one frame

; T-states (INT-relative) where the loop starts: 96 T before line 128's first fetch, minus the setup before the
; chain (DI 4, LD A,(nn) 13, LD R,A 9, LD A,n 7, LD I,A 9). The first fetch of line y: 14336 + 224 y on the 48K
; (the floating bus reads the first bitmap byte there), 14362 + 228 y on the 128K
SETUP     equ 4 + 13 + 9 + 7 + 9
ENTRY48   equ 14336 + 224 * 128 - 96 - SETUP
ENTRY128  equ 14362 + 228 * 128 - 96 - SETUP

; ---- main ----

MAIN:
        call INSTINT
        call FRAMETIME
        ld hl,(FRAMET)
        ld (FRAMELEN),hl

        call FillChain
        call IsFrame128         ; (uses HL)
        ld hl,ENTRY48 + 1       ; CODETIME counts one more (as ctprobe)
        ld a,224
        jr nz,Have48
        call Patch128
        ld hl,ENTRY128 + 1
        ld a,228
Have48:
        ld (EntryT),hl
        ld (LINELEN),a

        call Draw

        ld a,1                  ; measure one pass of the loop
        ld (ONEPASS),a
        ld hl,(EntryT)
        ld de,BODY
        call CODETIME
        ld (BODYLEN),hl
        ld hl,(FRAMELEN)        ; DELAY = frame - measured - 21 (see BODY), with frame = FRAMELEN + 32768
        ld de,(BODYLEN)
        or a
        sbc hl,de
        ld de,32768 - 21
        add hl,de
        ld (DELAYLEN),hl

        xor a                   ; the loop, until SPACE
        ld (ONEPASS),a
        ld hl,(EntryT)
        ld de,BODY
        call CODETIME

        ld a,#3F                ; the ROM's I, interrupt mode 1
        ld i,a
        im 1
        ei
        ld a,15
        call AtRow
        ld hl,TxStopped
        call PrintStr
        ld a,1
        ld (DONE),a
        ld bc,0
        ret

EntryT: dw 0

; Z if the frame is 70908 T (128K / +2 timing)
IsFrame128:
        ld hl,(FRAMELEN)
        ld de,70908 - 32768
        or a
        sbc hl,de
        ret

; The chain: 32 lines of 32 x LD A,0
FillChain:
        ld hl,CHAIN
        ld bc,32 * 32
FillChainLoop:
        ld (hl),#3E
        inc hl
        ld (hl),0
        inc hl
        dec bc
        ld a,b
        or c
        jr nz,FillChainLoop
        ret

; On a 228 T line the last LD A,0 of each line becomes OUT (#FF),A (11 T, one M1 like LD A,0): 228 T, 32 M1s.
; Port #00FF: odd, high byte 0; no device of the 48K / 128K / +2 answers it
Patch128:
        ld hl,CHAIN + 31 * 2
        ld de,64
        ld b,32
Patch128Loop:
        ld (hl),#D3
        inc hl
        ld (hl),#FF
        dec hl
        add hl,de
        djnz Patch128Loop
        ret

; ---- the band loop ----
;
; Entered by CODETIME at an exact T-state. Loop mode: one pass lasts exactly one frame (DELAYLEN), so the chain
; meets the same ULA ticks every frame. T_pre = time from BODY to the JR NZ; one-pass mode returns through
; JR NZ (12), EI (4), RET (10), and CODETIME reports T_pre + 12 + 4 + 10 - 10 = T_pre + 16. Loop mode:
; T_pre + JR NZ (7) + LD BC,(nn) (20) + DELAY (BC, the CALL included) + JP (10) = frame, so
; BC = frame - T_pre - 37 = frame - measured - 21.
BODY:
        di
        ld a,(R0)
        ld r,a                  ; the next M1's refresh puts R0 on the bus (I still #BE: no snow)
        ld a,#40
        ld i,a                  ; slow memory: the band snows
CHAIN:
        ds 32 * 32 * 2          ; 32 lines of 32 x LD A,0 (FillChain, Patch128)
        ld a,#BE                ; the engine's IM2 table (this M1 falls in the border: no snow)
        ld i,a
        ld a,#7F
        in a,(#FE)
        rra
        jr nc,BodyExit          ; SPACE
        ld a,(ONEPASS)
        or a
        jr nz,BodyOnce
        ld bc,(DELAYLEN)
        call DELAY
        jp BODY
BodyOnce:
        ei
        ret
BodyExit:
        ei
        ret

; ---- the screen ----

Draw:
        call #0D6B              ; CLS (leaves channel K open)
        ld a,2
        call #1601              ; CHAN-OPEN: the upper screen
        ld a,#FF
        ld (#5C8C),a            ; SCR_CT: no "scroll?"
        ld hl,TxHead
        call PrintStr
        ld hl,EXPCHARS          ; EXPECTED: rows 8-11 (the top of the middle third)
        ld a,8
        call DrawBand
        ld a,14
        call AtRow
        ld hl,TxLive
        call PrintStr
        ld hl,PLAINCHARS        ; LIVE: rows 16-19 (the top of the bottom third)
        ld a,16
        call DrawBand
        ld a,20                 ; the prediction in words, rows 20-21
        call AtRow
        ld hl,EXPTEXT
        jp PrintStr

; A = first of four character rows; HL = 32 characters
DrawBand:
        ld c,4
DrawRows:
        push bc
        push af
        push hl
        call DrawRow
        pop hl
        pop af
        pop bc
        inc a
        dec c
        jr nz,DrawRows
        ret

; one character row A from the 32 characters at HL: each cell the character's ROM glyph and the attribute of
; the column the character stands for ('0'..'9', 'A'..'V' -> column 0..31)
DrawRow:
        ld e,a                  ; E = row
        ld d,0                  ; D = column
DrawCell:
        push hl
        push de
        ld a,(hl)
        push af                 ; the character
        call CharColumn         ; A = the column it stands for
        call ColumnAttr         ; A = that column's attribute
        pop bc                  ; B = the character
        push af
        ld a,b
        call DrawGlyph
        call CellAttr
        pop af
        ld (hl),a
        pop de
        pop hl
        inc hl
        inc d
        ld a,d
        cp 32
        jr nz,DrawCell
        ret

; A = character -> A = its column ('0'..'9' -> 0..9, 'A'..'V' -> 10..31)
CharColumn:
        sub '0'
        cp 10
        ret c
        sub 'A' - '0' - 10
        ret

; A = column -> A = attribute: bright, black ink, paper cycling blue..white by column
ColumnAttr:
        cp 7
        jr c,ColumnAttrGot
        sub 7
        jr ColumnAttr
ColumnAttrGot:
        inc a
        rlca
        rlca
        rlca
        or #40
        ret

; the ROM font glyph of character A into cell (row E, column D); keeps DE
DrawGlyph:
        push de
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        ld bc,#3C00             ; the 48 BASIC ROM font: #3D00 is character 32
        add hl,bc
        push hl
        ld a,e                  ; the cell's first scan line: #40 | third, row << 5 | column
        and #18
        or #40
        ld h,a
        ld a,e
        and 7
        rrca
        rrca
        rrca
        or d
        ld l,a
        pop de
        ld b,8
DrawGlyphLine:
        ld a,(de)
        ld (hl),a
        inc de
        inc h
        djnz DrawGlyphLine
        pop de
        ret

; attribute address of (row E, column D) -> HL
CellAttr:
        ld a,e
        rrca
        rrca
        rrca
        ld l,a
        and #03
        or #58
        ld h,a
        ld a,l
        and #E0
        or d
        ld l,a
        ret

; PRINT AT A,0
AtRow:
        push af
        ld a,22
        rst #10
        pop af
        rst #10
        xor a
        rst #10
        ret

; zero-terminated string at HL
PrintStr:
        ld a,(hl)
        or a
        ret z
        push hl
        rst #10
        pop hl
        inc hl
        jr PrintStr

TxHead:    db "snowtest 1 - ULA snow", 13
           db "48K, 128K, +2: LIVE must look", 13
           db "like EXPECTED. +2A, +3, clones:", 13
           db "LIVE stays plain. SPACE: stop", 13, 13
           db "EXPECTED", 0
TxLive:    db "LIVE", 0
TxStopped: db "stopped", 0

PLAINCHARS: db "0123456789ABCDEFGHIJKLMNOPQRSTUV"
EXPCHARS:   db "0123456789ABCDEFGHIJKLMNOPQRSTUV"  ; the generator writes the prediction here
EXPTEXT:    ds 64                                   ; the generator writes the prediction in words here
            db 0
PROBEEND:
