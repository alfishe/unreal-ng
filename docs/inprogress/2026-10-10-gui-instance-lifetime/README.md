# GUI instance lifetime: removing an instance while the GUI reads it

Status: **landed on master** (f38e72b8e, 2026-10-10); BUGS.md #11 fixed. Progress: [TODO.md](TODO.md). Tracks [BUGS.md #11](../BUGS.md).

The Qt frontend reads an emulator instance's context in about 160 functions (199 reads) without any
guarantee that the context is still alive. A removal from the WebAPI, the CLI or MCP frees the context while
GUI handlers for that instance are still queued. #10 fixed the two sites the WebAPI stress hit first; fixing
sites one at a time does not converge, because every new stress round or new widget finds the next one.

| Document | Contents |
|----------|----------|
| [gap-analysis.md](gap-analysis.md) | How removal and the GUI interact today, where the window is, what is and is not protected, the site inventory |
| [proposal.md](proposal.md) | The options (per-site leases, synchronous GUI unbind, freeing at the last owner), the recommendation and its design |
| [test-plan.md](test-plan.md) | How the fix is proved: core tests, Qt tests, a GUI-busy sanitizer stress, a ratchet on raw reads |
| [TODO.md](TODO.md) | Checklist by phase |
| [roadmap.md](roadmap.md) | Phases, effort, what each phase gives, risks, decisions for the owner |

## In one paragraph

`EmulatorManager::RemoveEmulator()` frees the context and every subsystem at once, in `Emulator::Release()`,
on the removing thread. The GUI hears about the removal on the MessageCenter thread and queues its unbind to
the Qt thread, so the unbind runs after the free. Any GUI handler that runs between the free and the unbind
(a timer, a queued signal, a menu update, a click) reads freed memory. The recommendation is to split
`Release()`: removal **retires** the instance (stops it, detaches it from host input, observers and the
coordinator, refuses new work), and the memory is **freed when the last `shared_ptr<Emulator>` goes**. In the
GUI, `EmulatorBinding` becomes the only owner of that `shared_ptr` and the only source of the instance; raw
`Emulator*` / `EmulatorContext*` members go away. A stale handler then reads a stopped but valid machine
instead of freed memory, and this holds for the whole class, including sites nobody has found yet.
