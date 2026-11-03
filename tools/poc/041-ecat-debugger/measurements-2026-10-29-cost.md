# Measurements 2026-10-29: the cost while the machine runs (goal 5)

## Setup

As in [measurements-2026-10-29-port.md](measurements-2026-10-29-port.md); Pentagon, booted to its 128 menu (the
ROM's idle loop, interrupts on), with and without NeoGS in `zxbus.1`. A small screen window shows the machine in every
phase.

## Method

`ecat-debugger-poc --measure 10`, one process, four windows of 10 s each:

1. screen only: the machine runs, no debug mode;
2. debug mode, no window: `debug_attach` + `debug_run(true)`, as the window sets them (breakpoint checks, PC history);
3. debugger window open and following the run (its 200 ms timer). The window opens one second before this window
   starts, so its creation (fonts, layout, the first paint) is outside it;
4. the same window over the paused machine, one second after the pause.

Per window: emulated frames per second, process CPU (`getrusage`, all threads, % of one core), the GUI thread's own
CPU (`CLOCK_THREAD_CPUTIME_ID`), and every `QEvent::Paint` an application event filter sees, counted with the area
of its region (not its bounding rectangle), split into the screen window and the debugger. The breakdown by widget
is printed for windows 3 and 4.

## Results, after the partial repaint

| Window | GS | frames/s | process CPU | GUI thread | debugger paints/s | debugger px/s |
|--------|:--:|---------:|------------:|-----------:|------------------:|--------------:|
| screen only | yes | 48.80 | 2.2 % | 0.2 % | 0 | 0 |
| debug mode, no window | yes | 48.80 | 2.9 % | 0.3 % | 0 | 0 |
| debugger window | yes | 48.90 | 3.0 % | 0.7 % | 14.3 | 133 688 |
| window, paused | yes | 0 | 0.2 % | 0.2 % | 0 | 0 |
| screen only | no | 48.89 | 2.3 % | 0.2 % | 0 | 0 |
| debug mode, no window | no | 48.80 | 2.5 % | 0.2 % | 0 | 0 |
| debugger window | no | 48.80 | 3.0 % | 0.7 % | 14.5 | 146 930 |
| window, paused | no | 0 | 0.2 % | 0.1 % | 0 | 0 |

(48.8 frames/s is the Pentagon's own rate: 71 680 T-states a frame at 3.5 MHz.)

What is still painted while running, by widget (a 5 s run): the listing's viewport (the rows PC passes through),
the registers' viewport (the changed rows), the stack (SP moves, so every row's address shifts), and the text caret
of the toolbar's address field (Qt's own blink, ~130 px). The screen window repaints only when a new frame differs
from the one shown, only the box of changed pixels: 0 in the menu, where nothing moves.

Paused: no paint at all in 3 runs of 4. In the fourth, the macOS style animated the dump's overlay scroll bars
(QScrollBar and the panel beneath them, 49 paints/s for a few seconds). Under `QT_STYLE_OVERRIDE=fusion` the paused
window paints nothing.

## Before the partial repaint (the views as upstream)

Window 3 counted from the window's first second (5 s run, GS on): the top-level window and the chain of parents
under each view (splitters, both tab widgets, the stacked widgets) were painted on every timer tick:

| Widget | paints in 4 s | px |
|--------|--------------:|---:|
| `DebuggerWindow` (the whole window background) | 38 | 15 790 654 |
| CPU `QTabWidget` | 38 | 11 066 487 |
| listing viewport | 27 | 6 460 887 |
| registers frame | 20 | 3 072 160 |
| ... | | |
| all debugger widgets, per second | 127 | 22 384 980 |

The listing and register viewports were painted whole on every tick even when nothing in them changed (the `refresh`
paths called `viewport()->update()`), and `FlagsView::refresh`'s `updateGeometry()` made both tab widgets repaint
whole. A profile of the GUI thread (`sample`, window `window.sample.txt` in scratch) had `QWidgetRepaintManager::sync`
in 74 of about 100 busy samples, the timer's own work (disassembly, reads) in about 11.

The first cost numbers (process CPU 9.8 % with the window against 6.9 % in debug mode, GUI thread 7.3 %) are not
comparable with the table above: that window also contained the window's creation. The paint counts are.
