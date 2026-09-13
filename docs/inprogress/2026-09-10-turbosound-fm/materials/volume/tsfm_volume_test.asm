; =============================================================================
; tsfm_volume_test.asm - TSFM volume balance test (TFD player), sjasmplus source
; =============================================================================
; Reconstructed from tsfm_volume_test.spg ("TFD player test", SPG Builder 1.0,
; SHA-256 9f0db630b9a304cb6697862d13fe6512a6e89fc3e74a1e0723be762ad5e39073).
; Disassembled 2026-09-13 with z80dasm 1.2.0 and annotated by hand. The code
; assembles to the same 512 bytes as the SPG's code block (ORG 4000h); the
; TFMD stream is taken verbatim from the SPG (file offset 600h, 1536 bytes).
;
; BUILD (sjasmplus 1.20+):
;   sjasmplus tsfm_volume_test.asm                      -> tsfm_volume_test_48k.sna
;   sjasmplus -DSNA128 tsfm_volume_test.asm             -> tsfm_volume_test_128k.sna
;   sjasmplus -DTSCONF tsfm_volume_test.asm             -> tsfm_volume_test_tsconf.sna:
;                                                          keeps the TS-Conf port writes,
;                                                          code block byte-identical to
;                                                          the SPG (reference only)
; Both snapshots are for Pentagon 128 with [SOUND] TurboSound=FM. The 48K one
; loads on any 128K machine too (no paging is used: code at 4000h, stream at
; 8000h, IM2 table at 6E00h, stack below it).
;
; WHAT IT DOES
;   Plays ONE sound source at maximum level for 24 frames, all at ~1165 Hz:
;     chip0 FM ch0, ch1, ch2 -> chip0 SSG A, B, C ->
;     chip1 FM ch0, ch1, ch2 -> chip1 SSG A, B, C -> repeat (loop = 578 frames)
;   FM voice: algorithm 6, feedback 7, key-on of slots S1+S2 only; S2 is a
;   carrier at TL 0 (full level), S1 modulates it lightly (TL 24h); S3, S4 at
;   TL 7Fh. MUL 1, AR 31, no decay. Block 5, fnum 225h = 1166.9 Hz @ 1.75 MHz.
;   SSG voice: tone period 005Eh (94) = 1163.6 Hz @ 1.75 MHz, volume 15, no
;   envelope, only that channel un-muted in R7.
;   NOTE: one carrier at TL 0 is one operator's full output (about a quarter
;   of the chip's full-scale DAC word), not "YM2203 full scale".
;
; BOARD CONTROL WORD (OUT #FFFD, F8h..FFh - TSFM rev C):
;   bit0 chip (0 = D1, 1 = D2)   bit1 0 = status reads / 1 = register reads
;   bit2 0 = FM enabled / 1 = FM muted
;   F8h/F9h = chip 0/1, status mode, FM on   FEh/FFh = chip 0/1, register mode, FM muted
; =============================================================================

        IFDEF SNA128
        DEVICE ZXSPECTRUM128
        ELSE
        DEVICE ZXSPECTRUM48
        ENDIF

; -----------------------------------------------------------------------------
        ORG     4000h
; -----------------------------------------------------------------------------
entry:
        jp      start                   ; 4000

start:
        di                              ; 4003
        ld      sp,6E00h                ; 4004  stack grows down from the IM2 table
        ld      hl,int_handler          ; 4007
        ld      (6EFFh),hl              ; 400A  IM2 vector: I = 6Eh, data bus FFh
        ld      a,6Eh                   ; 400D
        ld      i,a                     ; 400F

        IFDEF TSCONF
        xor     a                       ; 4011  TS-Conf: #22AF HSINT, #23AF VSINTL,
        ld      b,22h                   ; 4012  #24AF VSINTH = 0 (assumes C = AFh
        out     (c),a                   ; 4014  on entry, as the SPG loader leaves it)
        inc     b                       ; 4016
        out     (c),a                   ; 4017
        inc     b                       ; 4019
        out     (c),a                   ; 401A
        ELSE
        DS      11,0                    ; 4011  Pentagon: nothing to configure
        ENDIF
        im      2                       ; 401C
        IFDEF TSCONF
        ld      a,01h                   ; 401E  TS-Conf: #2AAF INTMASK = frame INT only
        ld      bc,2AAFh                ; 4020
        out     (c),a                   ; 4023
        ld      a,17h                   ; 4025  TS-Conf: #15AF system config
        ld      bc,15AFh                ; 4027
        out     (c),a                   ; 402A
        ELSE
        DS      14,0                    ; 401E
        ENDIF

        ld      iy,7400h                ; 402C  scratch (IY+20h = "last write" flag)
        ld      hl,stream+4             ; 4030  TFMD stream after the "TFMD" signature
        ld      a,10h                   ; 4033
        ld      (7412h),a               ; 4035  page bookkeeping (unused here)
        inc     a                       ; 4038
        ld      (7413h),a               ; 4039
        ld      d,03h                   ; 403C  skip three zero-terminated strings
        xor     a                       ; 403E  (title / author / comment, all empty)
        ld      b,a                     ; 403F
        ld      c,a                     ; 4040
