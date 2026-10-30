; PushRetDispatch: the target is pushed, then RET jumps to it (no CALL made the stack slot), alternating t0 / t1.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld a,(select)
        xor 1
        ld (select),a
        ld hl,t0
        or a
        jr z,go
        ld hl,t1
go:     push hl
        ret
t0:     inc b
        jr main
t1:     inc c
        jr main
select: db 0
