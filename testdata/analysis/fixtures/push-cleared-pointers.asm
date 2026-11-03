; L1: a buffer cleared with the PUSH trick (LD SP,buf_end / PUSH HL), then filled with pointers the code reads back
; through IX and uses as addresses. A PUSH wrote its bytes and nothing popped them: its words are pointers in memory,
; not stack slots (positive).
        org $8000
start:
        di
main:
        ld (saved),sp
        ld sp,buf_end
        ld hl,0
        push hl
        push hl
        ld sp,(saved)
        ld hl,target
        ld (buf),hl
        ld hl,target+1
        ld (buf+2),hl
        ld ix,buf
        ld l,(ix+0)
        ld h,(ix+1)
        ld a,(hl)
        ld l,(ix+2)
        ld h,(ix+3)
        ld a,(hl)
        jr main
saved:  dw 0
buf:    ds 4
buf_end:
target: db 1,2
