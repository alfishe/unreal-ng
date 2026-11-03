; RecordTemplates behind a busy blitter: before the game loop one copier moves 71 680 distinct one-byte runs (more
; than L1 keeps in all), and the template copier comes after it. The copy list keeps room for every copier, so the
; template copies still reach L1. The rest is record-templates.asm.
; RecordTemplates: an array of 8 records of 16 bytes walked through IX (+0..+8 read every pass); every pass one record
; is refilled from one of 3 templates of 9 bytes (LDIR, the record's first 9 bytes). The templates are a table of
; 9-byte records; the bytes after them are other data.
        org $8000
start:
        ld sp,$7F00
        di
        ld hl,280               ; 280 sources x 256 destinations 2 apart: 71 680 distinct runs
        ld (rows),hl
blitrow:
        ld de,(rows)
        ld hl,blitsrc
        add hl,de               ; the source: blitsrc + row
        ld de,blitdst
        exx
        ld b,0                  ; the inner count in B' (LDI takes BC)
        exx
blitone:
        ldi
        dec hl
        inc de
        exx
        dec b
        exx
        jr nz,blitone
        ld hl,(rows)
        dec hl
        ld (rows),hl
        ld a,h
        or l
        jr nz,blitrow
main:
        ld a,(phase)
        inc a
        ld (phase),a
        ld b,a                  ; the record: phase mod 8
        and 7
        add a,a
        add a,a
        add a,a
        add a,a
        ld e,a
        ld d,0
        ld hl,records
        add hl,de
        ex de,hl
        ld a,b                  ; the template: phase mod 3
mod3:   cp 3
        jr c,pick
        sub 3
        jr mod3
pick:   ld l,a
        add a,a
        add a,a
        add a,a
        add a,l                 ; 9 x template
        ld l,a
        ld h,0
        ld bc,templates
        add hl,bc
        ld bc,9
        ldir
        ld ix,records           ; the walk
        ld b,8
walk:   ld a,(ix+0)
        add a,(ix+1)
        add a,(ix+2)
        add a,(ix+3)
        add a,(ix+4)
        add a,(ix+5)
        add a,(ix+6)
        add a,(ix+7)
        add a,(ix+8)
        ld (sink),a
        ld de,16
        add ix,de
        djnz walk
        jr main
phase:  db 0
rows:   dw 0
sink:   db 0
        org $9000
templates:
        db 1,2,3,4,5,6,7,8,9
        db 11,12,13,14,15,16,17,18,19
        db 21,22,23,24,25,26,27,28,29
after:  db $AA,$BB,$CC,$DD
        org $9100
records:
        ds 8*16
        org $A000
blitsrc:
        ds 512,$55
        org $B000
blitdst:
        ds 520
