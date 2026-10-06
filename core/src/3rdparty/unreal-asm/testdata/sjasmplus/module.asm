; Modules and macros: sjasmplus-only constructs
        DEVICE ZXSPECTRUM128
        MODULE sound
init:   ld a,7
        ld bc,#fffd
        out (c),a
        ret
        ENDMODULE
        OUTPUT "sound.bin"
        ORG #c000
        call sound.init
        OUTEND
