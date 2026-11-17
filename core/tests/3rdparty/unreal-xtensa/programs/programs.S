// Test programs for unreal-xtensa (ESP32-S3, windowed ABI). Assembled by genprograms.py with --no-transform, so every
// mnemonic is encoded exactly as written (no narrowing, no MOVI -> L32R relaxation). Each test function is called by
// the host with BeginCall (CALL4), arguments in a2.., result in a2. Functions that store results take the output
// buffer in a2 and write consecutive words through a15; the expected values are in xtensacpu_test.cpp in the same
// order. Trap addresses (TRAP_*) are defined by the linker command line.

    .text
    .literal_position

    .macro ST reg
    s32i    \reg, a15, 0
    addi    a15, a15, 4
    .endm

    .macro LI reg, val
    .literal .Llit\@, \val
    l32r    \reg, .Llit\@
    .endm

    // Stores 1 if the branch is taken, else 0.
    .macro BR insn, ops:vararg
    movi    a5, 1
    \insn   \ops, 1f
    movi    a5, 0
1:  ST      a5
    .endm

    .macro FLI freg, val
    LI      a5, \val
    wfr     \freg, a5
    .endm

    .macro FST freg
    rfr     a5, \freg
    ST      a5
    .endm

// ---------------------------------------------------------------------------------------------------------------------
// alu(buf): integer ALU, shifts, booleans, loads and stores
    .literal_position
    .global alu
    .type alu, @function
    .align 4
