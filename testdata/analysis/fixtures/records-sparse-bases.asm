; AnalysisDetectorRecords_SparseBasesAreNoArray: one routine reads two bytes through IX at four bases, three of them 2
; bytes apart and the fourth far above them. The bases share a step of 2, but four bases make no array of nearly two
; thousand 2-byte records over everything between them (negative: no record array between the bases).
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld ix,recs
        call touch
        ld ix,recs+2
        call touch
        ld ix,recs+4
        call touch
        ld ix,distant
        call touch
        jr main
touch:
        ld a,(ix+0)
        add a,(ix+1)
        ld (sink),a
        ret
sink:   db 0
        org $A000
recs:   db 1,2,3,4,5,6
between:
        ds 16,$55
        org $AF00
distant:
        db 7,8
