; Labels of every kind for the symbol codecs' golden files (sjasmplus 1.24)
        DEVICE ZXSPECTRUM128
SCREEN  EQU #4000
LINES   EQU 24
        ORG #8000
start:  ld hl,SCREEN
.loop:  djnz .loop
        call player.play
        ret
data_tab: db 1,2,3
q?mark: dw start
        MODULE player
play:   ld a,LINES
.frame: nop
        ret
        ENDMODULE
        SLOT 3
        PAGE 1
        ORG #C000
paged1: nop
.inner: ret
        PAGE 3
        ORG #C000
paged3: nop
        SAVEBIN "labels.bin",#8000,32
        CSPECTMAP "labels.cspect.map"
        LABELSLIST "labels.unreal.l"