.skip:  cpir                            ; 4041
        ld      b,a                     ; 4043
        ld      c,a                     ; 4044
        dec     d                       ; 4045
        jp      nz,.skip                ; 4046
        inc     hl                      ; 4049  skip the FFh that follows the strings
        ld      a,10h                   ; 404A
        ld      (stream_ptr+1),hl       ; 404C  play pointer = first stream byte
        ld      (stream_page),a         ; 404F  play page    = 10h
        ld      (loop_ptr+1),hl         ; 4052  loop pointer = first stream byte
        ld      (loop_page),a           ; 4055  loop page    = 10h

; ---- reset both chips: every register 0, SSG mixer, prescaler /6 -----------
        ld      de,0FFBFh               ; 4058  D = high byte of #FFFD, E = of #BFFD
        ld      c,0FDh                  ; 405B
        ld      l,00h                   ; 405D  value for every register
        call    select_chip0_reg        ; 405F  OUT #FFFD,FEh (chip 0, register mode, FM muted)
        ld      a,0BFh                  ; 4062
.clr0:  dec     a                       ; 4064  registers BEh .. 00h
        ld      b,d                     ; 4065
        out     (c),a                   ; 4066  address
        ld      b,e                     ; 4068
        out     (c),l                   ; 4069  data 0
        jr      nz,.clr0                ; 406B
        ld      b,d                     ; 406D
        ld      a,07h                   ; 406E
        out     (c),a                   ; 4070  SSG R7 (mixer)
        ld      b,e                     ; 4072
        ld      a,0F8h                  ; 4073
        out     (c),a                   ; 4075    F8h: tones A,B,C on, noise off
        ld      b,d                     ; 4077
        ld      a,2Fh                   ; 4078
        out     (c),a                   ; 407A  prescaler address 2Fh
        ld      b,e                     ; 407C
        out     (c),a                   ; 407D  (same byte to #BFFD, harmless)
        ld      b,d                     ; 407F
        ld      a,2Dh                   ; 4080
        out     (c),a                   ; 4082  prescaler address 2Dh (/6 - the default)
        ld      b,e                     ; 4084
        out     (c),a                   ; 4085
        call    select_chip1_reg        ; 4087  OUT #FFFD,FFh (chip 1, register mode, FM muted)
        ld      a,0BFh                  ; 408A
.clr1:  dec     a                       ; 408C
        ld      b,d                     ; 408D
        out     (c),a                   ; 408E
        ld      b,e                     ; 4090
        out     (c),l                   ; 4091
        jr      nz,.clr1                ; 4093
        ld      b,d                     ; 4095
        ld      a,07h                   ; 4096
        out     (c),a                   ; 4098  SSG R7 on chip 1
        ld      b,e                     ; 409A
        ld      a,0C0h                  ; 409B
        out     (c),a                   ; 409D    C0h: tones and noise all on
        ld      b,d                     ; 409F    (the stream writes R7 = 3Fh in frame 0)
        ld      a,2Fh                   ; 40A0
        out     (c),a                   ; 40A2
        ld      b,e                     ; 40A4
        out     (c),a                   ; 40A5
        ld      b,d                     ; 40A7
        ld      a,2Dh                   ; 40A8
        out     (c),a                   ; 40AA
        ld      b,e                     ; 40AC
        out     (c),a                   ; 40AD
        ld      b,d                     ; 40AF
        ld      a,0F8h                  ; 40B0
        out     (c),a                   ; 40B2  OUT #FFFD,F8h: chip 0, STATUS mode, FM ON
        ei                              ; 40B4
.idle:  xor     a                       ; 40B5
        jp      .idle                   ; 40B6  everything else runs in the INT handler

select_chip1_reg:
        ld      b,d                     ; 40B9
        ld      a,d                     ; 40BA  A = FFh
        out     (c),a                   ; 40BB
        ret                             ; 40BD
select_chip0_reg:
        ld      b,d                     ; 40BE
        ld      a,0FEh                  ; 40BF
        out     (c),a                   ; 40C1
        ret                             ; 40C3
        ret                             ; 40C4  (unused byte in the original)

; ------------------------------------------------ frame interrupt handler ----
; One stream frame per interrupt; a pending delay (opcode FEh) counts down first.
int_handler:
        push    af                      ; 40C5
        push    bc                      ; 40C6
        push    de                      ; 40C7
        push    hl                      ; 40C8
delay_cnt:
        ld      a,00h                   ; 40C9  self-modified: frames still to wait
        and     a                       ; 40CB
        jp      z,play_frame            ; 40CC
        dec     a                       ; 40CF
        ld      (delay_cnt+1),a         ; 40D0
        jp      int_exit                ; 40D3

play_frame:
stream_ptr:
        ld      hl,0000h                ; 40D6  self-modified: current stream position
        ld      a,10h                   ; 40D9  40DAh = stream_page (self-modified)
stream_page     EQU     $-1
        ld      (7412h),a               ; 40DB
        inc     a                       ; 40DE
        ld      (7413h),a               ; 40DF
next_op:
        ld      a,(hl)                  ; 40E2
        inc     hl                      ; 40E3
        inc     a                       ; 40E4
        jr      z,op_end_frame          ; 40E5  FFh   : end of frame
        inc     a                       ; 40E7
        jr      z,op_delay              ; 40E8  FEh n : wait n+3 frames
        inc     a                       ; 40EA
        jr      z,op_chip1              ; 40EB  FDh   : select chip 1 (OUT #FFFD,F9h)
        inc     a                       ; 40ED
        jr      z,op_chip0              ; 40EE  FCh   : select chip 0 (OUT #FFFD,F8h)
        inc     a                       ; 40F0
        jr      z,op_loop               ; 40F1  FBh   : jump to the loop point
        inc     a                       ; 40F3
        jr      z,op_set_loop           ; 40F4  FAh   : set loop point (buggy, unused)
        sub     06h                     ; 40F6  else A = register, next byte = data
        ld      bc,0FFFDh               ; 40F8
        ld      (iy+20h),00h            ; 40FB
.wait1: in      f,(c)                   ; 40FF  wait while YM2203 busy (status bit 7)
        jp      m,.wait1                ; 4101
        out     (c),a                   ; 4104  register address -> #FFFD
.wait2: in      f,(c)                   ; 4106
        jp      m,.wait2                ; 4108
        ld      a,(hl)                  ; 410B
        ld      b,0BFh                  ; 410C
        out     (c),a                   ; 410E  data -> #BFFD
        inc     hl                      ; 4110
        ld      (iy+20h),02h            ; 4111
        jp      next_op                 ; 4115

op_end_frame:
        bit     6,h                     ; 4118  crossed C000h -> next 16K page
        jp      z,.save                 ; 411A  (never happens: the stream is 1.5 KB)
        res     6,h                     ; 411D
        ld      a,(stream_page)         ; 411F
        inc     a                       ; 4122
        ld      (stream_page),a         ; 4123
.save:  ld      (stream_ptr+1),hl       ; 4126
        ld      bc,0FFFDh               ; 4129
        ld      a,0F8h                  ; 412C
        out     (c),a                   ; 412E  back to chip 0 at the end of every frame
        jp      int_exit                ; 4130

op_delay:
        ld      a,(hl)                  ; 4133
        inc     hl                      ; 4134
        add     a,03h                   ; 4135
        ld      (delay_cnt+1),a         ; 4137  wait n+3 more interrupts
        jp      op_end_frame            ; 413A

op_chip1:
        ld      bc,0FFFDh               ; 413D
        ld      a,0F9h                  ; 4140
        out     (c),a                   ; 4142  chip 1, status mode, FM on
        jp      next_op                 ; 4144

op_chip0:
        ld      bc,0FFFDh               ; 4147
        ld      a,0F8h                  ; 414A
        out     (c),a                   ; 414C  chip 0, status mode, FM on
        jp      next_op                 ; 414E

op_set_loop:                            ; operands are crossed in the original
        ld      (loop_ptr+1),a          ; 4151  (A -> pointer low byte, HL -> page byte
        ld      (loop_page),hl          ; 4154  and the following opcode). Unused.
        jp      next_op                 ; 4157

op_loop:
loop_ptr:
        ld      hl,0000h                ; 415A  self-modified: loop position
        ld      a,00h                   ; 415D  415Eh = loop_page (self-modified)
loop_page       EQU     $-1
        ld      (stream_ptr+1),hl       ; 415F
        ld      (loop_page),a           ; 4162  (sic: writes the loop page back onto
        jp      next_op                 ; 4165   itself; stream_page is never restored)

int_exit:
        pop     hl                      ; 4168
        pop     de                      ; 4169
        pop     bc                      ; 416A
        pop     af                      ; 416B
        ei                              ; 416C
        ret                             ; 416D

        ASSERT $ == 416Eh
        DS      4200h-$,0               ; zero padding to the 512-byte block

; -----------------------------------------------------------------------------
        ORG     8000h
; -----------------------------------------------------------------------------
; TFMD stream, verbatim from the SPG (file offset 600h, 1536 bytes):
;   "TFMD", three empty zero-terminated strings, FFh, then the stream.
; Decoded (frames at the machine rate: 71680 T = 48.83 Hz, 24 frames = 0.4915 s):
;   frame   0  chip0: 12 x SL/RR=FF, key-off ch0..2 (28=00/01/02), R7=3F,
;                     R8..RA=0, prescaler 2D x2; FM ch0 voice:
;                       30/34/38/3C DT/MUL = 02 01 01 01
;                       40/44/48/4C TL     = 24 7F 00 7F  (S1 -27 dB, S3 off, S2 0 dB, S4 off)
;                       50..5C AR=1F  60..6C DR=00  70..7C SR=00
;                       80..8C SL/RR=0F  90..9C SSG-EG=00  B0 FB/ALG=3E (FB 7, alg 6)
;                       28=00, A4=2E, A0=25 (block 5, fnum 225h), 28=30 (key-on ch0, S1+S2)
;                     chip1: same clear sequence, SSG C period FF0F, R7=3F, RA=0
;   frame  24  chip0 FM ch1 voice, 28=00 (ch0 off), 28=31 (ch1 on)
;   frame  48  chip0 FM ch2 voice, 28=01 off, 28=32 on
;   frame  72  chip0 SSG A: period 005E, R7=3E, R8=0F ; 28=02 (FM ch2 off)
;   frame  96  chip0 SSG B: period 005E, R7=3D, R8=00, R9=0F
;   frame 120  chip0 SSG C: period 005E, R7=3B, R9=00, RA=0F
;   frame 144  chip0 R7=3F, RA=00 ; chip1 FM ch0 voice + key-on
;   frame 168  chip1 FM ch1        frame 192  chip1 FM ch2
;   frame 216  chip1 SSG A         frame 240  chip1 SSG B      frame 264  chip1 SSG C
;   frame 288  chip1 silent ; chip0 FM ch0 key-on (voice already programmed)
;   frame 312  chip0 FM ch1        frame 336  chip0 FM ch2
;   frame 360  chip0 SSG A         frame 384  chip0 SSG B      frame 408  chip0 SSG C
;   frame 432  chip1 FM ch0        frame 456  chip1 FM ch1     frame 480  chip1 FM ch2
;   frame 504  chip1 SSG A         frame 528  chip1 SSG B      frame 552  chip1 SSG C
;   frame 576  chip0 FM ch0 key-on, chip1 silent
;   frame 578  FB -> loop to frame 0
; Every frame ends with FE 15h (21+3 = 24 frames) and FFh.
stream:
        INCBIN  "tsfm_volume_test.spg", 600h, 1536

; -----------------------------------------------------------------------------
; Snapshot output. Registers other than PC do not matter: the program starts
; with DI and sets SP, I and IM itself. Port #7FFD = 10h (48 ROM, bank 0 at
; C000h, screen 5) in the 128K snapshot.
; -----------------------------------------------------------------------------
        IFDEF TSCONF
        SAVESNA "tsfm_volume_test_tsconf.sna", entry    ; reference build only
        ELSE
        IFDEF SNA128
        SAVESNA "tsfm_volume_test_128k.sna", entry
        ELSE
        SAVESNA "tsfm_volume_test_48k.sna", entry
        ENDIF
        ENDIF
