        MAIN "*",#C6
        DISPLAY "до 1-го выравнивания адреса: ",$
;l=254 для высоты 1 (сразу выход)
        DS .(256-(2*trimaxh)-$)
TRISTACK12
       DUP trimaxh-1
        DW TRILINE
       EDUP
        DW TRIDRAW23
        DISPLAY /T,TRISTACK12

;l=254 для высоты 1 (сразу выход)
        DS .(256-(2*trimaxh)-$)
TRISTACK23
       DUP trimaxh-1
        DW TRILINE
       EDUP
        DW TRIQ
        DISPLAY /T,TRISTACK23
        DISPLAY $
        DS .(-$)
VUjump
;младший байт адреса: %(V3)(U3)(V2)(U2)(V1)(U1)00
        DS 3;jp VU000000 ;невозможно
        NOP
        DS 3;jp VU000001 ;невозм
        NOP
        DS 3;jp VU000010 ;невозм
        NOP
        DS 3;jp VU000011 ;невозм
        NOP
        DS 3;jp VU000100 ;невозм
        NOP
        DS 3;jp VU000101 ;невозм
        NOP
   JP P,VU000110
        NOP
   JP P,VU000111
        NOP
        DS 3;jp VU001000 ;невозм
        NOP
     JP VU001001
        NOP
        DS 3;jp VU001010 ;невозм
        NOP
   JP P,VU001011
        NOP
        DS 3;jp VU001100 ;невозм
        NOP
     JP VU001101
        NOP
     JP VU001110
        NOP
        DS 3;jp VU001111 ;невозм
        NOP
        DS 3;jp VU010000 ;невозм
        NOP
        DS 3;jp VU010001 ;невозм
        NOP
     JP VU010010
        NOP
     JP VU010011
        NOP
        DS 3;jp VU010100 ;невозм
        NOP
        DS 3;jp VU010101 ;невозм
        NOP
        DS 3;jp VU010110 ;невозм
        NOP
        DS 3;jp VU010111 ;невозм
        NOP
   JP P,VU011000
        NOP
        DS 3;jp VU011001 ;невозм
        NOP
        DS 3;jp VU011010 ;невозм
        NOP
     JP VU011011
        NOP
   JP P,VU011100
        NOP
        DS 3;jp VU011101 ;невозм
        NOP
   JP P,VU011110
        NOP
        DS 3;jp VU011111 ;невозможно
        NOP
        DS 3;jp VU100000 ;невозможно
        NOP
   JP P,VU100001
        NOP
        DS 3;jp VU100010 ;невозможно
        NOP
     JP VU100011
        NOP
     JP VU100100
        NOP
        DS 3;jp VU100101 ;невозможно
        NOP
        DS 3;jp VU100110 ;невозможно
        NOP
   JP P,VU100111
        NOP
        DS 3;jp VU101000 ;невозможно
        NOP
        DS 3;jp VU101001 ;невозможно
        NOP
        DS 3;jp VU101010 ;невозможно
        NOP
        DS 3;jp VU101011 ;невозможно
        NOP
   JP P,VU101100
        NOP
     JP VU101101
        NOP
        DS 3;jp VU101110 ;невозможно
        NOP
        DS 3;jp VU101111 ;невозможно
        NOP
        DS 3;jp VU110000 ;невозможно
        NOP
   JP P,VU110001
        NOP
   JP P,VU110010
        NOP
        DS 3;jp VU110011 ;невозможно
        NOP
     JP VU110100
        NOP
        DS 3;jp VU110101 ;невозможно
        NOP
     JP VU110110
        NOP
        DS 3;jp VU110111 ;невозможно
        NOP
     JP VU111000
        NOP
   JP P,VU111001
       IFN 0
        NOP
        DS 3;jp VU111010 ;невозможно
        NOP
        DS 3;jp VU111011 ;невозможно
        NOP
        DS 3;jp VU111100 ;невозможно
        NOP
        DS 3;jp VU111101 ;невозможно
        NOP
        DS 3;jp VU111110 ;невозможно
        NOP
        DS 3;jp VU111111 ;невозможно
        NOP
       ENDIF


