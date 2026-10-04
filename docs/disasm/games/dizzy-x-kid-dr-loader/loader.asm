; DIZZY X (KID__DR release) - annotated loader disassembly
; Fixture: testdata/loaders/tap/DIZZY_X_KID__DR.tap
; Source of the listing: memory dumps taken in unreal-ng 48K (execution breakpoints, TTD
; and the WebAPI memory/register reads) and cross-checked against a MAME `spectrum` run.
; Addresses are the real run-time addresses.
;
; Notation: "; NNNN" at the end of a line is the address of that instruction.
; Everything marked VERIFIED was seen executing in MAME and in unreal-ng 48K.
; Everything marked INFERRED is a reading of the bytes, not an observed run.
; Everything marked NOT TRACED was not examined.

; ======================================================================
; STAGE 1 - trampoline and the ROM identity checks     (#9802 - #9898)
; ======================================================================
; Entered from the BASIC line of tape block 1 (line 0, hidden USR number) at #989E:
;
;   989E  di
;   989F  ld   sp,#5BFF
;   98A2  call #9802         ; the stage 1 below; its "return" address #98A5 is never used
;   98A5  jp   #9055         ; (dead for the loader's own purposes)
;
; The CALL operand (#9802, the two bytes at #98A3/#98A4) doubles as data: the code
; below POPs the return address, steps back two bytes, reads that word and adds #4D.
; VERIFIED: that is #9802 + #4D = #984F, the start of the encrypted area.

        di                      ; 9802  no interrupt may disturb the R-keyed code below
        ld   a,#41              ; 9803  border = 1 (blue) + MIC bit pattern, a visible
        out  (#FE),a            ; 9805  "stage 1 is running" sign
        ld   b,a                ; 9807  B = #41, later part of the key
        ld   de,#004D           ; 9808
        pop  hl                 ; 980B  HL = return address of USR (inside the BASIC line)
        dec  hl                 ; 980C
        ld   a,(hl)             ; 980D  A = byte before the return address
        dec  hl                 ; 980E
        ld   l,(hl)             ; 980F  L = the byte before that
        ld   h,a                ; 9810  HL = 16-bit value stored just before the return address
        ld   a,b                ; 9811
        add  hl,de              ; 9812  + #4D = #984F: where the encrypted area really starts
        push hl                 ; 9813
        pop  ix                 ; 9814  IX = start of the area to decrypt
        ld   r,a                ; 9816  R := #41 - the key stream starts from a known R
        xor  a                  ; 9818
        ld   hl,#4000           ; 9819  clear the whole screen bitmap: the loader hides
        ld   de,#4001           ; 981C  what it is doing from the user
        ld   bc,#1CD0           ; 981F
        ld   (hl),l             ; 9822  L = 0
        ldir                    ; 9823  (also clears the attributes partly)
        ld   b,a                ; 9825  B = 0
        cpl                     ; 9826  A = #FF
        ld   c,a                ; 9827  C = #FF  -> BC = #00FF
        call #33C5              ; 9828  ROM CHECK #1: #33C5 is a lone RET in the 48K ROM.
                                ;       A modified ROM changes the stack / R count and with it
                                ;       the decryption key below.

; Decryption loop #1 (#982B-#984F). The key is built from A=R, H, L, B, C, IXL, D, so a
; wrong register state anywhere above turns the code into garbage.
; The DD prefixes in front of ordinary opcodes do nothing except add one to R per prefix:
; they are there to make the R counter - the key - hard to reproduce by hand.
l982b:  ld   a,r                ; 982B
        xor  h                  ; 982D
        xor  l                  ; 982E
        xor  b                  ; 982F
        xor  c                  ; 9830
        xor  ixl                ; 9831  (DD AD)
        xor  h                  ; 9833
        db   #DD : xor c        ; 9834  DD A9  prefix has no effect, R += 1
        db   #DD : xor b        ; 9836  DD A8
        db   #DD : nop          ; 9838  DD 00
        xor  d                  ; 983A
        db   #DD : ld d,a       ; 983B  DD 57
        ld   a,(ix+0)           ; 983D  DD 7E 00  encrypted byte
        xor  d                  ; 9840  DD AA
        ld   (ix+0),a           ; 9842  DD 77 00  plain byte written back
        inc  ix                 ; 9845
        or   a                  ; 9847
        push ix                 ; 9848
        pop  hl                 ; 984A
        sbc  hl,bc              ; 984B  stop when IX reaches BC
        add  hl,bc              ; 984D
        jr   nz,l982b           ; 984E  VERIFIED: IX starts at #984F, BC = #00FF, so the loop
                                ;       decrypts #984F-#FFFF and wraps to #0000-#00FF: it covers
                                ;       all the rest of RAM (writes into ROM are ignored)

; Decryption loop #2: the BASIC program body, keyed purely by R
        ld   a,#48              ; 9850
        ld   r,a                ; 9852  R := #48
        ld   hl,#5CD5           ; 9854  start of the BASIC line body (PROG + 10)
        ld   de,#9857           ; 9857  end: this very loop
l985a:  ld   a,r                ; 985A  "ld a,r" is two M1 cycles (ED 5F), the rest of the loop adds ten ...
        ld   b,a                ; 985C
        ld   a,(hl)             ; 985D
        xor  b                  ; 985E  key = R at this moment
        ld   (hl),a             ; 985F
        inc  hl                 ; 9860
        or   a                  ; 9861
        sbc  hl,de              ; 9862
        add  hl,de              ; 9864
        jr   nz,l985a           ; 9865  ... every iteration costs 12 M1 cycles (R += 12, mod 128)
                                ;       Z80 detail: R bit 7 is never changed by increments, so
                                ;       the key repeats every 128/gcd(12,128)=32 iterations.

; ----------------------------------------------------------------------
; ROM CHECK #2 - the one that stops Pentagon (VERIFIED)
; ----------------------------------------------------------------------
        ld   hl,#0000           ; 9867
        push hl                 ; 986A  a RET to #0000 (reset) is the failure exit
        ld   a,(#006D)          ; 986B  byte at ROM #006D ...
        cp   #20                ; 986E  ... must be #20
                                ;       In the Sinclair 48K ROM #0066 is the NMI handler:
                                ;         0066 PUSH AF / PUSH HL / LD HL,(#5CB0) / LD A,H /
                                ;         OR L / JR NZ,#0070 ; JP (HL) ...
                                ;       #006D is the opcode of that JR NZ = #20.
                                ;       Patched ROMs (the "48K for 128K" ROM that unreal-ng's
                                ;       Pentagon ROM set uses) carry #28 (JR Z) there.
        jr   nz,lwipe           ; 9870  not the original ROM -> wipe memory
        pop  hl                 ; 9872  drop the reset address again
        ld   hl,#4000           ; 9873  clear the screen once more
        ld   de,#4001           ; 9876
        ld   bc,#1AFF           ; 9879
        ld   (hl),l             ; 987C
        ldir                    ; 987D
        ld   hl,#5E61           ; 987F  address of stage 2 (the tape loader, below)
        push hl                 ; 9882
        xor  a                  ; 9883  border black
        out  (#FE),a            ; 9884
        ret                     ; 9886  "return" into #5E61

lwipe:  di                      ; 9887  copy protection reaction: fill 15000 bytes of RAM
        ld   hl,#4000           ; 9888  (the whole screen and the area after it) with #15
        ld   de,#4001           ; 988B
        ld   bc,#3A98           ; 988E
        ld   (hl),#15           ; 9891
        ldir                    ; 9893  this also erases stage 2 at #5DAE: the CPU then runs
        ret                     ; 9895  through #15 bytes ("dec d" is #15) and derails

; ======================================================================
; STAGE 2 - the custom tape loader                     (#5D00 - #5E60)
; ======================================================================
; All blocks after block 2 are loaded by this code, not by the ROM.

; ---- #5CFD-#5D80: load the data blocks, decrypt them, start the game (retry target
; #5CFD is just before the first line shown here) ----------------------------
l5d00:  ld   de,#1B00           ; 5D00  block length = screen
        call load               ; 5D03  load one block to (HL)
        jr   nc,l5d00           ; 5D06  carry clear = tape error: retry from the top
        call dec1               ; 5D08  decrypt the screen (#0D80 bytes, key by R)
        ld   hl,#C000           ; 5D0B  copy it to the display
        ld   de,#4000           ; 5D0E
        ld   bc,#1B00           ; 5D11
        ldir                    ; 5D14
        ld   hl,#5F57           ; 5D16
        push hl                 ; 5D19  the next stage will "return" to #5F57
        ld   de,#A100           ; 5D1A  length of the next block
        call load               ; 5D1D
        jr   nc,#5D16       ; 5D20  (retry, same idea)
        call dec3               ; 5D22  decrypt #5F57.. (#5055 words, stride 2)
        ld   a,(#5D80)          ; 5D25  #5D80 holds #FF at load time, the Y/N answer later
        cp   #00                ; 5D28  (set to 0 by the Y/N prompt at #5EB8 = cheat on)
        jr   nz,l5d30           ; 5D2A
        xor  a                  ; 5D2C
        ld   (#8B9E),a          ; 5D2D  patch the game: infinite lives
l5d30:  ld   a,#64              ; 5D30
        ld   (#FFFF),a          ; 5D32  write a test value into the very last RAM cell ...
        ld   a,#11              ; 5D35  (the A values #11, #17, #10 passed to #6009 here and at
        call #6009              ; 5D37  #5D51 / #5D64 are not used: VERIFIED #6009 is a lone RET,
                                ;       the end of the routine that starts at #5FD2)
        ld   a,(#FFFF)          ; 5D3A  ... and read it back
        cp   #64                ; 5D3D
        jr   z,l5d76            ; 5D3F  equal: RAM is there -> #5D76.
                                ;       INFERRED, not observed failing: where nothing answers at
                                ;       #FFFF (a 16K Spectrum, whose top RAM is missing) the
                                ;       read-back differs and the code falls into the alternative
                                ;       load path below. The test value cannot be disturbed by the
                                ;       CALL: SP is about #5BFB here (observed), nowhere near #FFFF.
        ; #5D41-#5D75 load two more blocks with the same routine (#C000 <- decrypt, #4000 <-
        ; screen), then fill the attributes with #47 and RET into #5F57. NOT TRACED in detail.

l5d76:  ld   a,#C9              ; 5D76  write #C9 (the RET opcode) into two places of the routine
        ld   (#5FFD),a          ; 5D78  at #5FD2 (VERIFIED bytes before the patch):
        ld   (#5FE3),a          ; 5D7B    #5FFD = first byte of `inc iy` (FD 23) -> becomes RET: the
                                ;         routine ends after its first cell instead of looping;
                                ;         #5FE3 = low byte of the operand in `ld iy,#5FA0`
                                ;         (FD 21 A0 5F) -> IY = #5FC9.
                                ;       Effect: that routine (it fills a display buffer at #E678
                                ;       from the table at #5FA0 and the font-like tables at
                                ;       #604D...) is cut short on this path. Its on-screen purpose
                                ;       was not identified.
        jr   #5D62          ; 5D7E
        db   #FF                ; 5D80  Y/N cheat flag

; ---- decrypt helpers (#5D81-#5DAD) -----------------------------------
dec1:   ld   hl,#C000 : ld bc,#0D80 : jr dec          ; 5D81
dec2:   ld   hl,#C000 : ld bc,#2000 : jr dec          ; 5D89
dec3:   ld   hl,#5F57 : ld bc,#5055                    ; 5D91
dec:    xor  a                  ; 5D97
        di                      ; 5D98
        ld   r,a                ; 5D99  R := 0
        ld   a,a                ; 5D9B  (padding, 1 M1 more)
        ld   a,r                ; 5D9C
        ld   e,a                ; 5D9E  E = R
        ld   a,(hl)             ; 5D9F
        xor  e                  ; 5DA0
        ld   d,a                ; 5DA1  D = byte ^ R1
        ld   a,r                ; 5DA2
        xor  d                  ; 5DA4  ^ R2
        ld   (hl),a             ; 5DA5  two different R samples in one byte
        dec  bc                 ; 5DA6
        inc  hl                 ; 5DA7
        inc  hl                 ; 5DA8  every second byte only (stride 2)
        ld   a,b                ; 5DA9
        or   c                  ; 5DAA
        jr   nz,dec+2           ; 5DAB
        ret                     ; 5DAD

; ---- #5DAE: the tape block loader (a re-write of ROM LD-BYTES) -------
; In:  HL = destination, DE = length, A=#FF (data block flag)
; Out: carry set = loaded OK
load:   push hl                 ; 5DAE
        pop  ix                 ; 5DAF  IX = destination
        ld   a,#FF              ; 5DB1  expect a data block
        scf                     ; 5DB3
        inc  d                  ; 5DB4  \ same trick as ROM #0556: make a zero D reach
        ex   af,af'             ; 5DB5   | "flag = A', carry = load/verify" bookkeeping
        dec  d                  ; 5DB6  /
        di                      ; 5DB7
        ld   a,#0F              ; 5DB8  border white, MIC on
        out  (#FE),a            ; 5DBA
        in   a,(#FE)            ; 5DBC  read EAR once to get its idle level
        rra                     ; 5DBE
        and  #20                ; 5DBF  bit 5 = the EAR level
        or   #02                ; 5DC1  red border
        ld   c,a                ; 5DC3  C = current border/EAR phase
        cp   a                  ; 5DC4  Z := 1
        ret  nz                 ; 5DC5  (never taken)
        call edge               ; 5DC6  wait for an edge (ROM #05E7 LD-EDGE-1)
        jr   nc,#5DC5         ; 5DC9  BREAK or timeout: retry
        ld   hl,#0415           ; 5DCB  pilot length counter (H:L) ...
l5dce:  djnz l5dce              ; 5DCE
        dec  hl                 ; 5DD0
        ld   a,h                ; 5DD1
        or   l                  ; 5DD2
        jr   nz,l5dce           ; 5DD3
        call edge2              ; 5DD5  two edges per pulse
        jr   nc,#5DC5         ; 5DD8
        ld   b,#9C              ; 5DDA  pilot pulse reference
        call edge2              ; 5DDC
        jr   nc,#5DC5         ; 5DDF
        ld   a,#C6              ; 5DE1  too long a pulse -> not a pilot
        cp   b                  ; 5DE3
        jr   nc,#5DC6         ; 5DE4
        inc  h                  ; 5DE6  count pilot pulses
        jr   nz,#5DDA              ; 5DE7
        ld   b,#C9              ; 5DE9  sync pulse, first half
        call edge               ; 5DEB
        jr   nc,#5DC5         ; 5DEE
        ld   a,b                ; 5DF0
        cp   #D4                ; 5DF1  sync must be shorter than this
        jr   nc,#5DE9              ; 5DF3
        call edge               ; 5DF5
        ret  nc                 ; 5DF8
        ld   a,c                ; 5DF9
        xor  #03                ; 5DFA  flip the border colors for the data phase
        ld   c,a                ; 5DFC
        ld   h,#00              ; 5DFD  H = running XOR (parity)
        ld   b,#B0              ; 5DFF
        jr   lbyte              ; 5E01
        ; ... (bit loop #5E03-#5E3C: same structure as ROM LD-8-BITS, one byte per 8 pulse
        ;      pairs; "ld (ix+0),l" stores, "xor l" accumulates the parity, the final
        ;      "cp 1" checks it) ...
        ret                     ; 5E3C  carry = parity OK
edge2:  call edge               ; 5E3D
        ret  nc                 ; 5E40
edge:   ld   a,#16              ; 5E41  LD-EDGE-1: count how long the EAR level stays put
        dec  a                  ; 5E43
        jr   nz,$-1             ; 5E44
        and  a                  ; 5E46
        inc  b                  ; 5E47  B counts the time; wrap = timeout
        ret  z                  ; 5E48
        ld   a,#7F              ; 5E49  also read the SPACE half-row
        in   a,(#FE)            ; 5E4B
        rra                     ; 5E4D  carry = SPACE (BREAK)
        nop                     ; 5E4E
        xor  c                  ; 5E4F
        and  #20                ; 5E50  EAR changed?
        jr   z,#5E47          ; 5E52
        ld   a,c                ; 5E54  yes: invert our copy and flash the border
        cpl                     ; 5E55
        ld   c,a                ; 5E56
        ld   a,r                ; 5E57  *** the border color comes from R: the stripes on
        and  #07                ; 5E59  screen are not a ROM palette, they are R counting M1
        or   #08                ; 5E5B  cycles.
        out  (#FE),a            ; 5E5D
        scf                     ; 5E5F
        ret                     ; 5E60

; ======================================================================
; STAGE 3 - game start (#5E61)                                  VERIFIED
; ======================================================================
        di                      ; 5E61
        call #7022              ; 5E62  relocation fix-ups. VERIFIED: the routine starts with
                                ;       `di / call #0052 / dec sp / dec sp / pop bc` - #0052 is a RET
                                ;       in the 48K ROM (byte #C9), so the CALL only pushes its own
                                ;       return address, and the two DEC SP + POP BC read it back:
                                ;       BC = the address of the next instruction. This is the
                                ;       classic "where am I" trick; it then adds fixed offsets
                                ;       (#97, #66, #7B, #89, #BE) to BC and stores the results into
                                ;       itself. It needs byte #C9 at ROM #0052: a FOURTH implicit
                                ;       ROM dependence, besides #33C5, #006D and the #3D30 path.
        ; clear #E678..#EA77, relocate the main program #78D8 -> #C000 (#1C28 bytes) ...
        ; build an IM 2 vector table at #FE00 (257 x #FD), JP at #FDFD to the handler at #5EC0
        ; set I=#FE, IM 2, EI
        ; ask "infinite lives? Y/N" via keyboard rows #DF (bit 4 = Y) and #7F (bit 3 = N)
        ;   Y -> #5D80 := 0 (cheat on, tested at #5D25: patches #8B9E), JP #5CD5
        ;   N -> JP #5CD5 with #5D80 left at #FF
        ; (VERIFIED in unreal-ng 48K: after pressing Y, #5D80 = 0 and the game start #5CD5 runs)

; ---- ROM CHECK #3: every interrupt re-verifies the ROM (VERIFIED) ---
im2:    ex   af,af'             ; 5EC0  the handler runs 50 times per second
        exx                     ; 5EC1
        push ix                 ; 5EC2
        call #C00E              ; 5EC4  game music/IRQ work
        call #61CE              ; 5EC7
        ld   a,(#006D)          ; 5ECA  the same test as at #986B
        cp   #20                ; 5ECD
        ld   hl,#0000           ; 5ECF
        push hl                 ; 5ED2  failure: "return" to #0000 ...
        jp   nz,#3D30           ; 5ED3  ... through ROM #3D30. VERIFIED: #3D00-#3FFF of the 48K
                                ;       ROM is the character set, not code. #3D30 holds the glyph
                                ;       bytes 00 10 28 10 2A 44 3A 00 (the "&" bitmap): the CPU
                                ;       executes a letter shape as instructions (NOP, DJNZ ...) and
                                ;       runs wild. The pushed #0000 is the "return to reset" if the
                                ;       garbage ever executes a RET.
        pop  hl                 ; 5ED6  success: take the pushed zero away
        pop  ix                 ; 5ED7
        exx                     ; 5ED9
        ex   af,af'             ; 5EDA
        ei                      ; 5EDB
        ret                     ; 5EDC
