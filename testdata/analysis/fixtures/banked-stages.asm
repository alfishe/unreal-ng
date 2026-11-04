; CodeArrival (128K): one copier (LDIR) writes the same stage to $C000 in RAM 1, then in RAM 3, and calls it in each.
; Two arrivals at one Z80 address in two pages: both kept (an arrival is told by its physical first byte).
        org $8000
start:
        ld sp,$9F00
        di
        ld a,$11                ; ROM 1, RAM 1 at $C000
        call copy_run
        ld a,$13                ; ROM 1, RAM 3 at $C000
        call copy_run
idle:
        jr idle
copy_run:
        ld bc,$7FFD
        out (c),a
        ld hl,stage
        ld de,$C000
        ld bc,stage_end-stage
copy:
        ldir
        call $C000
        ret
stage:
        inc d
        inc e
        ret
stage_end:
