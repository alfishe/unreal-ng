# ShuttlePRO v2, macOS 15 USB (0B33:0030)

Contour Design, product string "ShuttlePro", version 2.02, full speed. Recorded 2026-10-28 on Darwin 24.6 with
`docs/inprogress/2026-10-17-controller-manager/probes/hiddump.c` built with the vendor id as a third argument
(`hiddump 600 0030 0B33`: IOHIDManager, every input report). The owner turned the jog wheel, turned the shuttle ring
to the left and to the right, and pressed the 15 keys one by one.

- `2026-10-28-descriptor-and-reports.txt`: `added` line (usage page 12, usage 1: a Consumer Control; max input 5),
  the 75-byte report descriptor, then `<ms> dev0 id=00 len=5 : <bytes>` (no report id).

Input report, 5 bytes: byte 0 the shuttle ring, signed -7..7 and 0 at rest (the descriptor declares a relative Wheel
of -128..127; the ring turned to -6 and +5 here); byte 1 the jog wheel, a counter that wraps (0x75 to 0x82 here, one
count a detent); byte 2 constant 0; bytes 3-4 the keys, bits 0..14. The descriptor declares 13 buttons: bits 13 and
14 (byte 4 bits 5 and 6, `20` and `40`) come anyway and are the two keys beside the jog wheel. The device repeats a
report unchanged now and then. Layout and family: [control surfaces devices.md §4](../../../../../docs/inprogress/2026-10-18-control-surfaces/devices.md#4-contour-shuttlepro-v2-and-shuttlexpress).
