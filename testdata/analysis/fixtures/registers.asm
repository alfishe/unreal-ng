; RegisterLiveInOut: add_ab reads A and B and returns A, which main stores; keep_bc writes B and C but pushes and
; pops BC, so it leaves them as they were.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld a,5
        ld b,3
        call add_ab
        ld (result),a
        call keep_bc
        jr main
add_ab:
        add a,b
        ret
keep_bc:
        push bc
        ld b,9
        ld c,b
        pop bc
        ret
result: db 0