alu:
    entry   a1, 32
    mov     a15, a2
    LI      a3, 0x87654321
    LI      a4, 0x0000F00D
    movi    a6, -16
    movi    a7, 5
    movi    a8, 0
    add     a5, a3, a4
    ST      a5                  // 0
    add.n   a5, a3, a4
    ST      a5
    addx2   a5, a3, a4
    ST      a5
    addx4   a5, a3, a4
    ST      a5
    addx8   a5, a3, a4
    ST      a5
    sub     a5, a3, a4
    ST      a5                  // 5
    subx2   a5, a3, a4
    ST      a5
    subx4   a5, a3, a4
    ST      a5
    subx8   a5, a3, a4
    ST      a5
    and     a5, a3, a4
    ST      a5
    or      a5, a3, a4
    ST      a5                  // 10
    xor     a5, a3, a4
    ST      a5
    neg     a5, a3
    ST      a5
    abs     a5, a3
    ST      a5
    abs     a5, a4
    ST      a5
    addi    a5, a3, -128
    ST      a5                  // 15
    addi.n  a5, a3, -1
    ST      a5
    addi.n  a5, a3, 15
    ST      a5
    addmi   a5, a3, -32768
    ST      a5
    addmi   a5, a3, 32512
    ST      a5
    movi    a5, -2048
    ST      a5                  // 20
    movi    a5, 2047
    ST      a5
    movi.n  a5, -32
    ST      a5
    movi.n  a5, 95
    ST      a5
    mov.n   a5, a4
    ST      a5
    salt    a5, a3, a4
    ST      a5                  // 25
    saltu   a5, a3, a4
    ST      a5
    min     a5, a3, a4
    ST      a5
    max     a5, a3, a4
    ST      a5
    minu    a5, a3, a4
    ST      a5
    maxu    a5, a3, a4
    ST      a5                  // 30
    mov.n   a5, a7
    moveqz  a5, a3, a6
    ST      a5
    mov.n   a5, a7
    movnez  a5, a3, a6
    ST      a5
    mov.n   a5, a7
    movltz  a5, a3, a6
    ST      a5
    mov.n   a5, a7
    movgez  a5, a3, a6
    ST      a5
    sext    a5, a3, 7
    ST      a5                  // 35
    sext    a5, a3, 15
    ST      a5
    sext    a5, a4, 15
    ST      a5
    sext    a5, a4, 7
    ST      a5
    clamps  a5, a3, 7
    ST      a5
    clamps  a5, a4, 7
    ST      a5                  // 40
    clamps  a5, a7, 7
    ST      a5
    nsa     a5, a3
    ST      a5
    nsa     a5, a4
    ST      a5
    nsa     a5, a8
    ST      a5
    nsau    a5, a4
    ST      a5                  // 45
    nsau    a5, a8
    ST      a5
    nsau    a5, a3
    ST      a5
    mul16u  a5, a3, a4
    ST      a5
    mul16s  a5, a3, a4
    ST      a5
    mull    a5, a3, a4
    ST      a5                  // 50
    muluh   a5, a3, a4
    ST      a5
    mulsh   a5, a3, a6
    ST      a5
    quou    a5, a3, a4
    ST      a5
    quos    a5, a3, a4
    ST      a5
    remu    a5, a3, a4
    ST      a5                  // 55
    rems    a5, a3, a4
    ST      a5
    LI      a9, 0x80000000
    movi    a10, -1
    quos    a5, a9, a10
    ST      a5
    rems    a5, a9, a10
    ST      a5
    slli    a5, a3, 1
    ST      a5
    slli    a5, a3, 31
    ST      a5                  // 60
    srai    a5, a3, 4
    ST      a5
    srai    a5, a3, 31
    ST      a5
    srli    a5, a3, 4
    ST      a5
    srli    a5, a3, 15
    ST      a5
    ssr     a7
    src     a5, a3, a4
    ST      a5                  // 65
    srl     a5, a3
    ST      a5
    sra     a5, a3
    ST      a5
    ssl     a7
    sll     a5, a3
    ST      a5
    ssa8l   a7
    srl     a5, a3
    ST      a5
    ssa8b   a7
    sll     a5, a3
    ST      a5                  // 70
    ssai    31
    src     a5, a3, a4
    ST      a5
    rsr     a5, sar
    ST      a5
    extui   a5, a3, 4, 8
    ST      a5
    extui   a5, a3, 28, 4
    ST      a5
    extui   a5, a3, 0, 16
    ST      a5                  // 75
    extui   a5, a3, 16, 16
    ST      a5
    ssai    0
    src     a5, a3, a4
    ST      a5
    sra     a5, a3
    ST      a5

    // booleans
    movi    a5, 0xA5
    wsr     a5, br
    andb    b8, b0, b2
    andbc   b9, b0, b2
    orb     b10, b1, b3
    orbc    b11, b1, b3
    xorb    b12, b0, b1
    any4    b13, b4
    all4    b14, b0
    any8    b15, b0
    rsr     a5, br
    ST      a5                  // 79
    LI      a5, 0xFF00
    wsr     a5, br
    all8    b0, b8
    any4    b1, b4
    rsr     a5, br
    ST      a5                  // 80
    movi    a5, 2
    wsr     a5, br
    mov.n   a5, a7
    movt    a5, a3, b1
    ST      a5
    mov.n   a5, a7
    movf    a5, a3, b1
    ST      a5
    movf    a5, a3, b0
    ST      a5

    // loads and stores (scratch area at buf + 512)
    addmi   a14, a2, 512
    LI      a5, 0x80FF7F01
    s32i    a5, a14, 0
    s32i    a8, a14, 4
    s32i    a8, a14, 8
    s32i    a8, a14, 12
    l8ui    a5, a14, 0
    ST      a5                  // 84
    l8ui    a5, a14, 3
    ST      a5
    l16ui   a5, a14, 2
    ST      a5
    l16si   a5, a14, 2
    ST      a5
    l16si   a5, a14, 0
    ST      a5
    l32i.n  a5, a14, 0
    ST      a5                  // 89
    s8i     a3, a14, 4
    s16i    a3, a14, 6
    l32i    a5, a14, 4
    ST      a5
    addi    a13, a14, 9         // unaligned
    s32i    a3, a13, 0
    l32i    a5, a13, 0
    ST      a5
    l32i    a5, a14, 8
    ST      a5
    addi    a12, a14, 10
    l16ui   a5, a12, 0
    ST      a5
    l32ai   a5, a14, 0
    ST      a5                  // 94
    s32ri   a4, a14, 16
    l32i    a5, a14, 16
    ST      a5
    s32nb   a3, a14, 20
    l32i.n  a5, a14, 20
    ST      a5
    s32i.n  a4, a14, 24
    l32i    a5, a14, 24
    ST      a5
    LI      a5, 0xDEADBEEF
    ST      a5                  // 98
    sub     a2, a15, a2         // bytes written
    retw

// ---------------------------------------------------------------------------------------------------------------------
// branches(buf): every conditional branch taken and not taken, J, JX
    .literal_position
    .global branches
    .type branches, @function
    .align 4
