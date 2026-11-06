; ReplayDeterministic: many loops and routines, so that an L1 serialized in hash order (any unordered container
; walked without a sort) differs between two salts. 24 routines, each a counted loop over its own buffer.
        org $8000
start:
        ld sp,$9F00
        di
main:
        call r0
        call r1
        call r2
        call r3
        call r4
        call r5
        call r6
        call r7
        call r8
        call r9
        call r10
        call r11
        call r12
        call r13
        call r14
        call r15
        call r16
        call r17
        call r18
        call r19
        call r20
        call r21
        call r22
        call r23
        jr main
r0:
        ld hl,$A000
        ld b,3
r0_loop:
        ld (hl),b
        inc hl
        djnz r0_loop
        ret
r1:
        ld hl,$A040
        ld b,4
r1_loop:
        ld (hl),b
        inc hl
        djnz r1_loop
        ret
r2:
        ld hl,$A080
        ld b,5
r2_loop:
        ld (hl),b
        inc hl
        djnz r2_loop
        ret
r3:
        ld hl,$A0C0
        ld b,6
r3_loop:
        ld (hl),b
        inc hl
        djnz r3_loop
        ret
r4:
        ld hl,$A100
        ld b,7
r4_loop:
        ld (hl),b
        inc hl
        djnz r4_loop
        ret
r5:
        ld hl,$A140
        ld b,8
r5_loop:
        ld (hl),b
        inc hl
        djnz r5_loop
        ret
r6:
        ld hl,$A180
        ld b,9
r6_loop:
        ld (hl),b
        inc hl
        djnz r6_loop
        ret
r7:
        ld hl,$A1C0
        ld b,10
r7_loop:
        ld (hl),b
        inc hl
        djnz r7_loop
        ret
r8:
        ld hl,$A200
        ld b,11
r8_loop:
        ld (hl),b
        inc hl
        djnz r8_loop
        ret
r9:
        ld hl,$A240
        ld b,12
r9_loop:
        ld (hl),b
        inc hl
        djnz r9_loop
        ret
r10:
        ld hl,$A280
        ld b,13
r10_loop:
        ld (hl),b
        inc hl
        djnz r10_loop
        ret
r11:
        ld hl,$A2C0
        ld b,14
r11_loop:
        ld (hl),b
        inc hl
        djnz r11_loop
        ret
r12:
        ld hl,$A300
        ld b,15
r12_loop:
        ld (hl),b
        inc hl
        djnz r12_loop
        ret
r13:
        ld hl,$A340
        ld b,16
r13_loop:
        ld (hl),b
        inc hl
        djnz r13_loop
        ret
r14:
        ld hl,$A380
        ld b,17
r14_loop:
        ld (hl),b
        inc hl
        djnz r14_loop
        ret
r15:
        ld hl,$A3C0
        ld b,18
r15_loop:
        ld (hl),b
        inc hl
        djnz r15_loop
        ret
r16:
        ld hl,$A400
        ld b,19
r16_loop:
        ld (hl),b
        inc hl
        djnz r16_loop
        ret
r17:
        ld hl,$A440
        ld b,20
r17_loop:
        ld (hl),b
        inc hl
        djnz r17_loop
        ret
r18:
        ld hl,$A480
        ld b,21
r18_loop:
        ld (hl),b
        inc hl
        djnz r18_loop
        ret
r19:
        ld hl,$A4C0
        ld b,22
r19_loop:
        ld (hl),b
        inc hl
        djnz r19_loop
        ret
r20:
        ld hl,$A500
        ld b,23
r20_loop:
        ld (hl),b
        inc hl
        djnz r20_loop
        ret
r21:
        ld hl,$A540
        ld b,24
r21_loop:
        ld (hl),b
        inc hl
        djnz r21_loop
        ret
r22:
        ld hl,$A580
        ld b,25
r22_loop:
        ld (hl),b
        inc hl
        djnz r22_loop
        ret
r23:
        ld hl,$A5C0
        ld b,26
r23_loop:
        ld (hl),b
        inc hl
        djnz r23_loop
        ret
