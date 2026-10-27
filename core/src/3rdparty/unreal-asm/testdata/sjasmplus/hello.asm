; A small sjasmplus program: prints a message through the ROM and saves a snapshot
        DEVICE ZXSPECTRUM48
        ORG #8000
start:  ld hl,message
.loop:  ld a,(hl)
        or a
        ret z
        rst #10
        inc hl
        jr .loop
message:
        db "Hello from unreal-asm!",13,0
        SAVESNA "hello.sna",start
