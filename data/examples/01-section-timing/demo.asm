;
; Section timing demo for unreal-ng (ZX Spectrum 48K / Pentagon)
;
; An attribute plasma, a varying number of XOR sparkles and an IM2 interrupt handler with a stand-in
; music player. Each measured piece of code has an entry label and an exit label (NAME and NAME.End,
; the exit label sits on the instruction that leaves it). The scripts next to this file put
; breakpoints on these labels and collect T-state statistics per section.
;
;   Frame     .. Frame.End      everything the main loop does in one frame
;   Plasma    .. Plasma.End     768 attribute cells, constant work
;   Sparkles  .. Sparkles.End   erase + draw 2..33 pixels, the count follows a sine: varying work
;   Isr       .. Isr.End        the frame interrupt (IM2), with Music inside
;
; Border profiling, the classic way: each routine paints the border its own colour while it runs, so the
; height of each stripe on the border shows how much of the frame it takes (one scan line = 224 T-states):
;   blue Plasma, red Sparkles, magenta the interrupt, green Music, black idle (HALT).
; The OUTs cost time too (they are inside the measured sections).
;
; Build: sjasmplus --sym=demo.sym demo.asm   (writes demo.sna, see build.sh)
;

        DEVICE ZXSPECTRUM48

SINTAB      EQU #9000           ; 256 bytes, 0..63, page aligned
PALETTE     EQU #9100           ; 256 attributes, page aligned
COLUMNS     EQU #9200           ; 32 bytes: the plasma's column values for this frame
MASKS       EQU #9300           ; 8 pixel masks, page aligned
SPARKBUF    EQU #9400           ; (x, y) of the sparkles drawn last frame, 2 bytes each
IM2TABLE    EQU #FE00           ; 257 x #FD
IM2JUMP     EQU #FDFD           ; JP Isr

