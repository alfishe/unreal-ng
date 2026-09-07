; ==============================================================================
; "Scroller by Demarche" (1996) — SCROLL00.C Disassembly
; Address: $6200 - $62B1 (178 bytes)
; Functions: Demo Start Vector, Depack Dispatcher, Depack Table, MegaLZ Decruncher
; ==============================================================================

                ORG     $6200

; ------------------------------------------------------------------------------
; Entry Point 1: 25088 ($6200) — Start Demo
; Called by Line 90: RANDOMIZE USR VAL "25088"
; ------------------------------------------------------------------------------
START_DEMO:
6200: 31 00 62          LD      SP, $6200       ; Set stack pointer below code/buffers
6203: C3 6B 9B          JP      $9B6B           ; Jump to Covox menu / demo entry point

; ------------------------------------------------------------------------------
; Entry Point 2: 25094 ($6206) — Depack Dispatcher
; Called by Lines 40, 50, 60, 70, 80, 90: RANDOMIZE USR VAL "25094"
; ------------------------------------------------------------------------------
DEPACK_DISPATCH:
6206: F3                DI                      ; Disable interrupts during bank switch & depack
6207: FD E5             PUSH    IY              ; Preserve BASIC's IY register ($5C3A)
6209: 21 26 62          LD      HL, DEPACK_TBL  ; Self-modifying table pointer (initially $6226)
620C: 7E                LD      A, (HL)         ; Read Port #7FFD configuration byte
620D: 23                INC     HL
620E: 01 FD 7F          LD      BC, $7FFD       ; Port #7FFD address
6211: ED 79             OUT     (C), A          ; Map destination RAM page into Bank 3 ($C000-$FFFF)
6213: 5E                LD      E, (HL)         ; Destination address Low
6214: 23                INC     HL
6215: 56                LD      D, (HL)         ; Destination address High
6216: 23                INC     HL
6217: 4E                LD      C, (HL)         ; Source address Low
6218: 23                INC     HL
6219: 46                LD      B, (HL)         ; Source address High
621A: 23                INC     HL
621B: 22 0A 62          LD      (DEPACK_DISPATCH+4), HL ; Advance self-modifying pointer at $620A
621E: 60                LD      H, B            ; HL = Source address
621F: 69                LD      L, C            ; DE = Destination address
6220: CD 44 62          CALL    MEGALZ_DEPACK   ; Decompress data using MegaLZ
6223: FD E1             POP     IY              ; Restore BASIC IY
6225: C9                RET                     ; Return to BASIC loader

