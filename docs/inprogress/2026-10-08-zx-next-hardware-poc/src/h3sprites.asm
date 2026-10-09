; H3: how many sprites on a line before the hardware reports "too many sprites on a line".
; Sprites are switched on one at a time (sprite 0, then 0-1, ...). After each step the program waits one whole
; frame (to clear the flags), then another frame, and reads port #303B: bit 1 = "max sprites per line exceeded",
; bit 0 = collision. The first count N at which bit 1 is set is printed (#FF = never up to 128).
; Variants (the question: is the limit a COUNT of sprites or a TIME budget?):
;   0 = 8-bit patterns, one size, all on the same line
;   1 = 4-bit patterns
;   2 = 8-bit, X scale 2     3 = 8-bit, X scale 8     4 = 8-bit, Y scale 8 (same pixels per line as 0)
;   5 = 8-bit, sprites spread over different lines (12 rows): a per-sprite cost that does not depend on the line
;
; Screen: row 0 = id, row 1 = core version / board / machine type, rows 4-9 = variants 0..5,
;         column 2 = first N with bit 1 (hex byte), column 6 = collision bit seen (00/01).
    org 0x8000
Start:
    di
    ld sp,0xBFF0
    call InitScreen
    ld a,0x03
    call PrintHeader
    call MeasureMaxLine

    ld a,0x15
    call ReadNextReg
    or 0x01                     ; sprites enabled (bit 0)
    nextreg 0x15,a

    call LoadPattern

    ld b,0                      ; variant
VariantLoop:
    push bc
    ld a,b
    ld (Variant),a
    call HideAll
    ld a,0xFF
    ld (FoundN),a
    xor a
    ld (SeenColl),a
    ld c,1                      ; N = number of visible sprites
NLoop:
    push bc
    ld a,c
    dec a                       ; sprite index to switch on = N - 1
    call SetSprite
    pop bc
    push bc
    call WaitFrameStart         ; end of the frame in which it appeared
    ld bc,0x303B
    in a,(c)                    ; clears the flags
    call WaitFrameStart         ; one whole frame with N sprites
    ld bc,0x303B
    in a,(c)                    ; flags of that frame
    pop bc
    ld d,a
    and 1
    jr z,NoColl
    ld a,1
    ld (SeenColl),a
NoColl:
    ld a,d
    and 2
    jr z,NextN
    ld a,c
    ld (FoundN),a
    jr Found
NextN:
    inc c
    ld a,c
    cp 129
    jr c,NLoop
Found:
    pop bc
    push bc
    ld a,b
    add a,4
    ld b,a
    ld a,(FoundN)
    ld c,2
    call PrintAt8
    pop bc
    push bc
    ld a,b
    add a,4
    ld b,a
    ld a,(SeenColl)
    ld c,6
    call PrintAt8
    pop bc
    inc b
    ld a,b
    cp 6
    jp c,VariantLoop
    jp Done

; ---------------------------------------------------------------------------
; LoadPattern: pattern 0 = 256 bytes of #11 (a visible colour in 8-bit and in 4-bit mode)
LoadPattern:
    ld bc,0x303B
    xor a
    out (c),a                   ; sprite 0, pattern 0
    ld b,0
    ld c,0x5B
    ld a,0x11
LPloop:
    out (c),a
    djnz LPloop
    ret

; HideAll: all 128 sprites invisible
HideAll:
    ld bc,0x303B
    xor a
    out (c),a
    ld d,128
HAloop:
    xor a
    out (0x57),a
    out (0x57),a
    out (0x57),a
    out (0x57),a
    dec d
    jr nz,HAloop
    ret

; SetSprite: A = sprite index; visible, position and flags from (Variant)
SetSprite:
    ld (SpriteIdx),a
    ld bc,0x303B
    out (c),a                   ; select the sprite (also selects pattern = index; harmless, the pattern RAM is not rewritten)
    ; X = 40 + (5 * index) mod 200
    ld a,(SpriteIdx)
    ld e,a
    add a,a
    add a,a
    add a,e                     ; 5 * index (mod 256)
SSmod:
    cp 200
    jr c,SSxok
    sub 200
    jr SSmod
SSxok:
    add a,40
    out (0x57),a                ; attribute 0: X low
    ld a,(Variant)
    cp 5
    jr z,SSspread
    ld a,32+80
    jr SSy
SSspread:
    ld a,(SpriteIdx)
    and 0x0F
    cp 12
    jr c,SSrow
    sub 12
SSrow:
    add a,a
    add a,a
    add a,a
    add a,a                     ; row * 16
    add a,32+8
SSy:
    out (0x57),a                ; attribute 1: Y low
    xor a
    out (0x57),a                ; attribute 2: no palette offset, no mirror, X MSB 0
    ld a,(Variant)
    or a
    jr z,SSplain
    cp 5
    jr z,SSplain
    ld a,0xC0                   ; attribute 3: visible, attribute 4 follows, pattern 0
    out (0x57),a
    ld a,(Variant)
    cp 1
    ld a,0x80                   ; 4-bit
    jr z,SSa4
    ld a,(Variant)
    cp 2
    ld a,0x08                   ; X scale 2
    jr z,SSa4
    ld a,(Variant)
    cp 3
    ld a,0x18                   ; X scale 8
    jr z,SSa4
    ld a,0x06                   ; variant 4: Y scale 8
SSa4:
    out (0x57),a
    ret
SSplain:
    ld a,0x80                   ; attribute 3: visible, no attribute 4, pattern 0
    out (0x57),a
    ret

Variant:    defb 0
FoundN:     defb 0xFF
SeenColl:   defb 0
SpriteIdx:  defb 0

    include "common.inc"
