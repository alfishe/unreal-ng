; ---------------------------------------------------------------------------
; main.asm - tiny ZX Spectrum 48K demo for debugging with DeZog over unreal-ng
;
; A visible loop: cycle the border colour and keep a counter in RAM, with a
; delay so single-stepping and breakpoints are easy to observe.
;
; Built in two ways (either is fine, the emulator only needs the .sna):
;   1. make-sna.py      - no tools required, emits the exact bytes below
;   2. sjasmplus        - `make-sna` variant in tasks.json; additionally emits
;                         src/main.sld, which DeZog can consume for
;                         source-level debugging via the launch.json option:
;                             "sjasmplus": [ { "path": "src/main.sld" } ]
;
; The addresses baked into make-sna.py (counter/delay/stack_top) come from
; this listing - keep both in sync when editing.
; ---------------------------------------------------------------------------

                DEVICE ZXSPECTRUM48

STACK_SIZE      EQU 64

                ORG 0x8000
start:
                di
                ld   sp, stack_top
                ld   a, 0                 ; a = border colour (0..7)
main_loop:
                out  (0xFE), a            ; set border colour  <-- try a breakpoint here
                ld   (counter), a         ; store colour in RAM (watch `counter`)
                inc  a
                and  0x07
                call delay
                jp   main_loop

; Busy-wait so the border change is visible / stepping is comfortable.
delay:
                ld   bc, 0x4000
delay_loop:
                dec  bc
                ld   a, b
                or   c
                jr   nz, delay_loop
                ret

counter:        db 0

stack_bottom:   defs STACK_SIZE*2, 0
stack_top:

                ; sjasmplus only: emit a .sna snapshot booting into `start`
                SAVESNA "main.sna", start
