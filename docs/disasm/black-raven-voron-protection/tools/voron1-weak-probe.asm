; voron1-weak-probe.asm - live WD1793 probe for scratch/voron1-weak.udi
;
; Runs on the PENTAGON 128 machine config ONLY (per investigation rule) and proves,
; through the real Beta-128 port path (#1F/#3F/#5F/#7F/#FF), that:
;   1. the UDIW weak mark (cyl 59 / side 1 / R=192 data field) is applied on image load,
;   2. the FlakySectorEmulator mutates weak data per revolution: two reads differ,
;   3. both weak reads end with CRC-error status (fuzzy data, no RNF - sector found),
;   4. the solid neighbour R=193 reads identical bytes twice with clean status,
;   5. the lying-C trick works: track register = 55 matches the ID that claims C=55.
;
; Port map (as implemented by this emulator):
;   #1F command / status   #3F track   #5F sector   #7F data   #FF beta128 system
;   #FF write: bits0-1 drive, bit2 reset (active low), bit4 side (INVERTED: 0 = side 1),
;              bit6 density (0 = MFM)
;   #FF read:  bit6 = DRQ, bit7 = INTRQ (completion)
;   #1F status (emulator layout): bit0 BUSY, bit1 DRQ, bit2 LOST, bit3 CRCERR, bit4 RNF
;
; Type I bits: 0 0 cmd3 h V r1 r0 -> RESTORE 0x0B / SEEK 0x1B (h=1, V=0, 30 ms rate)
; Type II bits: 1 0 m s E C 0     -> READ SECTOR 0x80 (single, no side compare)
;
; Deliverable: raw binary poked to 0x8100 (RAM bank 2, fixed window on 128K),
; PC set to 0x8100, executed via WebAPI run_tstates. Results block at 0x8000:

RESW1   EQU 0x8000      ; [0] status, [1..2] byte count (LE) - weak R=192 read 1
RESW2   EQU 0x8003      ; weak R=192 read 2
RESS1   EQU 0x8006      ; solid R=193 read 1
RESS2   EQU 0x8009      ; solid R=193 read 2
DIFFW   EQU 0x800C      ; u16 count of differing bytes BUF1 vs BUF2
DIFFS   EQU 0x800E      ; u16 count of differing bytes BUF3 vs BUF4
PHASE   EQU 0x8010      ; last completed phase (1..8) - diagnosis if DONE stays 0
DONE    EQU 0x8011      ; 0xA5 signature when the probe finished

BUF1    EQU 0x9000      ; R=192 read 1 (1024 bytes)
BUF2    EQU 0x9400      ; R=192 read 2
BUF3    EQU 0x9800      ; R=193 read 1
BUF4    EQU 0x9C00      ; R=193 read 2

    ORG 0x8100

START:
    DI                   ; the probe polls ports - no IM1 handler may steal cycles
    LD SP, 0xFF60       ; own stack (boot state's SP is wherever the ROM left it)
    LD A, 0x04           ; drive A, side 1 (bit4=0, inverted decode), reset released, MFM
    OUT (0xFF), A

    LD A, 1
    LD (PHASE), A
    LD A, 0x0B           ; RESTORE (recalibrate to track 0)
    OUT (0x1F), A
    CALL WAITIDLE

    LD A, 2
    LD (PHASE), A
    LD A, 59             ; data register = target cylinder
    OUT (0x7F), A
    LD A, 0x1B           ; SEEK to cylinder 59
    OUT (0x1F), A
    CALL WAITIDLE

    LD A, 3
    LD (PHASE), A
    LD A, 55             ; lying-C: R=192's ID claims C=55, and C must match track reg
    OUT (0x3F), A

    LD A, 192
    LD DE, BUF1
    LD IX, RESW1
    CALL READSEC

    LD A, 4
    LD (PHASE), A
    LD A, 192            ; second read of the same weak sector
    LD DE, BUF2
    LD IX, RESW2
    CALL READSEC

    LD A, 5
    LD (PHASE), A
    LD A, 59             ; R=193..196 carry the true cylinder in their IDs
    OUT (0x3F), A
    LD A, 193
    LD DE, BUF3
    LD IX, RESS1
    CALL READSEC

    LD A, 6
    LD (PHASE), A
    LD A, 193            ; second read of the solid control sector
    LD DE, BUF4
    LD IX, RESS2
    CALL READSEC

    LD A, 7
    LD (PHASE), A
    LD HL, BUF1
    LD DE, BUF2
    CALL CMP1024
    LD (DIFFW), IX       ; > 0 proves per-revolution mutation of the weak data

    LD A, 8
    LD (PHASE), A
    LD HL, BUF3
    LD DE, BUF4
    CALL CMP1024
    LD (DIFFS), IX       ; must be 0: solid sector is stable across reads

    LD A, 0xA5
    LD (DONE), A

HALTLOOP:
    JR HALTLOOP

; ---- A = sector number, DE = destination buffer, IX = result slot [status, count LE16]
READSEC:
    OUT (0x5F), A        ; sector register - real silicon latches any byte (no clamp)
    LD A, 0x80           ; READ SECTOR: single, no side compare, no settle delay
    OUT (0x1F), A
    LD BC, 0
RDLOOP:
    IN A, (0xFF)         ; beta128 status: DRQ bit6, INTRQ bit7
    BIT 6, A             ; data byte waiting?
    JR NZ, RDGET
    BIT 7, A             ; command complete?
    JR Z, RDLOOP
    IN A, (0x1F)         ; final status (bit3 CRCERR, bit4 RNF in this layout)
    LD (IX+0), A
    LD (IX+1), C
    LD (IX+2), B
    RET
RDGET:
    IN A, (0x7F)         ; fetch the byte from the data register
    LD (DE), A
    INC DE
    INC BC
    JR RDLOOP

; ---- wait for a Type I command to complete (INTRQ), then clear it via #1F read
WAITIDLE:
    IN A, (0xFF)
    BIT 7, A
    JR Z, WAITIDLE
    IN A, (0x1F)
    RET

; ---- HL = buffer A, DE = buffer B, 1024 bytes; IX = number of differing bytes
CMP1024:
    LD BC, 1024
    LD IX, 0
CMPLOOP:
    LD A, (DE)
    INC DE
    CPI                  ; compare A with (HL), HL++, BC--
    JR Z, CMPNEXT
    INC IX
CMPNEXT:
    LD A, B
    OR C
    JR NZ, CMPLOOP
    RET