; ------------------------------------------------------------------------------
; Depack Descriptor Table ($6226 - $6243)
; Format: [1 byte: #7FFD Port Val] [2 bytes: Dest Addr] [2 bytes: Src Addr]
; ------------------------------------------------------------------------------
DEPACK_TBL:
; Entry 1 (Line 40): SCROLL15 -> Page 5 @ $62B2 (appends to loader)
6226: 15                DB      $15             ; Page 5 ($C000), 48K ROM
6227: B2 62             DW      $62B2           ; Destination = $62B2
6229: 00 80             DW      $8000           ; Source = $8000

; Entry 2 (Line 50): SCROLL10 -> Page 0 @ $C000 (Sample Block 1)
622B: 10                DB      $10             ; Page 0 ($C000), 48K ROM
622C: 00 C0             DW      $C000           ; Destination = $C000
622E: 00 80             DW      $8000           ; Source = $8000

; Entry 3 (Line 60): SCROLL11 -> Page 1 @ $C000 (Sample Block 2)
6230: 11                DB      $11             ; Page 1 ($C000), 48K ROM
6231: 00 C0             DW      $C000           ; Destination = $C000
6233: 00 80             DW      $8000           ; Source = $8000

; Entry 4 (Line 70): SCROLL13 -> Page 3 @ $C000 (Sample Block 3)
6235: 13                DB      $13             ; Page 3 ($C000), 48K ROM
6236: 00 C0             DW      $C000           ; Destination = $C000
6238: 00 80             DW      $8000           ; Source = $8000

; Entry 5 (Line 80): SCROLL17 -> Page 7 @ $DB00 (Sample Block 4)
623A: 17                DB      $17             ; Page 7 ($C000), 48K ROM
623B: 00 DB             DW      $DB00           ; Destination = $DB00
623D: 00 80             DW      $8000           ; Source = $8000

; Entry 6 (Line 90): SCROLL12 -> Page 2 @ $8000 (Main Demo Code & Menu)
; FATAL FAILURE POINT if Page 4 was clobbered by 128K SWAP hook!
623F: 14                DB      $14             ; Page 4 ($C000), 48K ROM
6240: 00 80             DW      $8000           ; Destination = $8000 (Bank 2)
6242: 00 C0             DW      $C000           ; Source = $C000 (Bank 3, Page 4)

; ------------------------------------------------------------------------------
; MegaLZ Decompressor Routine ($6244 - $62B1)
; HL = Source (packed data stream), DE = Destination (unpacked buffer)
; ------------------------------------------------------------------------------
MEGALZ_DEPACK:
6244: 3E 80             LD      A, $80          ; Initialize bit reservoir with sentinel bit
6246: 08                EX      AF, AF'
6247: ED A0             LDI                     ; First byte is always uncompressed literal: (DE++) = (HL++)
6249: 01 FF 02          LD      BC, $02FF       ; B = 2 (displacement base), C = $FF

MLZ_LOOP:
624C: 08                EX      AF, AF'

MLZ_GET_BIT:
624D: 87                ADD     A, A            ; Shift out next bit from reservoir into Carry
624E: 20 03             JR      NZ, MLZ_GOT_BIT ; Reservoir still has bits
6250: 7E                LD      A, (HL)         ; Refill reservoir from input byte
6251: 23                INC     HL
6252: 17                RLA                     ; Bit into Carry, sentinel 1 into bit 0
MLZ_GOT_BIT:
6253: CB 11             RL      C               ; Accumulate bit into C
6255: 30 F6             JR      NC, MLZ_GET_BIT ; Loop until 8 bits gathered

6257: 08                EX      AF, AF'
6258: 10 0F             DJNZ    MLZ_MATCH       ; If B != 0, it is an LZ match reference

; Literal Copy:
625A: 3E 02             LD      A, 2
625C: CB 29             SRA      C
625E: 38 18             JR      C, MLZ_LIT_DONE
6260: 3C                INC     A
6261: 0C                INC     C
6262: 28 0F             JR      Z, MLZ_SPECIAL
6264: 01 3F 03          LD      BC, $033F
6267: 18 E3             JR      MLZ_LOOP

MLZ_MATCH:
6269: 10 25             DJNZ    MLZ_LONG_MATCH
626B: CB 39             SRL     C
626D: 38 D8             JR      C, MLZ_LOOP
626F: 04                INC     B
6270: 18 DA             JR      MLZ_MATCH

MLZ_LIT_DONE:
6272: 81                ADD     A, C
6273: 01 FF 04          LD      BC, $04FF
6276: 18 D4             JR      MLZ_LOOP

MLZ_SPECIAL:
6278: 0C                INC     C
6279: 20 28             JR      NZ, MLZ_COPY
627B: 08                EX      AF, AF'
627C: 04                INC     B
627D: CB 19             RR      C
627F: D8                RET     C               ; End of compressed stream!

6280: CB 10             RL      B
6282: 87                ADD     A, A
6283: 20 03             JR      NZ, $+5
6285: 7E                LD      A, (HL)
6286: 23                INC     HL
6287: 17                RLA
6288: 30 F3             JR      NC, $627D
628A: 08                EX      AF, AF'
628B: 80                ADD     A, B
628C: 06 06             LD      B, 6
628E: 18 BC             JR      MLZ_MATCH

6290: 10 04             DJNZ    $+6
6292: 3E 01             LD      A, 1
6294: 18 0F             JR      $+17
6296: 10 08             DJNZ    $+10
6298: 0C                INC     C
6299: 20 08             JR      NZ, $+10
629B: 01 1F 05          LD      BC, $051F
629E: 18 AC             JR      MLZ_LOOP

62A0: 10 D0             DJNZ    MLZ_MATCH
62A2: 41                LD      B, C
62A3: 4E                LD      C, (HL)
62A4: 23                INC     HL
62A5: 05                DEC     B

MLZ_COPY:
62A6: E5                PUSH    HL              ; Save compressed stream pointer
62A7: 69                LD      L, C            ; Calculate backward displacement
62A8: 60                LD      H, B
62A9: 19                ADD     HL, DE          ; HL = DE - displacement
62AA: 4F                LD      C, A            ; BC = match length
62AB: 06 00             LD      B, 0
62AD: ED B0             LDIR                    ; Copy decompressed bytes from history
62AF: E1                POP     HL              ; Restore compressed stream pointer
62B0: 18 97             JR      MLZ_LOOP        ; Continue decompression