branches:
    entry   a1, 32
    mov     a15, a2
    LI      a3, 0x87654321
    LI      a4, 0x0000F00D
    movi    a6, -16
    movi    a7, 5
    movi    a8, 0
    mov     a9, a3
    BR      beqz, a8            // 0
    BR      beqz, a3
    BR      bnez, a3
    BR      bnez, a8
    BR      bltz, a6
    BR      bltz, a7            // 5
    BR      bgez, a7
    BR      bgez, a6
    BR      beqz.n, a8
    BR      bnez.n, a8
    BR      bnez.n, a3          // 10
    BR      beqz.n, a3
    BR      beqi, a7, 5
    BR      beqi, a6, -1
    BR      bnei, a7, 5
    BR      blti, a6, -1        // 15
    BR      bgei, a7, 5
    BR      blti, a7, 4
    BR      bgei, a6, 256
    BR      bltui, a7, 6
    BR      bgeui, a3, 32768    // 20
    BR      bltui, a6, 2
    BR      bgeui, a7, 65536
    BR      beq, a3, a9
    BR      bne, a3, a9
    BR      blt, a3, a4         // 25
    BR      bge, a3, a4
    BR      bltu, a3, a4
    BR      bgeu, a3, a4
    BR      bany, a3, a4
    BR      bnone, a3, a4       // 30
    BR      ball, a3, a4
    BR      bnall, a3, a4
    BR      ball, a3, a9
    BR      bbc, a3, a7
    BR      bbs, a3, a7         // 35
    BR      bbci, a3, 1
    BR      bbsi, a3, 31
    BR      bbsi, a3, 17
    BR      bbci, a3, 30
    movi    a5, 2
    wsr     a5, br
    BR      bt, b1              // 40
    BR      bf, b1
    BR      bt, b0
    BR      bf, b0
    movi    a5, 1
    j       1f
    movi    a5, 0
1:  ST      a5                  // 44
    movi    a5, 1
    LI      a10, .Ljxtarget
    jx      a10
    movi    a5, 0
.Ljxtarget:
    ST      a5                  // 45
    sub     a2, a15, a2
    retw

// ---------------------------------------------------------------------------------------------------------------------
// loops(buf): zero-overhead loops
    .literal_position
    .global loops
    .type loops, @function
    .align 4
loops:
    entry   a1, 48
    mov     a15, a2
    movi    a6, -16
    movi    a7, 5
    movi    a8, 0

    movi    a10, 0              // 0: LOOP 5
    loop    a7, 1f
    addi    a10, a10, 1
1:  ST      a10

    movi    a10, 0              // 1: LOOPNEZ 0 skips
    loopnez a8, 1f
    addi    a10, a10, 1
1:  ST      a10

    movi    a10, 0              // 2: LOOPGTZ -16 skips
    loopgtz a6, 1f
    addi    a10, a10, 1
1:  ST      a10

    movi    a10, 0              // 3: LOOPGTZ 5
    loopgtz a7, 1f
    addi    a10, a10, 1
1:  ST      a10

    movi    a10, 0              // 4: LOOP 1 (LCOUNT = 0)
    movi    a11, 1
    loop    a11, 1f
    addi    a10, a10, 1
1:  ST      a10

    movi    a10, 0              // 5: taken branch out of the loop as the last instruction before LEND
    movi    a11, 10
    loop    a11, 1f
    addi    a10, a10, 1
    beqi    a10, 3, 2f
1:  movi    a10, 99
2:  ST      a10

    movi    a10, 0              // 6: taken branch to LEND: no loop-back
    movi    a11, 10
    loop    a11, 1f
    addi    a10, a10, 1
    bnez    a7, 1f
1:  ST      a10

    movi    a10, 0              // 7: not-taken branch at LEND: loop-back
    movi    a11, 10
    loop    a11, 1f
    addi    a10, a10, 1
    beqz    a7, 1f
1:  ST      a10

    movi    a10, 0              // 8: body longer than a block
    movi    a11, 3
    loop    a11, 1f
    .rept 70
    addi    a10, a10, 1
    .endr
1:  ST      a10

    // 9: a block decoded before LEND pointed into it
    movi    a10, 0
    movi    a12, 0
    movi    a13, 1
    movi    a11, 4
    j       .Lstalepre
.Lstaleloop:
    loop    a11, .Lstaleend
.Lstalebody:
    addi    a10, a10, 1
    addi    a10, a10, 1
.Lstaleend:
    addi    a12, a12, 1
    addi    a12, a12, 1
    beqz    a13, .Lstaledone
    movi    a13, 0
    j       .Lstaleloop
.Lstalepre:
    j       .Lstalebody
