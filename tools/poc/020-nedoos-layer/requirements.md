# 020 - POC requirements

What this POC had to show before the NedoOS layer is designed for the core.
Each P-item names the product requirement it de-risks
([requirements-nedoos-layer.md](../../../docs/inprogress/2026-09-30-nedoos-integration/requirements-nedoos-layer.md)).

| # | The POC must show that ... | Product req. | Result |
|---|---|---|---|
| P-1 | the kernel can be recognized in a running machine from a few fixed entry points | NK-1 | done: `detect` |
| P-2 | the exact kernel symbols can be rebuilt from the NedoOS sources on macOS / Linux, without IAR, and match the running kernel | NK-2, NK-3 | done: `generate-symbols.sh`; code bytes identical, only variables differ |
| P-3 | the task table is readable without any help from the running system: ids, parents, states, windows, command lines, where each task stopped | NK-4, NK-6 | done: `tasks` |
| P-4 | task memory can be read through the task's own windows while the CPU is in the kernel (the ATM's second register set) | NK-6, NK-8 | done: `read_task`; the CPU view was proven wrong there |
| P-5 | page owners, pipes, sockets and open files come out of kernel memory | NK-7, NK-9, NK-13, NK-14 | done, with limits: file names and socket addresses are not in memory |
| P-6 | "the kernel is busy" can be told apart from "a task loops", with the command, the caller and the driver loop | NK-21 | done: `kernel`, on the zxdb hang |
| P-7 | kernel calls can be traced | NK-15 | done at demo speed (about 4 calls/s through WebAPI breakpoints); the product needs a core hook |
| P-8 | a kernel call can be made on behalf of any task at a safe point, and the task continues unharmed | NK-11 | done: `call`, results cross-checked against memory reads |
| P-9 | a stuck network call can be ended cleanly, and the OS recovers | NK-22 | done: `unstick`; zxdb shows "No results found" |
| P-10 | every operation leaves the machine as it was: pause state, breakpoints, debug mode, registers | NK-11, NK-17 | done |

Out of scope for the POC: TTD journaling of injected calls, the ESP kernel's
socket table (its symbols build, see [results.md](results.md) §5), ATM Turbo 2+
builds, the Qt panel, the other automation surfaces.
