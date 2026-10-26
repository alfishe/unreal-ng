; CfgBasicBlocks: a loop with an if / else inside. Every label named blk_* starts a basic block, and no other
; address does (AnalysisL1_Test checks the set).
        org $8000
start:
blk_start:
        ld sp,$9F00
        di
blk_main:
        ld b,4
blk_loop:
        ld a,b
        and 1
        jr z,blk_even
blk_odd:
        inc c
        jr blk_next
blk_even:
        inc d
blk_next:
        djnz blk_loop
blk_again:
        jr blk_main