.Lstaledone:
    slli    a10, a10, 8
    add     a10, a10, a12
    ST      a10                 // 9: (2 + 8) << 8 | 4

    rsr     a10, lcount
    ST      a10                 // 10

    // 11: loop registers written directly
    movi    a10, 0
    LI      a11, .Lwsrbeg
    wsr     a11, lbeg
    LI      a11, .Lwsrend
    wsr     a11, lend
    movi    a11, 2
    wsr     a11, lcount
    isync
.Lwsrbeg:
    addi    a10, a10, 1
    addi    a10, a10, 2
.Lwsrend:
    ST      a10                 // 11: 3 iterations * 3

    // 12: calls inside the loop body (a8..a15 are not preserved across CALL8: keep the buffer pointer on the stack)
    s32i    a15, a1, 0
    movi    a10, 0
    movi    a11, 4
    loop    a11, 1f
    LI      a8, increment
    callx8  a8
    nop
1:  l32i    a15, a1, 0
    ST      a10                 // 12

    sub     a2, a15, a2
    retw

    .literal_position
    .global increment
    .type increment, @function
    .align 4
increment:                      // increment(x) = x + 1
    entry   a1, 32
    addi    a2, a2, 1
    retw

// ---------------------------------------------------------------------------------------------------------------------
// Window chains. Each frame sets register patterns, calls one level deeper and returns
//   result(n) = result(n - 1) + sum of the registers it kept across the call;   result(0) = trap TRAP_LEAF's value
// Pattern: a2 = n, a(i) = (i << 12) | n for i = 3..11.

    .macro PATTERNS last
    .irp i, 3,4,5,6,7,8,9,10,11
    .if \i <= \last
    movi    a\i, (\i << 4)
    slli    a\i, a\i, 8
    or      a\i, a\i, a2
    .endif
    .endr
    .endm

    .macro SUMREGS dst, last
    mov     \dst, a2
    .irp i, 3,4,5,6,7,8,9,10,11
    .if \i <= \last
    add     \dst, \dst, a\i
    .endif
    .endr
    .endm

    // chain4(n): CALL4 recursion
    .literal_position
    .global chain4
    .type chain4, @function
    .align 4
chain4:
    entry   a1, 32
    PATTERNS 3
    beqz    a2, 1f
    addi    a6, a2, -1
    call4   chain4
    SUMREGS a7, 3
    add     a2, a6, a7
    retw
1:  LI      a8, TRAP_LEAF
    callx8  a8
    mov     a2, a10
    retw

    // chain8(n): CALL8 recursion
    .literal_position
    .global chain8
    .type chain8, @function
    .align 4
chain8:
    entry   a1, 48
    PATTERNS 7
    beqz    a2, 1f
    addi    a10, a2, -1
    call8   chain8
    SUMREGS a11, 7
    add     a2, a10, a11
    retw
1:  LI      a8, TRAP_LEAF
    callx8  a8
    mov     a2, a10
    retw

    // chain12(n): CALL12 recursion
    .literal_position
    .global chain12
    .type chain12, @function
    .align 4
chain12:
    entry   a1, 64
    PATTERNS 11
    beqz    a2, 1f
    addi    a14, a2, -1
    call12  chain12
    SUMREGS a15, 11
    add     a2, a14, a15
    retw
1:  LI      a8, TRAP_LEAF
    callx8  a8
    mov     a2, a10
    retw

    // mix(n): n % 3 == 0 -> CALL12, 1 -> CALL4, 2 -> CALL8; n == 0 -> CALLX8 TRAP_LEAF (keeps a2..a7)
    .literal_position
    .global mix
    .type mix, @function
    .align 4
mix:
    entry   a1, 64
    PATTERNS 11
    beqz    a2, .Lmixleaf
    movi    a12, 3
    remu    a12, a2, a12
    beqi    a12, 1, .Lmix4
    beqi    a12, 2, .Lmix8
    addi    a14, a2, -1
    call12  mix
    SUMREGS a15, 11
    add     a2, a14, a15
    retw
.Lmix4:
    addi    a6, a2, -1
    call4   mix
    SUMREGS a7, 3
    add     a2, a6, a7
    retw
.Lmix8:
    addi    a10, a2, -1
    call8   mix
    SUMREGS a11, 7
    add     a2, a10, a11
    retw
