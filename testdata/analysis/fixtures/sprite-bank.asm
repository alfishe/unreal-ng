; AnalysisDetectorGraphics, a sprite bank (Dizzy's): records of a 2-byte header (width x 2, height) and the rows,
; back to back, a zero header after the last. A sprite is found by walking the bank from its start (LD HL,bank: the
; sizes summed), then drawn row by row; the glyphs are records too, printed side by side from a message of record
; numbers (no printable text). Drawn: sprites 1, 2, 15 and 16 and the twelve glyphs; record 0 (before the first one
; drawn, named by the LD HL,bank) and records 17 and 18 (after the last) never are. A second bank holds the masked
; sprites: a header of height and a flag, then a mask byte before each graphic byte; records 0..8 of it drawn (screen
; AND mask OR graphic), record 9 never. Every masked record is two bytes wide: the header holds its height and a flag
; byte (Dizzy's hero), the draw routine knows the width.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld a,1
        ld de,$4000
        call draw
        ld a,2
        ld de,$4003
        call draw
        ld a,15
        ld de,$4006
        call draw
        ld a,16
        ld de,$400A
        call draw
        ld a,0
        ld de,$4800
masked:
        push af
        push de
        call mdraw
        pop de
        pop af
        inc e
        inc e
        inc e
        inc a
        cp 9
        jr nz,masked
        ld hl,message
        ld de,$5000
print:
        ld a,(hl)
        cp $FF
        jr z,main
        push hl
        push de
        add a,3
        call draw
        pop de
        pop hl
        inc hl
        inc e
        jr print
; A = the record's number, DE = the screen address
draw:
        ld hl,bank
find:
        or a
        jr z,found
        push af
        ld a,(hl)
        srl a
        ld b,a
        inc hl
        ld c,(hl)
        inc hl
        push de
        ld e,c
        ld d,0
size:
        add hl,de
        djnz size
        pop de
        pop af
        dec a
        jr find
found:
        ld a,(hl)
        srl a
        ld c,a
        inc hl
        ld b,(hl)
        inc hl
row:
        push bc
        push de
column:
        ld a,(hl)
        ld (de),a
        inc hl
        inc e
        dec c
        jr nz,column
        pop de
        inc d
        ld a,d
        and 7
        jr nz,next
        ld a,e
        add a,32
        ld e,a
        jr c,next
        ld a,d
        sub 8
        ld d,a
next:
        pop bc
        djnz row
        ret
; A = the masked record's number, DE = the screen address
mdraw:
        ld hl,heroes
mfind:
        or a
        jr z,mfound
        push af
        ld b,(hl)
        inc hl
        inc hl
        push de
        ld de,4
msize:
        add hl,de
        djnz msize
        pop de
        pop af
        dec a
        jr mfind
mfound:
        ld b,(hl)
        inc hl
        ld c,2
        inc hl
mrow:
        push bc
        push de
mcolumn:
        ld a,(de)
        and (hl)
        inc hl
        or (hl)
        inc hl
        ld (de),a
        inc e
        dec c
        jr nz,mcolumn
        pop de
        inc d
        ld a,d
        and 7
        jr nz,mnext
        ld a,e
        add a,32
        ld e,a
        jr c,mnext
        ld a,d
        sub 8
        ld d,a
mnext:
        pop bc
        djnz mrow
        ret
message: db 7,4,11,11,2,0,9,2,5,11,3,10,1,6,8,$FF
        org $9000
bank:
        db 4,16           ; record 0, never drawn
        db $25,$31,$3B,$47,$51,$5D,$67,$73,$7F,$8B,$91,$9D,$AB,$B7,$BD,$C9
        db $D1,$E5,$EF,$F3,$05,$09,$13,$27,$2B,$3F,$45,$49,$5F,$63,$69,$7D
hero:
        db 4,16           ; record 1
        db $4B,$55,$61,$6B,$77,$81,$8D,$97,$A1,$AF,$BB,$C1,$CD,$DB,$E7,$ED
        db $FF,$01,$15,$1F,$23,$35,$39,$43,$55,$5B,$6F,$75,$79,$8F,$93,$99
        db 4,16           ; record 2
        db $6F,$7B,$85,$91,$9B,$A7,$B1,$BD,$C5,$D1,$DF,$EB,$F1,$FD,$0B,$17
        db $1B,$2F,$31,$45,$4F,$53,$65,$69,$71,$85,$8B,$9F,$A5,$A9,$BF,$C3
glyphs:
        db 2,8             ; record 3, glyph 0
        db $00,$3C,$42,$42,$42,$42,$3C,$00
        db 2,8             ; record 4, glyph 1
        db $00,$3C,$46,$46,$46,$46,$3C,$00
        db 2,8             ; record 5, glyph 2
        db $00,$3C,$4A,$4A,$4A,$4A,$3C,$00
        db 2,8             ; record 6, glyph 3
        db $00,$3C,$4E,$4E,$4E,$4E,$3C,$00
        db 2,8             ; record 7, glyph 4
        db $00,$3C,$52,$52,$52,$52,$3C,$00
        db 2,8             ; record 8, glyph 5
        db $00,$3C,$56,$56,$56,$56,$3C,$00
        db 2,8             ; record 9, glyph 6
        db $00,$3C,$5A,$5A,$5A,$5A,$3C,$00
        db 2,8             ; record 10, glyph 7
        db $00,$3C,$5E,$5E,$5E,$5E,$3C,$00
        db 2,8             ; record 11, glyph 8
        db $00,$3C,$62,$62,$62,$62,$3C,$00
        db 2,8             ; record 12, glyph 9
        db $00,$3C,$66,$66,$66,$66,$3C,$00
        db 2,8             ; record 13, glyph 10
        db $00,$3C,$6A,$6A,$6A,$6A,$3C,$00
        db 2,8             ; record 14, glyph 11
        db $00,$3C,$6E,$6E,$6E,$6E,$3C,$00
big:
        db 6,24           ; record 15
        db $B9,$C5,$CF,$DB,$E5,$F1,$FB,$07,$13,$1F,$25,$31,$3F,$4B,$51,$5D
        db $6D,$71,$7B,$8F,$91,$A5,$AF,$B3,$C7,$CB,$D1,$E5,$EB,$FF,$05,$09
        db $11,$2D,$27,$33,$4D,$59,$53,$6F,$7B,$77,$8D,$99,$97,$A3,$B9,$B5
        db $C5,$D9,$D3,$E7,$F9,$0D,$07,$1B,$2F,$23,$39,$4D,$43,$57,$6D,$61
        db $69,$95,$9F,$8B,$B5,$A1,$AB,$D7
        db 6,24           ; record 16
        db $DF,$E9,$F5,$FF,$0B,$15,$21,$2B,$35,$43,$4F,$55,$61,$6F,$7B,$81
        db $8B,$9D,$A1,$AB,$BF,$C1,$D5,$DF,$E1,$F7,$FB,$01,$15,$1B,$2F,$35
        db $37,$41,$5D,$57,$63,$7D,$89,$83,$9D,$AB,$A7,$BD,$C9,$C7,$D3,$E9
        db $E3,$F5,$09,$03,$17,$29,$3D,$37,$49,$5F,$53,$69,$7D,$73,$87,$9D
        db $8F,$B9,$A5,$AF,$DB,$C5,$F1,$FB
boxes:
        db 8,12           ; record 17, never drawn
        db $03,$0F,$19,$25,$2F,$3B,$45,$51,$59,$65,$73,$7F,$85,$91,$9F,$AB
        db $B7,$BB,$CD,$D1,$DB,$EF,$F1,$05,$0D,$11,$27,$2B,$31,$45,$4B,$5F
        db $6B,$67,$71,$8D,$87,$93,$AD,$B9,$B1,$CD,$DB,$D7,$ED,$F9,$F7,$03
        db 8,12           ; record 18, never drawn
        db $29,$33,$3F,$49,$55,$5F,$6B,$75,$83,$89,$95,$A3,$AF,$B5,$C1,$CF
        db $DD,$E7,$EB,$FD,$01,$0B,$1F,$21,$37,$3D,$41,$57,$5B,$61,$75,$7B
        db $81,$9B,$97,$A1,$BD,$B7,$C3,$DD,$EB,$E1,$FD,$0B,$07,$1D,$29,$27
        db 0,0             ; the end of the bank
        org $A000
heroes:
        db 8,6            ; masked record 0
        db $E3,$1D,$03,$FC,$C9,$37,$41,$BE,$CF,$31,$CF,$30,$45,$BB,$2D,$D2
        db $EB,$15,$EB,$14,$01,$FF,$69,$96,$47,$B9,$27,$D8,$0D,$F3,$C5,$3A
        db 8,1            ; masked record 1
        db $E3,$1D,$C3,$3C,$49,$B7,$81,$7E,$AF,$51,$8F,$70,$C5,$3B,$6D,$92
        db $CB,$35,$8B,$74,$81,$7F,$89,$76,$27,$D9,$E7,$18,$6D,$93,$05,$FA
        db 8,2            ; masked record 2
        db $C3,$3D,$63,$9C,$A9,$57,$A1,$5E,$AF,$51,$2F,$D0,$25,$DB,$8D,$72
        db $CB,$35,$4B,$B4,$E1,$1F,$C9,$36,$27,$D9,$87,$78,$ED,$13,$25,$DA
        db 8,6            ; masked record 3
        db $C3,$3D,$23,$DC,$29,$D7,$E1,$1E,$8F,$71,$EF,$10,$A5,$5B,$CD,$32
        db $AB,$55,$EB,$14,$61,$9F,$E9,$16,$07,$F9,$47,$B8,$4D,$B3,$65,$9A
        db 8,3            ; masked record 4
        db $A3,$5D,$C3,$3C,$89,$77,$01,$FE,$8F,$71,$8F,$70,$05,$FB,$ED,$12
        db $AB,$55,$AB,$54,$C1,$3F,$29,$D6,$07,$F9,$E7,$18,$CD,$33,$85,$7A
        db 8,0            ; masked record 5
        db $A3,$5D,$83,$7C,$09,$F7,$41,$BE,$6F,$91,$4F,$B0,$85,$7B,$2D,$D2
        db $8B,$75,$4B,$B4,$41,$BF,$49,$B6,$E7,$19,$A7,$58,$2D,$D3,$C5,$3A
        db 8,6            ; masked record 6
        db $83,$7D,$23,$DC,$69,$97,$61,$9E,$6F,$91,$EF,$10,$E5,$1B,$4D,$B2
        db $8B,$75,$0B,$F4,$A1,$5F,$89,$76,$E7,$19,$47,$B8,$AD,$53,$E5,$1A
        db 8,5            ; masked record 7
        db $83,$7D,$E3,$1C,$E9,$17,$A1,$5E,$4F,$B1,$AF,$50,$65,$9B,$8D,$72
        db $6B,$95,$AB,$54,$21,$DF,$A9,$56,$C7,$39,$07,$F8,$0D,$F3,$25,$DA
        db 8,1            ; masked record 8
        db $63,$9D,$83,$7C,$49,$B7,$C1,$3E,$4F,$B1,$4F,$B0,$C5,$3B,$AD,$52
        db $6B,$95,$6B,$94,$81,$7F,$E9,$16,$C7,$39,$A7,$58,$8D,$73,$45,$BA
        db 8,4            ; masked record 9, never drawn
        db $63,$9D,$43,$BC,$C9,$37,$01,$FE,$2F,$D1,$0F,$F0,$45,$BB,$ED,$12
        db $4B,$B5,$0B,$F4,$01,$FF,$09,$F6,$A7,$59,$67,$98,$ED,$13,$85,$7A
        db 0,0             ; the end of the masked bank
