;LD HL,VTII_MODULE; JR ... - engine init, HL = module address
;for THIS instance. Standard VTII contract: C000=init, C005=play.
VTII_INIT: equ 0xc000
;JP VTII_PLAY_IMPL - per-interrupt play entry. WildPlayer calls
;this at 0xC005 through the paged bank (see INT_HANDLER).
VTII_PLAY: equ 0xc005
R_C022: equ 0xc022
M_C120: equ 0xc120
R_C171: equ 0xc171
M_C174: equ 0xc174
R_C176: equ 0xc176
M_C1A1: equ 0xc1a1
M_C1A8: equ 0xc1a8
R_C1E5: equ 0xc1e5
M_C271: equ 0xc271
M_C28D: equ 0xc28d
M_C293: equ 0xc293
R_C2BA: equ 0xc2ba
R_C2FB: equ 0xc2fb
R_C313: equ 0xc313
R_C32E: equ 0xc32e
R_C32F: equ 0xc32f
M_C337: equ 0xc337
M_C33E: equ 0xc33e
R_C368: equ 0xc368
D_C471: equ 0xc471
D_C472: equ 0xc472
;play frame implementation
VTII_PLAY_IMPL: equ 0xc4b9
M_C4D1: equ 0xc4d1
M_C4E7: equ 0xc4e7
M_C4F4: equ 0xc4f4
M_C527: equ 0xc527
M_C53B: equ 0xc53b
M_C545: equ 0xc545
D_C585: equ 0xc585
M_C5A1: equ 0xc5a1
M_C5A4: equ 0xc5a4
R_C5AB: equ 0xc5ab
;register-select OUT loop - pc C5B4/C5B7 in the port trace is
;exactly here (13 register writes per frame = one VTII frame).
VTII_REG_OUT_SELECT: equ 0xc5b4
;paired data OUT (OUT (#BFFD-mirror),A)
VTII_REG_OUT_DATA: equ 0xc5b7
D_C5C8: equ 0xc5c8
D_C618: equ 0xc618
D_C61C: equ 0xc61c
D_C651: equ 0xc651
D_C652: equ 0xc652
D_C65D: equ 0xc65d
M_C65E: equ 0xc65e
M_C660: equ 0xc660
D_C66C: equ 0xc66c
D_C66E: equ 0xc66e
D_C67A: equ 0xc67a
M_C67B: equ 0xc67b
M_C67D: equ 0xc67d
M_C689: equ 0xc689
D_C68B: equ 0xc68b
D_C697: equ 0xc697
M_C698: equ 0xc698
M_C69A: equ 0xc69a
M_C6A6: equ 0xc6a6
D_C6A8: equ 0xc6a8
M_C6A9: equ 0xc6a9
M_C6AB: equ 0xc6ab
M_C6AC: equ 0xc6ac
M_C6AD: equ 0xc6ad
D_C6AE: equ 0xc6ae
M_C6B0: equ 0xc6b0
M_C6B2: equ 0xc6b2
M_C6B4: equ 0xc6b4
D_C6B5: equ 0xc6b5
M_C6B6: equ 0xc6b6
M_C6B7: equ 0xc6b7
M_C6B8: equ 0xc6b8
M_C6B9: equ 0xc6b9
M_C6BB: equ 0xc6bb
M_C6BC: equ 0xc6bc
D_C6BE: equ 0xc6be
D_C71F: equ 0xc71f
D_C781: equ 0xc781
D_C785: equ 0xc785
D_C7AE: equ 0xc7ae
M_C7DC: equ 0xc7dc
;PT3 module staged for this engine ('Bad Apple!!' PT3).
;The player points each engine instance's INIT HL at its module.
VTII_MODULE: equ 0xc86e