.Lmixleaf:
    LI      a8, TRAP_LEAF
    callx8  a8
    SUMREGS a11, 7
    add     a2, a10, a11
    retw

    // allocatest(n): the caller's frame is spilled when MOVSP runs (alloca exception + underflow)
    .literal_position
    .global allocatest
    .type allocatest, @function
    .align 4
allocatest:
    entry   a1, 32
    PATTERNS 7
    mov     a10, a2
    call8   allocainner
    SUMREGS a11, 7
    add     a2, a10, a11
    retw

    .literal_position
    .type allocainner, @function
    .align 4
allocainner:                    // allocainner(n): chain12(n) to spill everything, then alloca 64 bytes
    entry   a1, 48                  // a CALL12 caller: 32 bytes of extra save area at the top of the frame
    mov     a14, a2
    call12  chain12
    addi    a3, a1, -64
    movsp   a1, a3
    movi    a4, 0x77
    s32i    a4, a1, 0           // use the new stack
    l32i    a4, a1, 0
    add     a2, a14, a4
    retw

// ---------------------------------------------------------------------------------------------------------------------
// trapargs(): CALLX8 TRAP_ADD8 with 8 arguments (two on the stack), CALLX4 / CALLX12 / CALLX0 TRAP_ADD2
    .literal_position
    .global trapargs
    .type trapargs, @function
    .align 4
trapargs:
    entry   a1, 48
    movi    a10, 1
    movi    a11, 2
    movi    a12, 3
    movi    a13, 4
    movi    a14, 5
    movi    a15, 6
    movi    a8, 7
    s32i    a8, a1, 0
    movi    a8, 8
    s32i    a8, a1, 4
    LI      a8, TRAP_ADD8
    callx8  a8
    mov     a3, a10             // 36
    movi    a6, 10
    movi    a7, 20
    LI      a8, TRAP_ADD2
    callx4  a8
    add     a3, a3, a6          // + 30
    movi    a14, 100
    movi    a15, 200
    LI      a8, TRAP_ADD2
    callx12 a8
    add     a3, a3, a14         // + 300
    mov     a12, a0
    mov     a13, a3
    movi    a2, 1000
    LI      a3, 2000
    LI      a8, TRAP_ADD2
    callx0  a8
    mov     a0, a12
    add     a2, a2, a13         // + 3000
    retw

// cbtest(n) = TRAP_CB(cbfn, n) + 1000; cbfn(n) = n ? TRAP_CB(cbfn, n - 1) + n : TRAP_ADD2(5, 6)
    .literal_position
    .global cbtest
    .type cbtest, @function
    .align 4
cbtest:
    entry   a1, 48
    mov     a11, a2
    LI      a10, cbfn
    LI      a8, TRAP_CB
    callx8  a8
    movi    a3, 1000
    add     a2, a10, a3
    retw

    .literal_position
    .global cbfn
    .type cbfn, @function
    .align 4
cbfn:
    entry   a1, 48
    beqz    a2, 1f
    addi    a11, a2, -1
    LI      a10, cbfn
    LI      a8, TRAP_CB
    callx8  a8
    add     a2, a10, a2
    retw
1:  movi    a10, 5
    movi    a11, 6
    LI      a8, TRAP_ADD2
    callx8  a8
    mov     a2, a10
    retw

    // add3(a, b, c) = a + b + c, called by the host
    .literal_position
    .global add3
    .type add3, @function
    .align 4
add3:
    entry   a1, 32
    add     a2, a2, a3
    add     a2, a2, a4
    retw

    // sum8(a..h): the host passes two arguments on the stack
    .literal_position
    .global sum8
    .type sum8, @function
    .align 4
sum8:
    entry   a1, 32
    add     a2, a2, a3
    add     a2, a2, a4
    add     a2, a2, a5
    add     a2, a2, a6
    add     a2, a2, a7
    l32i    a8, a1, 32
    add     a2, a2, a8
    l32i    a8, a1, 36
    add     a2, a2, a8
    retw

// ---------------------------------------------------------------------------------------------------------------------
// sjtest(jmpbuf): newlib setjmp, then 12 CALL12 levels deeper, longjmp(jmpbuf, 42) at the bottom.
// Returns 42 + 0x44 + 0x55 + 0x66 + 0x77 (a4..a7 restored from the jmp_buf / the spilled frame)
    .literal_position
    .global sjtest
    .type sjtest, @function
    .align 4
