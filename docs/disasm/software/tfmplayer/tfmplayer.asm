; =============================================================================
;  TFM Music Compiler 1.12 - runtime player ("_tsfmplaye" block)
; =============================================================================
;  1,883 bytes of Z80 that turn two bare YM2203 chips into a six-channel FM
;  orchestra.  This is the runtime half of the native ZX tracker TFM Music
;  Compiler 1.12, driving a TurboSound FM board: 2 x YM2203, twelve operators,
;  six melodic channels.  The authors never published the source; this
;  listing is a hand-written reconstruction, annotated for study, that
;  assembles bit-identical to the shipped binary.
;
;  Provenance and extraction . . . . . . . . . . . . . . .  disasm_tfmplayer.py
;  The tune data format it interprets . . . . .  docs/file-formats/music/
;  Wrapping player+tune into an instant-play .sna . . . . .  tsfm-sna-guide.md
;  (both live in this folder; see README.md for the full dossier)
;
;  HOW TO ASSEMBLE AND VERIFY (sjasmplus, github.com/z00m128/sjasmplus):
;
;      sjasmplus --raw=player.bin tfmplayer.asm
;      cmp player.bin player_org61A8.bin && echo BIT-IDENTICAL
;
;  POSITION-DEPENDENT CODE.  The block must be loaded at 0x61A8: it is full
;  of absolute references to its own bytes (self-modifying code slots), and
;  the trailing RELOC_TABLE stores absolute addresses.  The tune it plays
;  must sit at 0x8000 (MAIN_ENTRY hardcodes it).
;
; -----------------------------------------------------------------------------
;  MEMORY MAP
; -----------------------------------------------------------------------------
;    0x61A8  MAIN_ENTRY / PLAY_LOOP   halt loop, border heartbeat, key exit
;    0x61CC  trampolines             stable JP desks: INIT/FRAME/CHIP_RESET
;    0x61D5  INIT                    version check, SMC patch, relocation
;    0x622A  CHIP_RESET              -> REG_MOP: silence both chips
;    0x6238  REG_MOP                 register sweep that parks both YMs
;    0x628A  REG_WRITE               one polled (register,data) pair write
;    0x629D  SELECT_F8 / SELECT_F9   chip select - OUT_7FFD service entry
;    0x62B1  FRAME                   per-interrupt: six channels + divider
;    0x62DF  OUTI_PAIRS              bulk (register,data) stream writer
;    0x62F3  channel interpreters    six private copies, ~0x100 bytes each:
;                                     [helpers][stub][dispatch] per channel,
;                                     stamped out by the compiler back to back
;    0x68F7  RELOC_TABLE             six defw: pattern-pointer slot addresses
;
; -----------------------------------------------------------------------------
;  THE TURBOSOUND FM PORT PROTOCOL (as used by this code)
; -----------------------------------------------------------------------------
;  Every register access goes through a port pair built from BC/DE:
;
;      C = 0xFD   B = D = 0xFF -> port 0xFFFD   register select (write)
;                                port 0xFFFD   status       (read: bit7=busy)
;                  B = E = 0xBF -> port 0xBFFD   data         (write)
;
;  E = 0xBF also moonlights as a comparison constant (CP E tests opcode
;  against 0xBF), which is why that odd value is kept around in a register.
;  A "control word" written to 0xFFFD picks the active chip: 0xF8 = chip 0,
;  0xF9 = chip 1.  On genuine TSFM hardware 0xFFFD reads return a status
;  byte; after every write the chip is busy for ~32 cycles and bit7 is set.
;  Hence the IN F,(C) / JP M poll before every single OUT (F is used as the
;  destination so A survives).  On a legacy 2xAY TurboSound the same port
;  reads back the AY mixer (0xFF, bit7 set): the poll never exits and the
;  player parks forever at WAIT_DATA - silent and frozen.  That is why a
;  TSFM-capable machine config is a hard requirement.
;
;  PLAYING MODEL: one interrupt = one frame.  FRAME services all six channel
;  interpreters; each channel dispatches exactly ONE stream event per frame
;  (waits park the channel without consuming events).  The tune's streams
;  and opcodes are documented in docs/file-formats/music/tfmcom-module.md.
; =============================================================================

    org 0x61A8

; =============================================================================
;  MAIN ENTRY - where a snapshot points PC
; =============================================================================
;  Input: none that matters - the tune address is hardcoded.  Output: on key
;  exit, HL' = 0x2758 for the caller.  Interrupts must be masked on entry;
;  the player enables them itself right before the first HALT (a snapshot
;  therefore stores IFF1=0, see tsfm-sna-guide.md).
; =============================================================================
MAIN_ENTRY:
    ld hl,0x8000           ; tune base - fixed by design
    call TRAMP_INIT        ; validate version, relocate, silence chips
    ei                     ; one interrupt from here on = one frame

PLAY_LOOP:
    halt                   ; wait for the interrupt
    xor a
    out (0xfe),a           ; border black ...
    call TRAMP_FRAME       ; ... run one frame of all six channels ...
    ld a,7
    out (0xfe),a           ; ... border white: the classic heartbeat
    xor a                  ; any-key exit: sample the ULA port (active low)
    in a,(0xfe)
    and 0x1f
    cp 0x1f                ; all five lines high = nothing pressed
    jr z,PLAY_LOOP         ; -> next frame
    call TRAMP_STOP        ; key hit: mute and re-arm both chips
    exx
    ld hl,0x2758           ; exit token for the embedding program
    exx
    ret

; =============================================================================
;  Trampolines - stable call targets
; =============================================================================
;  Three JP desks so external callers (a loader, a BASIC stub, a menu) never
;  need to know the internal addresses.  All the real entry points below can
;  move between compiler builds; these desks are the contract.
; =============================================================================
TRAMP_INIT:
    jp INIT
TRAMP_FRAME:
    jp FRAME
TRAMP_STOP:
    jp CHIP_RESET

; =============================================================================
;  INIT - validate, patch, relocate, reset
; =============================================================================
;  1. Version check.  The tune's +9 byte is the format version: '2' means
;     TFMcom 1.12 (one event per interrupt, 50 Hz).  Anything else is an
;     older format that must be played at one frame in six.
;
;  2. The SMC patch.  FRAME_DIVIDER (inside FRAME) normally counts 6,5,..,1
;     and only lets the frame body run on the wrap.  For version '2' INIT
;     simply overwrites the LD A,6 opcode byte with RET (0xC9) - the whole
;     divider becomes a no-op and every interrupt plays.  Patching code to
;     data to code again: the entire versioning system is one byte.
;
;  3. Relocation.  The tune header (tune+10..tune+21) stores the six channel
;     stream offsets relative to the tune base.  INIT converts them to
;     absolute addresses and stores each into its channel stub's
;     LD HL,nnnn immediate.  The destination of the store instruction itself
;     is self-modified by an LDI pair fed from RELOC_TABLE - see below.
;
;  4. Channel reset.  Instrument counters (the OR nn slots) := 0, state
;     slots (the LD A,nn immediates) := 0xFF = "pattern not started".
;     INIT then falls through into CHIP_RESET so playback begins from
;     electrically silent, deterministic chips.
; =============================================================================
INIT:
    ex de,hl               ; DE = tune base (MAIN_ENTRY left it in HL)
    ld hl,9
    add hl,de              ; HL -> tune+9: the version character
    ld a,(hl)
    inc hl                 ; HL -> tune+10: first of the six stream offsets
    cp '2'                 ; '2' = TFMcom 1.12: full-rate playback
    ld a,0xc9              ; patch byte for the happy path: RET
    jr z,INIT_PATCH
    ld a,0x3e              ; old format: keep LD A,nn (the 1-in-6 divider)