BLACK       EQU 0               ; border colours: who runs now
BLUE        EQU 1
RED         EQU 2
MAGENTA     EQU 3
GREEN       EQU 4

        MACRO BORDER colour     ; the main loop's colour; the interrupt puts it back when it returns
        ld a,colour
        ld (BorderNow),a
        out (#FE),a
        ENDM

        ORG #8000

Start:
        di
        ld sp,#FDF0
        call InitTables
        call InitScreen
        ld a,high IM2TABLE
        ld i,a
        im 2
        ei

MainLoop:
        BORDER BLACK            ; idle
        halt                    ; wait for the frame interrupt
Frame:
        call Plasma
        call Sparkles
Frame.End:
        jr MainLoop

; --------------------------------------------------------------------------------------------------
; Attribute plasma: attr(x, y) = PALETTE[COLUMNS[x] + SIN(y)], 768 cells
; --------------------------------------------------------------------------------------------------
Plasma:
        BORDER BLUE
        ld a,(Phase1)           ; column values of this frame
        add a,3
        ld (Phase1),a
        ld c,a
        ld de,COLUMNS
        ld h,high SINTAB
        ld b,32
.column:
        ld l,c
        ld a,(hl)
        ld (de),a
        inc e
        ld a,c
        add a,9
        ld c,a
        djnz .column

        ld a,(Phase2)
        sub 2
        ld (Phase2),a
        ld (RowPhase),a
        ld a,24
        ld (RowsLeft),a
        ld hl,#5800
.row:
        ld a,(RowPhase)         ; this row's value goes into the ADD below
        ld e,a
        add a,11
        ld (RowPhase),a
        ld d,high SINTAB
        ld a,(de)
        ld (.add+1),a
        ld de,COLUMNS
        ld b,high PALETTE
.cell:
        ld a,(de)
.add:   add a,0
        ld c,a
        ld a,(bc)
        ld (hl),a               ; attribute write: contended on a 48K, free on a Pentagon
        inc hl
        inc e
        bit 5,e
        jr z,.cell
        ld a,(RowsLeft)
        dec a
        ld (RowsLeft),a
        jr nz,.row
Plasma.End:
        ret

; --------------------------------------------------------------------------------------------------
; Sparkles: XOR off last frame's pixels, XOR on a new set. The count follows a sine: 2..33 pixels
; --------------------------------------------------------------------------------------------------
Sparkles:
        BORDER RED
        ld a,(SparkCount)       ; erase the previous set
        or a
        jr z,.draw
        ld b,a
        ld ix,SPARKBUF
.erase:
        push bc
        ld c,(ix+0)
        ld b,(ix+1)
        call PixelXor
        inc ix
        inc ix
        pop bc
        djnz .erase

.draw:
        ld a,(Phase3)
        add a,2
        ld (Phase3),a
        ld l,a
        ld h,high SINTAB
        ld a,(hl)               ; 0..63
        srl a                   ; 0..31
        add a,2                 ; 2..33 pixels
        ld (SparkCount),a
        ld b,a
        ld ix,SPARKBUF
.new:
        push bc
        call Random             ; H = y candidate, L = x
        ld a,h
        cp 192
        jr c,.inside
        sub 64                  ; 192..255 -> 128..191
.inside:
        ld b,a
        ld c,l
        ld (ix+0),c
        ld (ix+1),b
        call PixelXor
        inc ix
        inc ix
        pop bc
        djnz .new
Sparkles.End:
        ret

; B = y (0..191), C = x: XOR that pixel
PixelXor:
        ld a,b                  ; H = 010 y7 y6 y2 y1 y0
        and 7
        or #40
        ld h,a
        ld a,b
        rra
        rra
        rra
        and #18
        or h
        ld h,a
        ld a,b                  ; L = y5 y4 y3 x7 x6 x5 x4 x3
        rla
        rla
        and #E0
        ld l,a
        ld a,c
        rra
        rra
        rra
        and #1F
        or l
        ld l,a
        ld a,c
        and 7
        ld e,a
        ld d,high MASKS
        ld a,(de)
        xor (hl)
        ld (hl),a
        ret

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

; --------------------------------------------------------------------------------------------------
; The frame interrupt (IM2): count frames, run the "music player"
; --------------------------------------------------------------------------------------------------
Isr:
        push af
        ld a,MAGENTA
        out (#FE),a
        push bc
        push hl
        ld hl,(FrameCount)
        inc hl
        ld (FrameCount),hl
        call Music
        ld a,(BorderNow)        ; the colour of whatever the interrupt came into
        out (#FE),a
        pop hl
        pop bc
        pop af
        ei
Isr.End:
        reti

; Stands in for a music player: its cost changes from frame to frame (16..47 loops)
Music:
        ld a,GREEN
        out (#FE),a
        ld a,(FrameCount)
        and 31
        add a,16
        ld b,a
.busy:
        nop
        djnz .busy
        ld a,MAGENTA            ; back to the interrupt's colour
        out (#FE),a
Music.End:
        ret

; --------------------------------------------------------------------------------------------------
; Set-up
; --------------------------------------------------------------------------------------------------
InitScreen:
        xor a
        out (#FE),a             ; black border
        ld hl,#4000             ; bitmap: #55 / #AA on alternate pixel rows, so INK and PAPER both show
.bitmap:
        ld a,h
        rra
        sbc a,a
        xor #55
        ld (hl),a
        inc hl
        ld a,h
        cp #58
        jr nz,.bitmap
.attrs:
        ld (hl),0               ; attributes: black until the first plasma frame
        inc hl
        ld a,h
        cp #5B
        jr nz,.attrs
        ret

InitTables:
        ld hl,IM2TABLE          ; IM2 vector table: every vector points at IM2JUMP
        ld b,0
.vectors:
        ld (hl),high IM2JUMP
        inc hl
        djnz .vectors
        ld (hl),high IM2JUMP
        ld a,#C3                ; JP Isr
        ld (IM2JUMP),a
        ld hl,Isr
        ld (IM2JUMP+1),hl
        ret

Phase1:     db 0
Phase2:     db 0
Phase3:     db 0
RowPhase:   db 0
RowsLeft:   db 0
SparkCount: db 0
Seed:       dw #ACE1
FrameCount: dw 0
BorderNow:  db 0

; --------------------------------------------------------------------------------------------------
; Tables, generated at assembly time
; --------------------------------------------------------------------------------------------------
        ORG SINTAB
        LUA ALLPASS
            for i = 0, 255 do
                _pc("db " .. math.floor(31.5 + 31.5 * math.sin(i * 2 * math.pi / 256)))
            end
        ENDLUA

        ORG PALETTE             ; a colour ramp: PAPER = this colour, INK = the next, BRIGHT on alternate steps
        LUA ALLPASS
            local ramp = { 1, 5, 4, 6, 2, 3, 7, 3, 2, 6, 4, 5 }
            for i = 0, 255 do
                local step = math.floor(i / 6) % #ramp
                local paper = ramp[step + 1]
                local ink = ramp[(step + 1) % #ramp + 1]
                local bright = (math.floor(i / 3) % 2) * 64
                _pc("db " .. (bright + paper * 8 + ink))
            end
        ENDLUA

        ORG MASKS
        db #80, #40, #20, #10, #08, #04, #02, #01

        SAVESNA "demo.sna", Start
