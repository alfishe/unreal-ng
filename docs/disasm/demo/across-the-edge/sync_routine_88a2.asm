; =============================================================================
; Edge Demo - Main Border Synchronization Routine
; Address: 88A2h - 88C9h
; =============================================================================
;
; This routine creates border effects using two phases:
;   Phase 1: HALT-based frame synchronization with color cycling
;   Phase 2: Tight counted loops for precise scanline-level timing
;
; =============================================================================

                ORG     88A2h

; -----------------------------------------------------------------------------
; Phase 1: Frame-synchronized color cycling
; - Uses HALT to sync to INT (frame boundary)
; - Runs for 3 outer iterations × 50 inner HALTs = 150 total HALTs
; - Toggles border color (red/cyan) every 50 HALTs
; -----------------------------------------------------------------------------

sync_main:
                ld      a, 02h          ; 7T   Initial border color = RED
                ld      c, 03h          ; 7T   Outer loop counter (3 frames)

.outer_loop:
                ld      b, 32h          ; 7T   Inner loop counter (50 HALTs)

.inner_loop:
                out     (0FEh), a       ; 11T  Set border color
                ei                      ; 4T   Enable interrupts
                halt                    ; 4T   Wait for INT (+ variable wait)
                djnz    .inner_loop     ; 13T  Loop 50 times (B = 50 → 0)
                                        ; Total: 32T per iteration (excl. INT wait)

                xor     07h             ; 7T   Toggle color: 02h ↔ 05h
                                        ;      RED (010) XOR 111 = CYAN (101)
                                        ;      CYAN (101) XOR 111 = RED (010)
                dec     c               ; 4T   Decrement frame counter
                jp      nz, .outer_loop ; 10T  Loop for C iterations

; -----------------------------------------------------------------------------
; Phase 2: Precise timing loop for scanline-level effects
; - Uses self-looping DJNZ for exact T-state delays
; - Total iterations: 3200 (0C80h)
; - Creates rapid color cycling effect
; -----------------------------------------------------------------------------

.precise_loop:
                ld      hl, 0C80h       ; 10T  3200 iterations total
                ld      c, 02h          ; 7T   Starting color = RED

.timing_loop:
                ld      b, 0A5h         ; 7T   Delay counter = 165

.delay:
                djnz    .delay          ; 13T  Self-loop: 165 × 13T = 2145T delay
                                        ; This is a critical timing element!

                ld      a, c            ; 4T   Get current color
                xor     0Fh             ; 7T   Toggle ALL color bits (0-3)
                                        ;      Creates complementary color effect
                ld      c, a            ; 4T   Save new color
                out     (0FEh), a       ; 11T  Output to border

                dec     hl              ; 6T   Decrement iteration counter
                ld      a, l            ; 4T   \
                or      h               ; 4T   | Check if HL = 0
                jp      nz, .timing_loop; 10T  / Loop until done

                ret                     ; 10T  Return to caller

; =============================================================================
; Timing Analysis
; =============================================================================
;
; Phase 2 loop timing per iteration:
;   LD B,A5h    :    7T
;   DJNZ self   : 2145T  (165 × 13T)
;   LD A,C      :    4T
;   XOR 0Fh     :    7T
;   LD C,A      :    4T
;   OUT (FE),A  :   11T
;   DEC HL      :    6T
;   LD A,L      :    4T
;   OR H        :    4T
;   JP NZ       :   10T
;   ─────────────────────
;   Total       : 2202T per iteration
;
; Full Phase 2: 3200 × 2202T = 7,046,400T
; At 3.5MHz clock: ~2.013 seconds
;
; =============================================================================
; Color Values (port FEh bits 0-2)
; =============================================================================
;
;   000 = Black      100 = Green
;   001 = Blue       101 = Cyan
;   010 = Red        110 = Yellow
;   011 = Magenta    111 = White
;
; XOR 07h toggles bits 0-2: complementary colors
; XOR 0Fh toggles bits 0-3: includes BRIGHT bit
;
; =============================================================================