INIT_PATCH:
    ld (FRAME_DIVIDER),a   ; SMC: rewrite FRAME's first divider byte

; -- relocate the six stream offsets (shadow register set drives the walk) --
    exx
    ld hl,RELOC_TABLE      ; six defw: each channel's LD HL,nnnn slot address
    ld bc,0x06fd           ; B = 6 channels; C = 0xFD = low byte of all slots
RELOC_COPY:
    ld de,0x61fd           ; DE -> operand bytes of the LD (nn),HL below
    ldi                    ; copy slot address low byte  \ into the operand
    ldi                    ; copy slot address high byte / of RELOC_STORE
    exx                    ; main set: HL walks the tune header offsets
    ld a,(hl)              ; stream offset, low byte
    inc hl
    push hl                ; remember where the high byte lives
    ld h,(hl)
    ld l,a                 ; HL = 16-bit offset from the tune header
    add hl,de              ; + tune base = absolute stream address
RELOC_STORE:
    ld (0),hl              ; SMC destination, patched by the LDI pair above
    pop hl
    inc hl                 ; -> next offset
    exx
    djnz RELOC_COPY        ; all six channels

; -- clear instrument counters, then flag every channel "not started" -------
    xor a                  ; 0 = no instrument running
    ld (CH1_COUNT),a
    ld (CH2_COUNT),a
    ld (CH3_COUNT),a
    ld (CH4_COUNT),a
    ld (CH5_COUNT),a
    ld (CH6_COUNT),a
    dec a                  ; 0xFF = state that survives the stub's INC A test
    ld (CH1_STATE),a
    ld (CH2_STATE),a
    ld (CH3_STATE),a
    ld (CH4_STATE),a
    ld (CH5_STATE),a
    ld (CH6_STATE),a
                           ; fall through: INIT also enters with the chips
                           ; silenced and rearmed

