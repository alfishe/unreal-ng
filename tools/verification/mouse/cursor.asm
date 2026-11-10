; Kempston mouse cursor demo: reads #FBDF (X) and #FFDF (Y) once a frame, as a mouse driver does (the difference of the
; 8-bit counters is the travel, added to a position kept in the program), and draws a cross with XOR plotting.
; Buttons (#FADF, active low): left = red border, right = blue border, both = magenta.
; Build: sjasmplus cursor.asm   ->  cursor.sna (48K snapshot, starts at #8000)

        DEVICE ZXSPECTRUM48
        ORG #8000

start:  di
        ld sp,#FF00
        im 1
        xor a
        out (#FE),a
        ld hl,#4000
        ld de,#4001
        ld bc,#17FF
        ld (hl),0
        ldir
        ld hl,#5800
        ld de,#5801
        ld bc,#02FF
        ld (hl),#07
        ldir
        ld hl,128
        ld (px),hl
        ld hl,96
        ld (py),hl
        ld bc,#FBDF
        in a,(c)
        ld (oldx),a
        ld bc,#FFDF
        in a,(c)
        ld (oldy),a
        call draw
        ei

loop:   halt
        call draw                       ; erase (XOR the same pixels again)

        ld bc,#FBDF                     ; X counter: grows to the right
        in a,(c)
        ld hl,oldx
        ld e,(hl)
        ld (hl),a
        sub e                           ; signed travel since the last frame
        ld e,a
        add a,a
        sbc a,a
        ld d,a                          ; DE = sign-extended travel
        ld hl,(px)
        add hl,de
        ld c,254                        ; keeps 2..253: the cross never leaves the screen
        call clamp
        ld (px),hl

        ld bc,#FFDF                     ; Y counter: grows upward
        in a,(c)
        ld hl,oldy
        ld e,(hl)
        ld (hl),a
        sub e
        ld e,a
        add a,a
        sbc a,a
        ld d,a
        ld hl,(py)
        or a
        sbc hl,de                       ; up is + for the mouse, y grows downward on the screen
        ld c,190                        ; keeps 2..189
        call clamp
        ld (py),hl

        ld bc,#FADF                     ; buttons, active low
        in a,(c)
        ld e,0
        bit 0,a
        jr nz,nolft
        set 1,e                         ; left: red
nolft:  bit 1,a
        jr nz,norgt
        set 0,e                         ; right: blue
norgt:  ld a,e
        out (#FE),a

        call draw
        jr loop

; HL clamped to 2..C-1 (C = limit + 1); a negative HL gives 2
clamp:  bit 7,h
        jr z,c1
        ld hl,2
        ret
c1:     ld a,h
        or a
        jr nz,c3
        ld a,l
        cp 2
        jr nc,c2
        ld hl,2
        ret
c2:     cp c
        ret c
c3:     ld a,c
        dec a
        ld l,a
        ld h,0
        ret

; XOR the cross (9 pixels) around (px, py)
draw:   ld hl,cross
        ld a,9
dloop:  push af
        ld a,(px)
        add a,(hl)
        ld c,a
        inc hl
        ld a,(py)
        add a,(hl)
        ld b,a
        inc hl
        push hl
        call plot
        pop hl
        pop af
        dec a
        jr nz,dloop
        ret

; XOR one pixel: C = x, B = y (0..191)
plot:   ld a,b
        and #07
        or #40
        ld h,a
        ld a,b
        rra
        rra
        rra
        and #18
        or h
        ld h,a
        ld a,b
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
        ld b,a
        ld a,#80
        jr z,pm
pl:     rrca
        djnz pl
pm:     xor (hl)
        ld (hl),a
        ret

cross:  DB 0,0, 1,0, 2,0, #FF,0, #FE,0, 0,1, 0,2, 0,#FF, 0,#FE
px:     DW 128
py:     DW 96
oldx:   DB 0
oldy:   DB 0

        SAVESNA "cursor.sna", start
