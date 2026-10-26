; FinishBufferFields page split: a 512-byte buffer at $9000 copied once from `source`; variables at +3 and +5, and
; the second page read as LD H,$91 / LD L,A / LD A,(HL) (a page named by its high byte): the unclaimed span after
; +5 ends at +256, the page is a field of its own. Negative: the first page is not named, no split inside it.
        org $8000
start:
        ld sp,$9F00
        di
        ld hl,source
        ld de,$9000
        ld bc,512
        ldir
main:
        ld a,($9003)
        inc a
        ld ($9003),a
        ld l,a
        ld h,$91
        ld a,(hl)
        ld ($9005),a
        jr main
source: ds 512,$5A
