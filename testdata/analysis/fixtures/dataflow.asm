; DataFlowAddressImmediate / CompareConstant / PageTable / BranchConditionVariable
        org $8000
start:
        ld sp,$9F00
        di
main:
imm:    ld hl,$C000          ; an immediate later used as an address
use:    ld a,(hl)
load:   ld a,($C010)         ; a value loaded from memory ...
cmp:    cp $20               ; ... compared with a constant
        jr z,skip
        inc c
skip:
        ld a,c
        and 7
        ld l,a
page:   ld h,$C1             ; a page table: H the table, L the index
read:   ld a,(hl)
        ld a,(flag)          ; a flag that alternates
        xor 1
        ld (flag),a
test:   or a
branch: jr nz,main
        jr main
flag:   db 0
