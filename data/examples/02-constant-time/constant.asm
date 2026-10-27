;
; Constant-time dispatch for unreal-ng: every pass from Dispatch to Dispatch.End must take exactly 354 T-states
;
; Dispatch is entered with IY = one of eight handlers H0..H7 and A' + B = some data. The prologue adds them,
; branches on the carry, and jumps to the handler with JP (IY); the handler jumps to Dispatch.End. Every path is
; meant to take the same time on a machine without wait states (Pentagon):
;
;   prologue 36 (either way of the JR C)  +  handler body 308  +  JP Dispatch.End 10  =  354
;
; The default build has two timing bugs, the kind a timing table hides:
;   - H5 reads LD B,(HL) (7 T) where the table says LD B,A (4 T): every H5 pass is 3 T-states long
;   - the no-carry path lost its RET C (5 T, never taken), which balances JR C taken (12) against not taken (7):
;     every pass where A' + B does not carry is 5 T-states short
; sjasmplus -DFIXED builds the corrected program (constant-fixed.sna): 354 on every pass.
;
; Build: build.sh (sjasmplus --sym=constant.sym constant.asm; the same with -DFIXED)
;

        DEVICE ZXSPECTRUM48

Buf         EQU #C200           ; scratch memory the handlers chew on (clear of the IM2 table up to #C000)
IM2TABLE    EQU #BF00           ; 257 x #BE: every vector points at #BEBE
IM2HANDLER  EQU #BEBE           ; EI : RETI
PASSES      EQU 8               ; dispatches per frame

        ORG #8000

Start:
        di
        ld sp,#BE00
        ld hl,IM2TABLE
        ld b,0
.vectors:
        ld (hl),high IM2HANDLER
        inc hl
        djnz .vectors
        ld (hl),high IM2HANDLER
        ld hl,#4DED             ; EI : RETI at #BEBE (the interrupt only wakes the HALT)
        ld a,#FB
        ld (IM2HANDLER),a
        ld (IM2HANDLER+1),hl
        ld a,high IM2TABLE
        ld i,a
        im 2

Loop:
        ei
        halt                    ; one batch per frame
        di                      ; the timed code runs with interrupts off
        ld a,PASSES
        ld (Count),a
Next:
        call Random             ; a handler: IY = Handlers[random & 7]
        ld a,l
        and 7
        add a,a
        ld e,a
        ld d,0
        ld hl,Handlers
        add hl,de
        ld e,(hl)
        inc hl
        ld d,(hl)
        push de
        pop iy
        call Random             ; data: A' + B carries about half the time
        ex af,af'
        ld a,h
        ex af,af'
        ld b,l
        jp Dispatch

; --------------------------------------------------------------------------------------------------
; The timed code: entry breakpoint at Dispatch, exit breakpoint at Dispatch.End
; --------------------------------------------------------------------------------------------------
Dispatch:
        ex af,af'               ;  4
        add a,b                 ;  4
        exx                     ;  4
        jr c,.carry             ;  7 / 12
    IFDEF FIXED
        ret c                   ;  5, never taken: 7 + 5 = 12, as the JR C taken
    ENDIF
        ex af,af'               ;  4
        jp (iy)                 ;  8      = 36
.carry:
        ex af,af'               ;  4
        jp (iy)                 ;  8      = 36

Dispatch.End:
        exx
        ld hl,Count
        dec (hl)
        jp nz,Next
        jp Loop

; --------------------------------------------------------------------------------------------------
; Eight handlers, 308 T-states of body each (the running total on the right), then JP Dispatch.End
; --------------------------------------------------------------------------------------------------
H0:
        ld hl,Buf               ; 10   10
        ld de,Buf+16            ; 10   20
        ld bc,16                ; 10   30
        ldi                     ; 16   46
        ldi                     ; 16   62
        ldi                     ; 16   78
        ldi                     ; 16   94
        ldi                     ; 16  110
        ldi                     ; 16  126
        ldi                     ; 16  142
        ldi                     ; 16  158
        ld a,(hl)               ;  7  165
        add a,a                 ;  4  169
        ld (de),a               ;  7  176
        inc de                  ;  6  182
        rld                     ; 18  200
        ld b,4                  ;  7  207
.wait:  djnz .wait              ; 3 x 13 + 8 = 47  254
        ex (sp),hl              ; 19  273
        ex (sp),hl              ; 19  292
        neg                     ;  8  300
        nop                     ;  4  304
        nop                     ;  4  308
        jp Dispatch.End

H1:
        ld ix,Buf               ; 14   14
        ld a,(ix+1)             ; 19   33
        add a,(ix+2)            ; 19   52
        ld (ix+3),a             ; 19   71
        inc (ix+4)              ; 23   94
        ld hl,Buf               ; 10  104
        ld de,Buf+8             ; 10  114
        ld b,4                  ;  7  121