; =============================================================================
;  CHIP_RESET / REG_MOP - silence both chips
; =============================================================================
;  CHIP_RESET selects chip 0, runs the sweep, selects chip 1 - and then falls
;  straight into REG_MOP a second time, so the sweep's RET exits CHIP_RESET.
;  The keypress exit path comes here too: leave the board quiet.
;
;  REG_MOP walks the register file downward through REG_WRITE pairs
;  (A = register, A' = data):
;     0x0D..0x00 <- 0x00   AY half: periods, noise, envelope - all silent
;     0xB3..0x30 <- 0x00   the whole FM operator grid zeroed (0x4F is never
;                          written: on OPNA parts it is a control register;
;                          that write is redirected to 0x3F)
;     0x8F..0x80 <- 0x0F   release rate maximum: kill any sounding envelope
;     0x28 <- 0,1,2        key-off all three FM channels
;     0x27 <- 2            timer control: load timer B (belt and braces)
;     0x4F..0x40 <- 0x7F   total level maximum: full attenuation = silence
;     0x2F, 0x2D <- 0x7F   mode registers (the 0x2D write falls through
;                          into REG_WRITE - the sweep's final trick)
; =============================================================================
CHIP_RESET:
    ld de,0xffbf           ; D = 0xFF / E = 0xBF: the two port high bytes
    ld c,0xfd              ; C completes both port addresses
    call SELECT_F8         ; chip 0 active
    call REG_MOP           ; silence it
    call SELECT_F9         ; chip 1 active ...
                           ; ... and fall into REG_MOP again for chip 1
REG_MOP:
    xor a
    ex af,af'              ; A' = 0x00: data for the AY-half sweep
    ld a,0x0d
REG_MOP_AY:
    call REG_WRITE
    dec a
    jp p,REG_MOP_AY        ; regs 0x0D down to 0x00
    ld a,0xb3
REG_MOP_SKIP:
    cp 0x4f                ; never write 0x4F ...
    jr nz,REG_MOP_WRITE
    ld a,0x3f              ; ... redirect that one write to 0x3F
REG_MOP_WRITE:
    call REG_WRITE
    dec a
    cp 0x30
    jr nc,REG_MOP_SKIP     ; regs 0xB3 down to 0x30
    ld a,0x0f
    ex af,af'              ; A' = 0x0F: data for the release rates
    ld a,0x8f
REG_MOP_RR:
    call REG_WRITE
    dec a
    jp m,REG_MOP_RR        ; regs 0x8F down to 0x80
    xor a
    ex af,af'              ; A' = 0: key-off data
    ld a,0x28
    call REG_WRITE         ; key-off FM channel 1
    ex af,af'
    inc a                  ; data 1
    ex af,af'
    call REG_WRITE         ; key-off FM channel 2
    ex af,af'
    inc a                  ; data 2
    ex af,af'
    call REG_WRITE         ; key-off FM channel 3
    dec a                  ; A = 0x27: the register for the next write
    call REG_WRITE         ; timer control <- 2
    ld a,0x7f
    ex af,af'              ; A' = 0x7F: data for everything below
    ld a,0x4f
REG_MOP_TL:
    call REG_WRITE         ; total level <- max attenuation
    dec a
    cp 0x40
    jr nc,REG_MOP_TL       ; regs 0x4F down to 0x40
    ld a,0x2f
    call REG_WRITE
    ld a,0x2d              ; final write: fall through into REG_WRITE itself

; =============================================================================
;  REG_WRITE - one polled register/data pair
; =============================================================================
;  A = register number, A' = data byte.  B/D/E/C form the port pair (see the
;  protocol notes in the file header).  WAIT_DATA is the famous park site on
;  legacy 2xAY TurboSound boards (status reads back 0xFF there).
; =============================================================================
REG_WRITE:
    ld b,d                 ; B = 0xFF -> port 0xFFFD
WAIT_REG:
    in f,(c)               ; status into flags only: A survives
    jp m,WAIT_REG          ; bit7 set: chip busy, try again
    out (c),a              ; register select
    ex af,af'              ; bring the data byte into A
WAIT_DATA:
    in f,(c)
    jp m,WAIT_DATA
    ld b,e                 ; B = 0xBF -> port 0xBFFD (re-armed only now -
    out (c),a              ;  the original keeps this OUT's setup asymmetric
    ex af,af'              ;  with the register half, see ld b,d above)
    ret

; =============================================================================
;  Chip selects and the Pentagon service entry
; =============================================================================
SELECT_F8:
    ld a,0xf8              ; control word: chip 0 (YM #1) active
    ld b,d
    out (c),a
    ret

SELECT_F9:
    ld a,0xf9              ; control word: chip 1 (YM #2) active
    ld b,d
    out (c),a
    ret

OUT_7FFD:                  ; service entry, never called from inside the
    push bc                ; player: write A to the 0x7FFD paging latch.
    ld bc,0x7ffd           ; Embedders use it to restore ROM paging after
    out (c),a              ; a TR-DOS load (the tsfm-elite boot stub does
    pop bc                 ; exactly this).
    ret

; =============================================================================
;  FRAME - the per-interrupt play routine
; =============================================================================
;  Chip 0's three channels first, then chip 1's.  Each stub interprets its
;  own stream and returns when its event for this frame is done.  The tail
;  is the SMC frame-rate divider (see INIT): for TFMcom 1.12 tunes the byte
;  at FRAME_DIVIDER is a RET and control never reaches the counter at all;
;  for old-format tunes the counter plays the body only every 6th wrap.
; =============================================================================
FRAME:
    ld de,0xffbf           ; D/E = port high bytes, same convention as above
    ld c,0xfd
    ld b,d
    ld a,0xf8
    out (c),a              ; chip 0 active
    call CH1
    call CH2
    call CH3
    ld b,d
    ld a,0xf9
    out (c),a              ; chip 1 active
    call CH4
    call CH5
    call CH6
FRAME_DIVIDER:             ; SMC: INIT overwrites this LD opcode with RET
    ld a,6                 ; counter lives in this immediate (see below)
    dec a
    jr nz,FD_STORE
    ld a,6                 ; wrapped: reload the immediate ...
FD_STORE:
    ld (FRAME_DIVIDER+1),a ; ... i.e. the counter persists inside the code
    jr z,FRAME             ; wrapped -> run the frame body right now
    ret                    ; otherwise this interrupt is skipped

; =============================================================================
;  OUTI_PAIRS - bulk (register,data) writer
; =============================================================================
;  A = number of pairs, HL -> the pairs, register byte first.  One poll per
;  OUT: the chips stay busy for ~32 cycles after every write.  Used by the
;  event opcodes to push a whole voice (DT/MULTI, TL, RS/AR, ...) in one go.
; =============================================================================
OUTI_PAIRS:
    ld b,d                 ; B = 0xFF -> 0xFFFD
OUTI_REG_POLL:
    in f,(c)
    jp m,OUTI_REG_POLL
    outi                   ; register byte -> 0xFFFD, HL++
OUTI_DAT_POLL:
    in f,(c)
    jp m,OUTI_DAT_POLL
    ld b,e                 ; B = 0xBF -> 0xBFFD
    outi                   ; data byte -> 0xBFFD, HL++
    dec a
    jr nz,OUTI_PAIRS       ; next pair
    ret

; =============================================================================
;  CHANNEL 1 - chip 0, FM channel 1        (F-num regs A4/A0, key bits 0)
; =============================================================================
;  The compiler stamped out six private copies of the whole interpreter,
;  one per YM2203 channel.  Each copy is three pieces:
;
;    helpers    instrument / call / second dispatch / register write / loop
;    stub       the per-frame prologue (state, resume, instrument counters)
;    dispatch   opcode fetch and the event execution itself
;
;  All inter-piece communication is through self-modified immediates - the
;  "slots" listed below - so no RAM variables are needed and the whole
;  player is position-locked ROMmable code.  Channels 2-6 are byte-for-byte
;  the same design with different constants; only channel 1 carries full
;  commentary.
;
;  SMC slot map of this copy (address = instruction + operand offset):
;    CH1_STATE   stub+ 1   LD A,nn      wait/state counter
;    CH1_RESUME  stub+ 6   LD HL,nnnn   main-stream position
;    CH1_COUNT   stub+ 9   OR nn        instrument frames remaining
;    CH1_RETURN  stub+19   LD HL,nnnn   position to resume at after instr.
;    CH1_DELTA   helper     ADD A,nn    relative-note delta base
;    CH1_FREQH   helper     LD HL,nnnn  cached F-num high byte (the H)
;    CH1_LOOPPT  helper     LD HL,nnnn  pattern loop point
; =============================================================================

; -----------------------------------------------------------------------------
;  Helpers of channel 1
; -----------------------------------------------------------------------------
;  INSTRUMENT (opcode 0xD0):  [len] [off_hi] [off_lo].  Seeds the frame
;  counter, saves the return position, then tail-shares into DISPATCH so
;  the instrument's first event runs this very frame.  Offsets are stored
;  BIG-ENDIAN and are effectively signed (in practice always backward).
; -----------------------------------------------------------------------------
CH1_INSTRUMENT:
    ld a,(hl)              ; instrument length in dispatched frames
    inc hl
    ld (CH1_COUNT),a       ; seed the OR nn counter slot (SMC)
    ld b,(hl)              ; offset high byte ...
    inc hl
    ld c,(hl)              ; ... then low byte: big-endian!
    inc hl
    ld (CH1_RETURN),hl     ; main-stream position for after the instrument
    add hl,bc              ; HL -> the instrument's event sub-stream
    ld c,0xfd
    jp CH1_DISPATCH        ; run its first event now

; -----------------------------------------------------------------------------
;  PATTERN CALLS.  0xBF carries a 16-bit big-endian signed offset; 0xFF
;  carries one unsigned byte - and enters at CH1_CALL8, where B (loaded
;  with the opcode 0xFF by the second dispatch) turns the byte offset into
;  a backward-by-0x100 window.  Both execute exactly ONE event from the
;  target this frame and resume the main stream after the operand.
; -----------------------------------------------------------------------------
CH1_CALL16:
    ld b,(hl)              ; offset, big-endian
    inc hl
CH1_CALL8:                 ; 0xFF lands here: B = 0xFF already in place
    ld c,(hl)
    inc hl
    push hl                ; resume position (after the operand)
    add hl,bc              ; HL -> the called event
    ld c,0xfd
    call CH1_DISPATCH      ; execute one event from the target
    pop hl
    ld (CH1_RESUME),hl     ; next frame continues the main stream
    ret

; -----------------------------------------------------------------------------
;  SECOND-LEVEL DISPATCH for opcodes >= 0xBF (entered with flags live from
;  CP E, E = 0xBF):
;     0xBF        pattern call, 16-bit offset
;     0xC0..0xDF  relative note (0xD0 = instrument, caught inside)
;     0xE0..0xFE  wait - the opcode itself becomes the channel state
;     0xFF        pattern call, 8-bit offset
; -----------------------------------------------------------------------------
CH1_BIGDISPATCH:
    jr z,CH1_CALL16        ; Z: opcode was exactly 0xBF
    cp 0xe0
    jr c,CH1_REGWRITE      ; 0xC0..0xDF
    ld b,a                 ; keep the opcode: B completes the 0xFF window
    cp 0xff
    jr z,CH1_CALL8         ; enter the call mid-sequence
    ld (CH1_RESUME),hl     ; wait: park the stream position ...
CH1_SETSTATE:
    ld (CH1_STATE),a       ; ... and store the opcode as the wait counter
    ret

; -----------------------------------------------------------------------------
;  RELATIVE NOTE (0xC0..0xDF, except 0xD0).  The new F-num low byte is
;  computed as  opcode + 0x30 + delta , where the delta addend is the
;  cached low byte of the last absolute frequency.  The ADD's immediate IS
;  the cache, and it self-updates right below - two SMC slots in three
;  instructions.  The pair (A4 <- cached high, A0 <- computed low) is then
;  written through CH1_WRITEREG.
; -----------------------------------------------------------------------------
CH1_REGWRITE:
    add a,0x30
    jr z,CH1_INSTRUMENT    ; wrapped to zero: opcode was 0xD0
CH1_ADDDELTA:
    add a,0xc3             ; immediate = CH1_DELTA (SMC): the delta base
    ld (CH1_DELTA),a       ; remember the new delta base for next time
    ld (CH1_RESUME),hl
    ld b,d
CH1_FREQCONST:             ; bytes 21 A4 22: L = reg A4, H = CH1_FREQH
    ld hl,0x22a4
CH1_WRITEREG:              ; write L as register, H as data, then A0 <- A
    in f,(c)
    jp m,CH1_WRITEREG
    out (c),l              ; register A4
CH1_WRITEREG2:
    in f,(c)
    jp m,CH1_WRITEREG2
    ld b,e
    out (c),h              ; cached F-num high byte
    ld b,d
    ld l,0xa0              ; now the low register of this FM channel
CH1_WRITEREG3:
    in f,(c)
    jp m,CH1_WRITEREG3
    out (c),l              ; register A0
CH1_WRITEREG4:
    in f,(c)
    jp m,CH1_WRITEREG4
    ld b,e
    out (c),a              ; computed F-num low byte
    ret

; -----------------------------------------------------------------------------
;  LOOP MARK (0x7E) / LOOP RESTART (0x7F).  0x7E plants the current stream
;  position into CH1_RESTART's immediate and dispatches again in the SAME
;  frame (marks cost no time); 0x7F reloads HL from that slot - the
;  pattern's endless loop, also within the same frame.
; -----------------------------------------------------------------------------
CH1_LOOP:
    ld (CH1_LOOPPT),hl     ; plant the loop point (SMC)
    jp CH1_DISPATCH        ; same frame, next event
CH1_RESTART:
    ld hl,0                ; immediate = CH1_LOOPPT, planted by 0x7E
    jp CH1_DISPATCH

; SMC slot addresses of this copy (see the map in the channel banner).
; The same +1/+6/+9/+19 offsets hold for every channel clone: the stub
; layout is identical, only the dispatch's key-pulse instruction differs.
CH1_STATE   equ CH1+1         ; LD A,nn immediate in the stub
CH1_RESUME  equ CH1+6         ; first LD HL,nnnn immediate in the stub
CH1_COUNT   equ CH1+9         ; OR nn immediate in the stub
CH1_RETURN  equ CH1+19        ; second LD HL,nnnn immediate in the stub
CH1_DELTA   equ CH1_ADDDELTA+1
CH1_FREQH   equ CH1_FREQCONST+2
CH1_LOOPPT  equ CH1_RESTART+1

; -----------------------------------------------------------------------------
;  Channel 1 stub - the per-frame prologue
; -----------------------------------------------------------------------------
;  Every interrupt, FRAME calls here.  Three gates, in order:
;
;    state    A wait counter (an opcode parked by CH1_SETSTATE).  INC A
;             until it wraps to zero - a wait of opcode N parks the channel
;             for 255-N frames.  INIT seeds 0xFF = "not started" so the
;             first call falls straight through to real work.
;
;    resume   HL = where the main stream continues (LD HL,nnnn slot).
;
;    count    An instrument is running: OR nn tests it, DEC counts it down,
;             and while it is nonzero HL stays inside the instrument's
;             sub-stream.  On the wrap the second LD HL,nnnn (CH1_RETURN)
;             puts the main stream back.
;
;  Note how the counter lives inside the OR immediate, so testing it,
;  decrementing it and storing it back are three different views of the
;  same code byte.
; -----------------------------------------------------------------------------
CH1:
    ld a,0xfd              ; immediate = CH1_STATE (SMC; INIT writes 0xFF)
    inc a                  ; wait counter + 1 ...
    jr nz,CH1_SETSTATE     ; ... still counting: store it back and idle
    ld hl,0                ; immediate = CH1_RESUME: main-stream position
    or 0                   ; immediate = CH1_COUNT: instrument frames left
    jr z,CH1_DISPATCH      ; no instrument -> plain event dispatch
    dec a                  ; one instrument frame consumed ...
    ld (CH1_COUNT),a       ; ... stored back into the OR immediate
    jr nz,CH1_DISPATCH     ; still inside the instrument sub-stream
    ld hl,0                ; immediate = CH1_RETURN: back to the main stream

; -----------------------------------------------------------------------------
;  Channel 1 dispatch - opcode fetch and event execution
; -----------------------------------------------------------------------------
;  Event opcodes (0x00..0xBE) are two packed fields:
;
;      bit 7    1 = pulse the key-control register around the event
;      bit 0    1 = two absolute frequency bytes follow the opcode
;      bits1-5  N = number of inline (register,data) pairs that follow
;
;  The famous twist: after the bit-0 test A holds opcode>>1, so the pair
;  count is (opcode>>1) & 0x1F - bit 6 is simply lost by the shift, which
;  is why 0x40..0x7D duplicate the 0x00..0x3D encodings in real tunes.
; -----------------------------------------------------------------------------
CH1_DISPATCH:
    ld a,(hl)              ; opcode
    inc hl
    cp 0x7e
    jr z,CH1_LOOP          ; loop mark: same-frame, free
    cp 0x7f
    jr z,CH1_RESTART       ; loop restart: same-frame, free
    cp e                   ; E = 0xBF: register reuse as a constant
    jr nc,CH1_BIGDISPATCH  ; opcodes >= 0xBF go to the second level
    jp p,CH1_NOTE          ; bit7 clear: plain event, no key pulse
    ex af,af'              ; 0x80..0xBE: park the opcode ...
    ld b,d
    ld a,0x28
CH1_KEYON1:
    in f,(c)
    jp m,CH1_KEYON1
    out (c),a              ; ... pulse key-control ...
    xor a                  ; data: channel 1 (of this chip)
CH1_KEYON2:
    in f,(c)
    jp m,CH1_KEYON2
    ld b,e
    out (c),a
    ex af,af'              ; ... opcode back into A
CH1_NOTE:
    or a                   ; flags from the opcode (bit7 = tail wanted)
    push af                ; keep it: the tail test needs it after the event
    rra                    ; bit0 -> carry: absolute frequency present?
    jr nc,CH1_PAIRS        ; no: straight to the pair count
    ex af,af'              ; park A = opcode>>1 for later
    ld b,d
    ld a,0xa4              ; F-num high register of FM channel 1
CH1_FQ1:
    in f,(c)
    jp m,CH1_FQ1
    out (c),a
    ld a,(hl)              ; absolute F-num, high byte
    inc hl
    ld (CH1_FREQH),a       ; cache it for relative notes (SMC)
CH1_FQ2:
    in f,(c)
    jp m,CH1_FQ2
    ld b,e
    out (c),a
    ld b,d
    ld a,0xa0              ; F-num low register
CH1_FQ3:
    in f,(c)
    jp m,CH1_FQ3
    out (c),a
    ld a,(hl)              ; absolute F-num, low byte
    inc hl
    ld (CH1_DELTA),a       ; seed the relative-note delta base (SMC)
CH1_FQ4:
    in f,(c)
    jp m,CH1_FQ4
    ld b,e
    out (c),a
    ex af,af'              ; A = opcode>>1 again
CH1_PAIRS:
    and 0x1f               ; N = (opcode >> 1) & 0x1F
    call nz,OUTI_PAIRS     ; N inline (register,data) pairs, shared writer
    ld (CH1_RESUME),hl     ; park the stream position for next frame
    pop af
    ret p                  ; bit7 clear: plain event, done
    ld b,d                 ; event tail: second key-control pulse
    ld a,0x28
CH1_KEYOFF1:
    in f,(c)
    jp m,CH1_KEYOFF1
    out (c),a
    ld a,0xf0              ; 0xF0 | channel bits: the closing strobe
CH1_KEYOFF2:
    in f,(c)
    jp m,CH1_KEYOFF2
    ld b,e
    out (c),a
    ret

; =============================================================================
;  CHANNEL 2 - chip 0, FM channel 2       (F-num regs A5/A1, key bits 1)
; =============================================================================
;  Byte-for-byte the channel 1 design.  Deltas: key-control data 1 and
;  0xF1, F-num registers A5/A1, cached-high constant 0x13A5, delta addend
;  0xAF, stub seed 0xEC - and the key pulse is LD A,1 instead of XOR A,
;  which makes this block one byte longer than channel 1's (0x101).
;  Comments below only where behaviour differs; see channel 1 for the rest.
; =============================================================================
CH2_INSTRUMENT:
    ld a,(hl)
    inc hl
    ld (CH2_COUNT),a
    ld b,(hl)
    inc hl
    ld c,(hl)
    inc hl
    ld (CH2_RETURN),hl
    add hl,bc
    ld c,0xfd
    jp CH2_DISPATCH

CH2_CALL16:
    ld b,(hl)
    inc hl
CH2_CALL8:
    ld c,(hl)
    inc hl
    push hl
    add hl,bc
    ld c,0xfd
    call CH2_DISPATCH
    pop hl
    ld (CH2_RESUME),hl
    ret

CH2_BIGDISPATCH:
    jr z,CH2_CALL16
    cp 0xe0
    jr c,CH2_REGWRITE
    ld b,a
    cp 0xff
    jr z,CH2_CALL8
    ld (CH2_RESUME),hl
CH2_SETSTATE:
    ld (CH2_STATE),a
    ret

CH2_REGWRITE:
    add a,0x30
    jr z,CH2_INSTRUMENT
CH2_ADDDELTA:
    add a,0xaf             ; delta addend constant differs per channel
    ld (CH2_DELTA),a
    ld (CH2_RESUME),hl
    ld b,d
CH2_FREQCONST:             ; L = reg A5, H = cached F-num high
    ld hl,0x13a5
CH2_WRITEREG:
    in f,(c)
    jp m,CH2_WRITEREG
    out (c),l
CH2_WRITEREG2:
    in f,(c)
    jp m,CH2_WRITEREG2
    ld b,e
    out (c),h
    ld b,d
    ld l,0xa1
CH2_WRITEREG3:
    in f,(c)
    jp m,CH2_WRITEREG3
    out (c),l
CH2_WRITEREG4:
    in f,(c)
    jp m,CH2_WRITEREG4
    ld b,e
    out (c),a
    ret

CH2_LOOP:
    ld (CH2_LOOPPT),hl
    jp CH2_DISPATCH
CH2_RESTART:
    ld hl,0
    jp CH2_DISPATCH

; SMC slots (same instruction map as channel 1)
CH2_STATE   equ CH2+1
CH2_RESUME  equ CH2+6
CH2_COUNT   equ CH2+9
CH2_RETURN  equ CH2+19
CH2_DELTA   equ CH2_ADDDELTA+1
CH2_FREQH   equ CH2_FREQCONST+2
CH2_LOOPPT  equ CH2_RESTART+1

CH2:
    ld a,0xec              ; seed differs; INIT overwrites with 0xFF anyway
    inc a
    jr nz,CH2_SETSTATE
    ld hl,0
    or 0
    jr z,CH2_DISPATCH
    dec a
    ld (CH2_COUNT),a
    jr nz,CH2_DISPATCH
    ld hl,0

CH2_DISPATCH:
    ld a,(hl)
    inc hl
    cp 0x7e
    jr z,CH2_LOOP
    cp 0x7f
    jr z,CH2_RESTART
    cp e
    jr nc,CH2_BIGDISPATCH
    jp p,CH2_NOTE
    ex af,af'
    ld b,d
    ld a,0x28
CH2_KEYON1:
    in f,(c)
    jp m,CH2_KEYON1
    out (c),a
    ld a,1                 ; key data: FM channel 2
CH2_KEYON2:
    in f,(c)
    jp m,CH2_KEYON2
    ld b,e
    out (c),a
    ex af,af'
CH2_NOTE:
    or a
    push af
    rra
    jr nc,CH2_PAIRS
    ex af,af'
    ld b,d
    ld a,0xa5              ; F-num high register of FM channel 2
CH2_FQ1:
    in f,(c)
    jp m,CH2_FQ1
    out (c),a
    ld a,(hl)
    inc hl
    ld (CH2_FREQH),a
CH2_FQ2:
    in f,(c)
    jp m,CH2_FQ2
    ld b,e
    out (c),a
    ld b,d
    ld a,0xa1
CH2_FQ3:
    in f,(c)
    jp m,CH2_FQ3
    out (c),a
    ld a,(hl)
    inc hl
    ld (CH2_DELTA),a
CH2_FQ4:
    in f,(c)
    jp m,CH2_FQ4
    ld b,e
    out (c),a
    ex af,af'
CH2_PAIRS:
    and 0x1f
    call nz,OUTI_PAIRS
    ld (CH2_RESUME),hl
    pop af
    ret p
    ld b,d
    ld a,0x28
CH2_KEYOFF1:
    in f,(c)
    jp m,CH2_KEYOFF1
    out (c),a
    ld a,0xf1              ; 0xF0 | 1: closing strobe for channel 2
CH2_KEYOFF2:
    in f,(c)
    jp m,CH2_KEYOFF2
    ld b,e
    out (c),a
    ret

; =============================================================================
;  CHANNEL 3 - chip 0, FM channel 3       (F-num regs A6/A2, key bits 2)
; =============================================================================
;  Deltas vs channel 1: key data 2 / 0xF2, registers A6/A2, cached-high
;  constant 0x1AA6, delta addend 0xC3, stub seed 0xEC, LD A,2 key pulse.
; =============================================================================
CH3_INSTRUMENT:
    ld a,(hl)
    inc hl
    ld (CH3_COUNT),a
    ld b,(hl)
    inc hl
    ld c,(hl)
    inc hl
    ld (CH3_RETURN),hl
    add hl,bc
    ld c,0xfd
    jp CH3_DISPATCH

CH3_CALL16:
    ld b,(hl)
    inc hl
CH3_CALL8:
    ld c,(hl)
    inc hl
    push hl
    add hl,bc
    ld c,0xfd
    call CH3_DISPATCH
    pop hl
    ld (CH3_RESUME),hl
    ret

CH3_BIGDISPATCH:
    jr z,CH3_CALL16
    cp 0xe0
    jr c,CH3_REGWRITE
    ld b,a
    cp 0xff
    jr z,CH3_CALL8
    ld (CH3_RESUME),hl
CH3_SETSTATE:
    ld (CH3_STATE),a
    ret

CH3_REGWRITE:
    add a,0x30
    jr z,CH3_INSTRUMENT
CH3_ADDDELTA:
    add a,0xc3
    ld (CH3_DELTA),a
    ld (CH3_RESUME),hl
    ld b,d
CH3_FREQCONST:             ; L = reg A6, H = cached F-num high
    ld hl,0x1aa6
CH3_WRITEREG:
    in f,(c)
    jp m,CH3_WRITEREG
    out (c),l
CH3_WRITEREG2:
    in f,(c)
    jp m,CH3_WRITEREG2
    ld b,e
    out (c),h
    ld b,d
    ld l,0xa2
CH3_WRITEREG3:
    in f,(c)
    jp m,CH3_WRITEREG3
    out (c),l
CH3_WRITEREG4:
    in f,(c)
    jp m,CH3_WRITEREG4
    ld b,e
    out (c),a
    ret

CH3_LOOP:
    ld (CH3_LOOPPT),hl
    jp CH3_DISPATCH
CH3_RESTART:
    ld hl,0
    jp CH3_DISPATCH

; SMC slots (same instruction map as channel 1)
CH3_STATE   equ CH3+1
CH3_RESUME  equ CH3+6
CH3_COUNT   equ CH3+9
CH3_RETURN  equ CH3+19
CH3_DELTA   equ CH3_ADDDELTA+1
CH3_FREQH   equ CH3_FREQCONST+2
CH3_LOOPPT  equ CH3_RESTART+1

CH3:
    ld a,0xec
    inc a
    jr nz,CH3_SETSTATE
    ld hl,0
    or 0
    jr z,CH3_DISPATCH
    dec a
    ld (CH3_COUNT),a
    jr nz,CH3_DISPATCH
    ld hl,0

CH3_DISPATCH:
    ld a,(hl)
    inc hl
    cp 0x7e
    jr z,CH3_LOOP
    cp 0x7f
    jr z,CH3_RESTART
    cp e
    jr nc,CH3_BIGDISPATCH
    jp p,CH3_NOTE
    ex af,af'
    ld b,d
    ld a,0x28
CH3_KEYON1:
    in f,(c)
    jp m,CH3_KEYON1
    out (c),a
    ld a,2                 ; key data: FM channel 3
CH3_KEYON2:
    in f,(c)
    jp m,CH3_KEYON2
    ld b,e
    out (c),a
    ex af,af'
CH3_NOTE:
    or a
    push af
    rra
    jr nc,CH3_PAIRS
    ex af,af'
    ld b,d
    ld a,0xa6              ; F-num high register of FM channel 3
CH3_FQ1:
    in f,(c)
    jp m,CH3_FQ1
    out (c),a
    ld a,(hl)
    inc hl
    ld (CH3_FREQH),a
CH3_FQ2:
    in f,(c)
    jp m,CH3_FQ2
    ld b,e
    out (c),a
    ld b,d
    ld a,0xa2
CH3_FQ3:
    in f,(c)
    jp m,CH3_FQ3
    out (c),a
    ld a,(hl)
    inc hl
    ld (CH3_DELTA),a
CH3_FQ4:
    in f,(c)
    jp m,CH3_FQ4
    ld b,e
    out (c),a
    ex af,af'
CH3_PAIRS:
    and 0x1f
    call nz,OUTI_PAIRS
    ld (CH3_RESUME),hl
    pop af
    ret p
    ld b,d
    ld a,0x28
CH3_KEYOFF1:
    in f,(c)
    jp m,CH3_KEYOFF1
    out (c),a
    ld a,0xf2              ; 0xF0 | 2: closing strobe for channel 3
CH3_KEYOFF2:
    in f,(c)
    jp m,CH3_KEYOFF2
    ld b,e
    out (c),a
    ret

; =============================================================================
;  CHANNEL 4 - chip 1, FM channel 1       (F-num regs A4/A0, key bits 0)
; =============================================================================
;  Chip 1's copy - FRAME selects 0xF9 before calling CH4..CH6.  Register
;  values repeat channel 1's (both chips are YM2203s with identical maps):
;  key data 0 / 0xF0 via XOR A, registers A4/A0, cached-high 0x1AA4, delta
;  addend 0xC3, stub seed 0xFE.  Block size 0x100, like channel 1.
; =============================================================================
CH4_INSTRUMENT:
    ld a,(hl)
    inc hl
    ld (CH4_COUNT),a
    ld b,(hl)
    inc hl
    ld c,(hl)
    inc hl
    ld (CH4_RETURN),hl
    add hl,bc
    ld c,0xfd
    jp CH4_DISPATCH

CH4_CALL16:
    ld b,(hl)
    inc hl
CH4_CALL8:
    ld c,(hl)
    inc hl
    push hl
    add hl,bc
    ld c,0xfd
    call CH4_DISPATCH
    pop hl
    ld (CH4_RESUME),hl
    ret

CH4_BIGDISPATCH:
    jr z,CH4_CALL16
    cp 0xe0
    jr c,CH4_REGWRITE
    ld b,a
    cp 0xff
    jr z,CH4_CALL8
    ld (CH4_RESUME),hl
CH4_SETSTATE:
    ld (CH4_STATE),a
    ret

CH4_REGWRITE:
    add a,0x30
    jr z,CH4_INSTRUMENT
CH4_ADDDELTA:
    add a,0xc3
    ld (CH4_DELTA),a
    ld (CH4_RESUME),hl
    ld b,d
CH4_FREQCONST:             ; L = reg A4, H = cached F-num high
    ld hl,0x1aa4
CH4_WRITEREG:
    in f,(c)
    jp m,CH4_WRITEREG
    out (c),l
CH4_WRITEREG2:
    in f,(c)
    jp m,CH4_WRITEREG2
    ld b,e
    out (c),h
    ld b,d
    ld l,0xa0
CH4_WRITEREG3:
    in f,(c)
    jp m,CH4_WRITEREG3
    out (c),l
CH4_WRITEREG4:
    in f,(c)
    jp m,CH4_WRITEREG4
    ld b,e
    out (c),a
    ret

CH4_LOOP:
    ld (CH4_LOOPPT),hl
    jp CH4_DISPATCH
CH4_RESTART:
    ld hl,0
    jp CH4_DISPATCH

; SMC slots (same instruction map as channel 1)
CH4_STATE   equ CH4+1
CH4_RESUME  equ CH4+6
CH4_COUNT   equ CH4+9
CH4_RETURN  equ CH4+19
CH4_DELTA   equ CH4_ADDDELTA+1
CH4_FREQH   equ CH4_FREQCONST+2
CH4_LOOPPT  equ CH4_RESTART+1

CH4:
    ld a,0xfe
    inc a
    jr nz,CH4_SETSTATE
    ld hl,0
    or 0
    jr z,CH4_DISPATCH
    dec a
    ld (CH4_COUNT),a
    jr nz,CH4_DISPATCH
    ld hl,0

CH4_DISPATCH:
    ld a,(hl)
    inc hl
    cp 0x7e
    jr z,CH4_LOOP
    cp 0x7f
    jr z,CH4_RESTART
    cp e
    jr nc,CH4_BIGDISPATCH
    jp p,CH4_NOTE
    ex af,af'
    ld b,d
    ld a,0x28
CH4_KEYON1:
    in f,(c)
    jp m,CH4_KEYON1
    out (c),a
    xor a                  ; key data: FM channel 1 (of chip 1)
CH4_KEYON2:
    in f,(c)
    jp m,CH4_KEYON2
    ld b,e
    out (c),a
    ex af,af'
CH4_NOTE:
    or a
    push af
    rra
    jr nc,CH4_PAIRS
    ex af,af'
    ld b,d
    ld a,0xa4
CH4_FQ1:
    in f,(c)
    jp m,CH4_FQ1
    out (c),a
    ld a,(hl)
    inc hl
    ld (CH4_FREQH),a
CH4_FQ2:
    in f,(c)
    jp m,CH4_FQ2
    ld b,e
    out (c),a
    ld b,d
    ld a,0xa0
CH4_FQ3:
    in f,(c)
    jp m,CH4_FQ3
    out (c),a
    ld a,(hl)
    inc hl
    ld (CH4_DELTA),a
CH4_FQ4:
    in f,(c)
    jp m,CH4_FQ4
    ld b,e
    out (c),a
    ex af,af'
CH4_PAIRS:
    and 0x1f
    call nz,OUTI_PAIRS
    ld (CH4_RESUME),hl
    pop af
    ret p
    ld b,d
    ld a,0x28
CH4_KEYOFF1:
    in f,(c)
    jp m,CH4_KEYOFF1
    out (c),a
    ld a,0xf0
CH4_KEYOFF2:
    in f,(c)
    jp m,CH4_KEYOFF2
    ld b,e
    out (c),a
    ret

; =============================================================================
;  CHANNEL 5 - chip 1, FM channel 2       (F-num regs A5/A1, key bits 1)
; =============================================================================
;  Deltas vs channel 1: key data 1 / 0xF1, registers A5/A1, cached-high
;  0x1BA5, delta addend 0xAF, stub seed 0xFD, LD A,1 key pulse.
; =============================================================================
CH5_INSTRUMENT:
    ld a,(hl)
    inc hl
    ld (CH5_COUNT),a
    ld b,(hl)
    inc hl
    ld c,(hl)
    inc hl
    ld (CH5_RETURN),hl
    add hl,bc
    ld c,0xfd
    jp CH5_DISPATCH

CH5_CALL16:
    ld b,(hl)
    inc hl
CH5_CALL8:
    ld c,(hl)
    inc hl
    push hl
    add hl,bc
    ld c,0xfd
    call CH5_DISPATCH
    pop hl
    ld (CH5_RESUME),hl
    ret

CH5_BIGDISPATCH:
    jr z,CH5_CALL16
    cp 0xe0
    jr c,CH5_REGWRITE
    ld b,a
    cp 0xff
    jr z,CH5_CALL8
    ld (CH5_RESUME),hl
CH5_SETSTATE:
    ld (CH5_STATE),a
    ret

CH5_REGWRITE:
    add a,0x30
    jr z,CH5_INSTRUMENT
CH5_ADDDELTA:
    add a,0xaf
    ld (CH5_DELTA),a
    ld (CH5_RESUME),hl
    ld b,d
CH5_FREQCONST:             ; L = reg A5, H = cached F-num high
    ld hl,0x1ba5
CH5_WRITEREG:
    in f,(c)
    jp m,CH5_WRITEREG
    out (c),l
CH5_WRITEREG2:
    in f,(c)
    jp m,CH5_WRITEREG2
    ld b,e
    out (c),h
    ld b,d
    ld l,0xa1
CH5_WRITEREG3:
    in f,(c)
    jp m,CH5_WRITEREG3
    out (c),l
CH5_WRITEREG4:
    in f,(c)
    jp m,CH5_WRITEREG4
    ld b,e
    out (c),a
    ret

CH5_LOOP:
    ld (CH5_LOOPPT),hl
    jp CH5_DISPATCH
CH5_RESTART:
    ld hl,0
    jp CH5_DISPATCH

; SMC slots (same instruction map as channel 1)
CH5_STATE   equ CH5+1
CH5_RESUME  equ CH5+6
CH5_COUNT   equ CH5+9
CH5_RETURN  equ CH5+19
CH5_DELTA   equ CH5_ADDDELTA+1
CH5_FREQH   equ CH5_FREQCONST+2
CH5_LOOPPT  equ CH5_RESTART+1

CH5:
    ld a,0xfd
    inc a
    jr nz,CH5_SETSTATE
    ld hl,0
    or 0
    jr z,CH5_DISPATCH
    dec a
    ld (CH5_COUNT),a
    jr nz,CH5_DISPATCH
    ld hl,0

CH5_DISPATCH:
    ld a,(hl)
    inc hl
    cp 0x7e
    jr z,CH5_LOOP
    cp 0x7f
    jr z,CH5_RESTART
    cp e
    jr nc,CH5_BIGDISPATCH
    jp p,CH5_NOTE
    ex af,af'
    ld b,d
    ld a,0x28
CH5_KEYON1:
    in f,(c)
    jp m,CH5_KEYON1
    out (c),a
    ld a,1                 ; key data: FM channel 2 (of chip 1)
CH5_KEYON2:
    in f,(c)
    jp m,CH5_KEYON2
    ld b,e
    out (c),a
    ex af,af'
CH5_NOTE:
    or a
    push af
    rra
    jr nc,CH5_PAIRS
    ex af,af'
    ld b,d
    ld a,0xa5
CH5_FQ1:
    in f,(c)
    jp m,CH5_FQ1
    out (c),a
    ld a,(hl)
    inc hl
    ld (CH5_FREQH),a
CH5_FQ2:
    in f,(c)
    jp m,CH5_FQ2
    ld b,e
    out (c),a
    ld b,d
    ld a,0xa1
CH5_FQ3:
    in f,(c)
    jp m,CH5_FQ3
    out (c),a
    ld a,(hl)
    inc hl
    ld (CH5_DELTA),a
CH5_FQ4:
    in f,(c)
    jp m,CH5_FQ4
    ld b,e
    out (c),a
    ex af,af'
CH5_PAIRS:
    and 0x1f
    call nz,OUTI_PAIRS
    ld (CH5_RESUME),hl
    pop af
    ret p
    ld b,d
    ld a,0x28
CH5_KEYOFF1:
    in f,(c)
    jp m,CH5_KEYOFF1
    out (c),a
    ld a,0xf1
CH5_KEYOFF2:
    in f,(c)
    jp m,CH5_KEYOFF2
    ld b,e
    out (c),a
    ret

; =============================================================================
;  CHANNEL 6 - chip 1, FM channel 3       (F-num regs A6/A2, key bits 2)
; =============================================================================
;  Last clone.  Deltas vs channel 1: key data 2 / 0xF2, registers A6/A2,
;  cached-high 0x23A6, delta addend 0xAF, stub seed 0xFF, LD A,2 key pulse.
;  The dispatch's RET is the final code byte: no helper tail follows it.
; =============================================================================
CH6_INSTRUMENT:
    ld a,(hl)
    inc hl
    ld (CH6_COUNT),a
    ld b,(hl)
    inc hl
    ld c,(hl)
    inc hl
    ld (CH6_RETURN),hl
    add hl,bc
    ld c,0xfd
    jp CH6_DISPATCH

CH6_CALL16:
    ld b,(hl)
    inc hl
CH6_CALL8:
    ld c,(hl)
    inc hl
    push hl
    add hl,bc
    ld c,0xfd
    call CH6_DISPATCH
    pop hl
    ld (CH6_RESUME),hl
    ret

CH6_BIGDISPATCH:
    jr z,CH6_CALL16
    cp 0xe0
    jr c,CH6_REGWRITE
    ld b,a
    cp 0xff
    jr z,CH6_CALL8
    ld (CH6_RESUME),hl
CH6_SETSTATE:
    ld (CH6_STATE),a
    ret

CH6_REGWRITE:
    add a,0x30
    jr z,CH6_INSTRUMENT
CH6_ADDDELTA:
    add a,0xaf
    ld (CH6_DELTA),a
    ld (CH6_RESUME),hl
    ld b,d
CH6_FREQCONST:             ; L = reg A6, H = cached F-num high
    ld hl,0x23a6
CH6_WRITEREG:
    in f,(c)
    jp m,CH6_WRITEREG
    out (c),l
CH6_WRITEREG2:
    in f,(c)
    jp m,CH6_WRITEREG2
    ld b,e
    out (c),h
    ld b,d
    ld l,0xa2
CH6_WRITEREG3:
    in f,(c)
    jp m,CH6_WRITEREG3
    out (c),l
CH6_WRITEREG4:
    in f,(c)
    jp m,CH6_WRITEREG4
    ld b,e
    out (c),a
    ret

CH6_LOOP:
    ld (CH6_LOOPPT),hl
    jp CH6_DISPATCH
CH6_RESTART:
    ld hl,0
    jp CH6_DISPATCH

; SMC slots (same instruction map as channel 1)
CH6_STATE   equ CH6+1
CH6_RESUME  equ CH6+6
CH6_COUNT   equ CH6+9
CH6_RETURN  equ CH6+19
CH6_DELTA   equ CH6_ADDDELTA+1
CH6_FREQH   equ CH6_FREQCONST+2
CH6_LOOPPT  equ CH6_RESTART+1

CH6:
    ld a,0xff
    inc a
    jr nz,CH6_SETSTATE
    ld hl,0
    or 0
    jr z,CH6_DISPATCH
    dec a
    ld (CH6_COUNT),a
    jr nz,CH6_DISPATCH
    ld hl,0

CH6_DISPATCH:
    ld a,(hl)
    inc hl
    cp 0x7e
    jr z,CH6_LOOP
    cp 0x7f
    jr z,CH6_RESTART
    cp e
    jr nc,CH6_BIGDISPATCH
    jp p,CH6_NOTE
    ex af,af'
    ld b,d
    ld a,0x28
CH6_KEYON1:
    in f,(c)
    jp m,CH6_KEYON1
    out (c),a
    ld a,2                 ; key data: FM channel 3 (of chip 1)
CH6_KEYON2:
    in f,(c)
    jp m,CH6_KEYON2
    ld b,e
    out (c),a
    ex af,af'
CH6_NOTE:
    or a
    push af
    rra
    jr nc,CH6_PAIRS
    ex af,af'
    ld b,d
    ld a,0xa6
CH6_FQ1:
    in f,(c)
    jp m,CH6_FQ1
    out (c),a
    ld a,(hl)
    inc hl
    ld (CH6_FREQH),a
CH6_FQ2:
    in f,(c)
    jp m,CH6_FQ2
    ld b,e
    out (c),a
    ld b,d
    ld a,0xa2
CH6_FQ3:
    in f,(c)
    jp m,CH6_FQ3
    out (c),a
    ld a,(hl)
    inc hl
    ld (CH6_DELTA),a
CH6_FQ4:
    in f,(c)
    jp m,CH6_FQ4
    ld b,e
    out (c),a
    ex af,af'
CH6_PAIRS:
    and 0x1f
    call nz,OUTI_PAIRS
    ld (CH6_RESUME),hl
    pop af
    ret p
    ld b,d
    ld a,0x28
CH6_KEYOFF1:
    in f,(c)
    jp m,CH6_KEYOFF1
    out (c),a
    ld a,0xf2
CH6_KEYOFF2:
    in f,(c)
    jp m,CH6_KEYOFF2
    ld b,e
    out (c),a
    ret

; =============================================================================
;  RELOC_TABLE - the tail that makes the player self-installing
; =============================================================================
;  Six words: the address of each channel stub's LD HL,nnnn immediate (its
;  CHn_RESUME slot).  INIT's LDI loop walks this table, converting the tune's
;  stream offsets into absolute addresses and storing them through these
;  pointers.  Because the table stores absolute addresses, the whole block
;  is position-locked to 0x61A8.
; =============================================================================
RELOC_TABLE:
    defw CH1_RESUME,CH2_RESUME,CH3_RESUME
    defw CH4_RESUME,CH5_RESUME,CH6_RESUME

;  The block must end exactly here: 1,883 bytes from 0x61A8.
    assert $ == 0x6903

; =============================================================================
;  End of the player.  Verified bit-identical to player_org61A8.bin with
;  the command pair at the top of this file.  This listing is hand-written
;  and hand-maintained: disasm_tfmplayer.py extracts the binary and builds
;  the raw z80dasm reference, it does not regenerate this file.
; =============================================================================