sjtest:
    entry   a1, 64
    movi    a4, 0x44
    movi    a5, 0x55
    movi    a6, 0x66
    movi    a7, 0x77
    mov     a10, a2
    call8   setjmp
    bnez    a10, 1f
    movi    a4, 0x11            // changed after setjmp: longjmp restores the values seen by the spill at setjmp time
    mov     a14, a2
    movi    a15, 12
    call12  sjdeep
    movi    a2, -1
    retw
1:  add     a2, a10, a4
    add     a2, a2, a5
    add     a2, a2, a6
    add     a2, a2, a7
    retw

    .literal_position
    .type sjdeep, @function
    .align 4
sjdeep:                         // sjdeep(jmpbuf, n)
    entry   a1, 64
    mov     a14, a2
    addi    a15, a3, -1
    mov     a10, a2
    movi    a11, 42
    beqz    a3, 1f
    PATTERNS 9
    call12  sjdeep
    movi    a2, -2
    retw
1:  call8   longjmp
    movi    a2, -3
    retw

// ---------------------------------------------------------------------------------------------------------------------
// fpu(buf)
    .literal_position
    .global fpu
    .type fpu, @function
    .align 4
fpu:
    entry   a1, 32
    mov     a15, a2
    FLI     f1, 0x3F800000      // 1.0
    FLI     f2, 0x40000000      // 2.0
    add.s   f0, f1, f2
    FST     f0                  // 0: 3.0
    FLI     f3, 0x7F800000      // +inf
    FLI     f4, 0xFF800000      // -inf
    add.s   f0, f3, f4
    FST     f0                  // 1: default NaN
    FLI     f5, 0x7FA00001      // signalling NaN with payload
    add.s   f0, f5, f1
    FST     f0                  // 2: default NaN
    FLI     f6, 0x00000000
    mul.s   f0, f6, f3
    FST     f0                  // 3: 0 * inf
    sub.s   f0, f1, f1
    FST     f0                  // 4: +0
    neg.s   f0, f6
    FST     f0                  // 5: -0
    neg.s   f7, f6
    add.s   f0, f7, f7
    FST     f0                  // 6: -0 + -0 = -0
    add.s   f0, f6, f7
    FST     f0                  // 7: +0 + -0 = +0
    neg.s   f0, f5
    FST     f0                  // 8: NEG.S keeps the payload
    abs.s   f0, f4
    FST     f0                  // 9: |-inf|
    mov.s   f0, f5
    FST     f0                  // 10
    FLI     f8, 0x3F800800      // 1 + 2^-12
    FLI     f0, 0xBF800000      // -1
    madd.s  f0, f8, f8
    FST     f0                  // 11: fused: 2^-11 + 2^-24
    FLI     f0, 0x3F800000
    msub.s  f0, f8, f8
    FST     f0                  // 12: fused: -(2^-11 + 2^-24)
    FLI     f0, 0xBF800000
    maddn.s f0, f8, f8
    FST     f0                  // 13
    mul.s   f0, f8, f8
    sub.s   f0, f0, f1
    FST     f0                  // 14: unfused: 2^-11
    FLI     f9, 0x40200000      // 2.5
    round.s a5, f9, 0
    ST      a5                  // 15: 2
    FLI     f9, 0x40600000      // 3.5
    round.s a5, f9, 0
    ST      a5                  // 16: 4
    FLI     f9, 0xC0200000      // -2.5
    round.s a5, f9, 0
    ST      a5                  // 17: -2
    FLI     f9, 0x3FA00000      // 1.25
    round.s a5, f9, 1
    ST      a5                  // 18: 2 (2.5 -> even)
    FLI     f9, 0xBFD9999A      // -1.7
    trunc.s a5, f9, 0
    ST      a5                  // 19: -1
    FLI     f9, 0x4F32D05E      // 3e9
    trunc.s a5, f9, 0
    ST      a5                  // 20: 0x7FFFFFFF
    FLI     f9, 0xCF32D05E      // -3e9
    trunc.s a5, f9, 0
    ST      a5                  // 21: 0x80000000
    trunc.s a5, f5, 0
    ST      a5                  // 22: NaN -> 0x7FFFFFFF
    trunc.s a5, f3, 0
    ST      a5                  // 23: +inf
    trunc.s a5, f4, 0
    ST      a5                  // 24: -inf
    FLI     f9, 0xBFC00000      // -1.5
    floor.s a5, f9, 0
    ST      a5                  // 25: -2
    ceil.s  a5, f9, 0
    ST      a5                  // 26: -1
    FLI     f9, 0x3F99999A      // 1.2
    ceil.s  a5, f9, 0
    ST      a5                  // 27: 2
    FLI     f9, 0x4F32D05E      // 3e9
    utrunc.s a5, f9, 0
    ST      a5                  // 28: 3000000000
    FLI     f9, 0xBF800000      // -1
    utrunc.s a5, f9, 0
    ST      a5                  // 29: 0xFFFFFFFF
    FLI     f9, 0xC0B00000      // -5.5
    utrunc.s a5, f9, 0
    ST      a5                  // 30: -5
    FLI     f9, 0x4F9502F9      // 5e9
    utrunc.s a5, f9, 0
    ST      a5                  // 31: 0xFFFFFFFF
    utrunc.s a5, f5, 0
    ST      a5                  // 32: NaN
    movi    a6, -3
    float.s f0, a6, 1
    FST     f0                  // 33: -1.5
    movi    a6, -1
    ufloat.s f0, a6, 0
    FST     f0                  // 34: 4294967296.0
    LI      a6, 0x7FFFFFFF
    float.s f0, a6, 0
    FST     f0                  // 35: 2147483648.0
    LI      a6, 0x01000001
    float.s f0, a6, 0
    FST     f0                  // 36: 16777216 (rounded to even)
    FLI     f9, 0x40400000      // 3.0
    trunc.s a5, f9, 3
    ST      a5                  // 37: 24

    // comparisons: b0 un, b1 oeq, b2 ueq, b3 olt, b4 ult, b5 ole, b6 ule for (1, 2); b8.. for (NaN, 1); then (2, 2)
    movi    a5, 0
    wsr     a5, br
    un.s    b0, f1, f2
    oeq.s   b1, f1, f2
    ueq.s   b2, f1, f2
    olt.s   b3, f1, f2
    ult.s   b4, f1, f2
    ole.s   b5, f1, f2
    ule.s   b6, f1, f2
    un.s    b8, f5, f1
    oeq.s   b9, f5, f1
    ueq.s   b10, f5, f1
    olt.s   b11, f5, f1
    ult.s   b12, f5, f1
    ole.s   b13, f5, f1
    ule.s   b14, f5, f1
    rsr     a5, br
    ST      a5                  // 38
    movi    a5, 0
    wsr     a5, br
    un.s    b0, f2, f2
    oeq.s   b1, f2, f2
    ueq.s   b2, f2, f2
    olt.s   b3, f2, f2
    ult.s   b4, f2, f2
    ole.s   b5, f2, f2
    ule.s   b6, f2, f2
    oeq.s   b7, f6, f7          // +0 == -0
    rsr     a5, br
    ST      a5                  // 39

    // conditional moves
    movi    a6, 0
    movi    a7, -1
    mov.s   f0, f1
    moveqz.s f0, f2, a6
    FST     f0                  // 40: 2.0
    mov.s   f0, f1
    movnez.s f0, f2, a6
    FST     f0                  // 41: 1.0
    mov.s   f0, f1
    movltz.s f0, f2, a7
    FST     f0                  // 42: 2.0
    mov.s   f0, f1
    movgez.s f0, f2, a7
    FST     f0                  // 43: 1.0
    movi    a5, 1
    wsr     a5, br
    mov.s   f0, f1
    movt.s  f0, f2, b0
    FST     f0                  // 44: 2.0
    mov.s   f0, f1
    movf.s  f0, f2, b0
    FST     f0                  // 45: 1.0

    // loads / stores with update (scratch at buf + 512)
    addmi   a14, a2, 512
    ssi     f1, a14, 0
    ssi     f2, a14, 4
    lsi     f10, a14, 4
    FST     f10                 // 46: 2.0
    movi    a6, 4
    lsx     f10, a14, a6
    FST     f10                 // 47: 2.0
    mov     a13, a14
    lsip    f10, a13, 4
    FST     f10                 // 48: 1.0 (post-increment)
    sub     a5, a13, a14
    ST      a5                  // 49: 4
    lsxp    f10, a13, a6
    FST     f10                 // 50: 2.0
    sub     a5, a13, a14
    ST      a5                  // 51: 8
    ssip    f3, a13, 4
    ssxp    f4, a13, a6
    sub     a5, a13, a14
    ST      a5                  // 52: 16
    l32i    a5, a14, 8
    ST      a5                  // 53: +inf
    l32i    a5, a14, 12
    ST      a5                  // 54: -inf
    ssx     f1, a14, a6
    l32i    a5, a14, 4
    ST      a5                  // 55: 1.0
    movi    a5, 3
    wur     a5, fcr
    rur     a5, fcr
    ST      a5                  // 56
    movi    a5, 0
    wur     a5, fcr
    sub     a2, a15, a2
    retw