.mix:   ld a,(de)               ;  7
        xor (hl)                ;  7
        ld (hl),a               ;  7
        inc hl                  ;  6
        djnz .mix               ; 3 x 40 + 35 = 155  276
        ld a,r                  ;  9  285
        sbc hl,de               ; 15  300
        ex de,hl                ;  4  304
        nop                     ;  4  308
        jp Dispatch.End

H2:
        push af                 ; 11   11
        push bc                 ; 11   22
        push de                 ; 11   33
        push hl                 ; 11   44
        pop hl                  ; 10   54
        pop de                  ; 10   64
        pop bc                  ; 10   74
        pop af                  ; 10   84
        ld hl,Buf               ; 10   94
        ld b,6                  ;  7  101
.rot:   rlc (hl)                ; 15
        inc l                   ;  4
        djnz .rot               ; 5 x 32 + 27 = 187  288
        ld a,b                  ;  4  292
        or a                    ;  4  296
        ret nz                  ;  5  301 (B = 0: never taken)
        ld a,#55                ;  7  308
        jp Dispatch.End

H3:
        ld hl,Buf               ; 10   10
        ld b,10                 ;  7   17
        xor a                   ;  4   21
.sum:   add a,(hl)              ;  7
        inc hl                  ;  6
        djnz .sum               ; 9 x 26 + 21 = 255  276
        ld (Buf+40),a           ; 13  289
        cpl                     ;  4  293
        daa                     ;  4  297
        scf                     ;  4  301
        ld a,0                  ;  7  308
        jp Dispatch.End

H4:
        ld de,Buf               ; 10   10
        ld hl,Buf+32            ; 10   20
        ld bc,8                 ; 10   30
        ldir                    ; 7 x 21 + 16 = 163  193
        ld a,(Buf)              ; 13  206
        ld (Buf+50),a           ; 13  219
        ld hl,(Buf+2)           ; 16  235
        ld (Buf+52),hl          ; 16  251
        inc hl                  ;  6  257
        add hl,hl               ; 11  268
        add hl,bc               ; 11  279
        sbc hl,bc               ; 15  294
        rra                     ;  4  298
        ld bc,#1234             ; 10  308
        jp Dispatch.End

H5:
        ld hl,Buf               ; 10   10
        ld b,6                  ;  7   17
.shift: sla (hl)                ; 15
        inc hl                  ;  6
        djnz .shift             ; 5 x 34 + 29 = 199  216
        ld a,(Buf+3)            ; 13  229
        and #0F                 ;  7  236
        ld e,a                  ;  4  240
        ld d,0                  ;  7  247
        ld hl,Buf               ; 10  257
        add hl,de               ; 11  268
        ld a,(hl)               ;  7  275
        bit 7,a                 ;  8  283
        res 0,(hl)              ; 15  298
    IFDEF FIXED
        ld b,a                  ;  4  302
    ELSE
        ld b,(hl)               ;  4  302   <- the bug: LD B,(HL) takes 7
    ENDIF
        inc de                  ;  6  308
        jp Dispatch.End

H6:
        ld b,8                  ;  7    7
.idle:  nop                     ;  4
        djnz .idle              ; 7 x 17 + 12 = 131  138
        ld hl,Buf               ; 10  148
        ld c,(hl)               ;  7  155
        inc hl                  ;  6  161
        ld b,(hl)               ;  7  168
        ld (Buf+60),bc          ; 20  188
        ld de,(Buf+60)          ; 20  208
        ex de,hl                ;  4  212
        ld a,h                  ;  4  216
        xor l                   ;  4  220
        ld (Buf+62),a           ; 13  233
        push ix                 ; 15  248
        pop ix                  ; 14  262
        ld hl,Buf+8             ; 10  272
        rrd                     ; 18  290
        ld a,i                  ;  9  299
        ld a,r                  ;  9  308
        jp Dispatch.End

H7:
        ld hl,Buf               ; 10   10
        ld de,Buf+1             ; 10   20
        ld (hl),#AA             ; 10   30
        ld bc,10                ; 10   40
        ldir                    ; 9 x 21 + 16 = 205  245
        dec hl                  ;  6  251
        ld a,(hl)               ;  7  258
        cp #AA                  ;  7  265
        jr z,.same              ; 12  277 (always: the bytes were just filled)
.same:  inc a                   ;  4  281
        ld (de),a               ;  7  288
        ld a,0                  ;  7  295
        adc a,a                 ;  4  299
        nop                     ;  4  303
        ret c                   ;  5  308 (no carry: never taken)
        jp Dispatch.End

Handlers:
        dw H0, H1, H2, H3, H4, H5, H6, H7

; xorshift: HL = next value
Random:
        ld hl,(Seed)
        ld a,h
        rra
        ld a,l
        rra
        xor h
        ld h,a
        ld a,l
        rra
        ld a,h
        rra
        xor l
        ld l,a
        xor h
        ld h,a
        ld (Seed),hl
        ret

Count:  db 0
Seed:   dw #ACE1

    IFDEF FIXED
        SAVESNA "constant-fixed.sna", Start
    ELSE
        SAVESNA "constant.sna", Start
    ENDIF
