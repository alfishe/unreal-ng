; =============================================================================
; Edge Demo - IM2 Interrupt Handler
; Address: BF26h
; =============================================================================
;
; Vector setup:
;   I register = BEh
;   Bus value during INT acknowledge = FFh (typical)
;   Vector read from: BEFFh
;   Vector content: 26h BFh → handler at BF26h
;
; =============================================================================

                ORG     BF26h

; -----------------------------------------------------------------------------
; Main interrupt handler
; Very minimal - just disables interrupts and jumps to frame counter
; This keeps ISR execution time predictable for timing-critical code
; -----------------------------------------------------------------------------

int_handler:
                di                      ; 4T   Disable interrupts immediately
                jp      frame_counter   ; 10T  Jump to actual handler at 625Ah

; -----------------------------------------------------------------------------
; Alternative entry point at BF2Ah
; Used for vector table manipulation (self-modifying code)
; -----------------------------------------------------------------------------

                ORG     BF2Ah

alt_entry:
                ld      bc, 06FFh       ; 10T  BC = 06FFh
                di                      ; 4T   Disable interrupts
                ld      hl, vector_new  ; 10T  HL points to new vector value
                ld      (0BEFFh), hl    ; 16T  Store new vector (self-modify!)
                xor     a               ; 4T   A = 0
                ld      hl, sync_point  ; 10T  HL points to sync entry
                ei                      ; 4T   Enable interrupts
                                        ; Fall through to sync_point...

sync_point:     ; BF39h
                dec     a               ; 4T   Decrement A
                jp      (hl)            ; 4T   Jump through HL

vector_new:     ; BF3Bh - New vector data
                pop     hl              ; This is data, not code
                ; ... (table data follows)

; -----------------------------------------------------------------------------
; Restore original vector
; -----------------------------------------------------------------------------

                ORG     BF4Bh

restore_vector:
                ld      hl, int_handler ; 10T  Point to original handler
                ld      (0BEFFh), hl    ; 16T  Restore vector
                ret                     ; 10T  Return

; -----------------------------------------------------------------------------
; System restore routine
; -----------------------------------------------------------------------------

                ORG     BF52h

system_restore:
                di                      ; 4T   Disable interrupts
                                        ; FD prefix follows (IY operations)
                ld      hl, 5C3Ah       ; System variable area
                ld      a, 3Fh          ;
                ld      i, a            ; Restore I register to ROM default
                im      1               ; Switch back to IM 1
                ret

; =============================================================================
; Frame Counter ISR (at 625Ah)
; =============================================================================
; This is where the JP from BF27h lands.
; It's a full-featured ISR that maintains frame counters.
;
; The actual code at 625Ah:
;
; frame_counter:
;                 push    hl              ; Save all registers
;                 push    de
;                 push    bc
;                 push    af
;                 exx
;                 push    hl
;                 push    de
;                 push    bc
;
;                 ld      hl, (640Eh)     ; Get frame counter 1
;                 inc     hl              ; Increment
;                 ld      (640Eh), hl     ; Store back
;
;                 ld      hl, (640Ch)     ; Get frame counter 2
;                 inc     hl              ; Increment
;                 ld      (640Ch), hl     ; Store back
;
;                 ld      a, 14h          ; Memory page 14h
;                 ld      bc, 7FFDh       ; Port 7FFDh (128K paging)
;                 out     (c), a          ; Switch memory page
;
;                 call    C003h           ; Call some routine
;
;                 ld      a, (5B5Ch)      ; Get stored page value
;                 ld      bc, 7FFDh
;                 out     (c), a          ; Restore memory page
;
;                 pop     bc              ; Restore all registers
;                 pop     de
;                 pop     hl
;                 exx
;                 pop     af
;                 pop     bc
;                 pop     de
;                 pop     hl
;
;                 ei                      ; Re-enable interrupts
;                 ret                     ; Return from interrupt
;
; =============================================================================
; Notes
; =============================================================================
;
; 1. The minimal BF26h handler (DI + JP) keeps interrupt latency predictable
;    at exactly 14T from INT acknowledge.
;
; 2. The frame counters at 640Ch/640Eh are used by the main code to track
;    animation frames and timing.
;
; 3. Port 7FFDh usage indicates Pentagon 128K compatibility.
;
; 4. The self-modifying vector code at BF2Ah allows dynamic ISR switching
;    for different effect phases.
;
; =============================================================================

frame_counter   EQU     625Ah