// ---------------------------------------------------------------------------------------------------------------------
// atomic(ptr, expected, new): SCOMPARE1 = expected, S32C1I new -> returns the old value
    .literal_position
    .global atomic
    .type atomic, @function
    .align 4
atomic:
    entry   a1, 32
    wsr     a3, scompare1
    s32c1i  a4, a2, 0
    mov     a2, a4
    retw

    // load32(addr) / store32(addr, value) / fetch(addr): for fault tests
    .literal_position
    .global load32
    .type load32, @function
    .align 4
load32:
    entry   a1, 32
    movi    a3, 0x123
    l32i    a3, a2, 0
    mov     a2, a3
    retw

    .literal_position
    .global store32
    .type store32, @function
    .align 4
store32:
    entry   a1, 32
    s32i    a3, a2, 0
    movi    a2, 1
    retw

    .literal_position
    .global storebyte
    .type storebyte, @function
    .align 4
storebyte:                      // storebyte(addr, value) = byte read back with L8UI
    entry   a1, 32
    s8i     a3, a2, 0
    l8ui    a2, a2, 0
    retw

    .literal_position
    .global jumpto
    .type jumpto, @function
    .align 4
jumpto:                         // jumpto(addr): CALLX8 to a host-chosen address
    entry   a1, 32
    callx8  a2
    mov     a2, a10
    retw

    // ccount(): returns CCOUNT difference over 10 instructions (expected 11: the RSR itself counts)
    .literal_position
    .global ccount
    .type ccount, @function
    .align 4
ccount:
    entry   a1, 32
    rsr     a3, ccount
    .rept 10
    nop
    .endr
    rsr     a4, ccount
    sub     a2, a4, a3
    LI      a5, 1000000
    wsr     a5, ccount
    rsr     a6, ccount          // 1000000 + 1
    sub     a6, a6, a5
    slli    a6, a6, 16
    or      a2, a2, a6
    retw

    // selfmod(): patches its own MOVI (in IRAM), returns 1 before and 2 after the patch: 0x0102 expected
    .literal_position
    .global selfmod
    .type selfmod, @function
    .align 4
selfmod:
    entry   a1, 32
    call8   .Lsmget
    mov     a3, a10
    LI      a4, .Lsmmovi
    movi    a6, 2
    s8i     a6, a4, 2           // MOVI bytes: 22 a0 imm8 -> imm8 = 2
    call8   .Lsmget
    slli    a3, a3, 8
    or      a2, a3, a10
    retw
    .align 4
.Lsmget:
    entry   a1, 32
.Lsmmovi:
    movi    a2, 1
    retw

    .literal_position
    .global syscallnz
    .type syscallnz, @function
    .align 4
syscallnz:
    entry   a1, 32
    movi    a2, 5
    syscall
    retw

    .literal_position
    .global brk
    .type brk, @function
    .align 4
brk:
    entry   a1, 32
    break   1, 15
    retw

    .literal_position
    .global illegal
    .type illegal, @function
    .align 4
illegal:
    entry   a1, 32
    ill
    retw

    .literal_position
    .global privileged
    .type privileged, @function
    .align 4
privileged:
    entry   a1, 32
    rsr     a2, eps2
    retw

    .literal_position
    .global divzero
    .type divzero, @function
    .align 4
divzero:
    entry   a1, 32
    movi    a3, 0
    quou    a2, a2, a3
    retw

    .literal_position
    .global eeinsn
    .type eeinsn, @function
    .align 4
eeinsn:
    entry   a1, 32
    .byte 0x0E, 0x00, 0x00, 0x00    // op0 = 14: a 4-byte EE.* instruction
    retw

    // longloop(n): n iterations of a 4-instruction body, for the budget / snapshot tests
    .literal_position
    .global longloop
    .type longloop, @function
    .align 4
longloop:
    entry   a1, 32
    movi    a3, 0
    movi    a4, 0
1:  addi    a3, a3, 3
    xor     a4, a4, a3
    addi    a2, a2, -1
    bnez    a2, 1b
    add     a2, a3, a4
    retw
