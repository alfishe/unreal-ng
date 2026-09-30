# NedoOS integration: OS-level analyzer and network adapters

Started 2026-09-30. Status: [TODO.md](TODO.md). Plan row: #78 in [PLAN.md](../PLAN.md).

Two linked topics, both driven by running the full NedoOS card on ZX-Evo
(`testdata/machines/zxevo/nedoos/`):

| Document | What it covers |
|---|---|
| [requirements-nedoos-layer.md](requirements-nedoos-layer.md) | NedoOS compatibility layer in the analyzers: tasks, memory pages, console, direct kernel calls, call trace, sockets, kernel health; on every automation surface |
| POC [020-nedoos-layer](../../../tools/poc/020-nedoos-layer/) | Python prototype of the layer over the WebAPI; its results confirm the requirements on a live NedoOS |
| [nedoos-kernel-reference.md](nedoos-kernel-reference.md) | Kernel memory model, symbols, structures and addresses, kernel-call mechanics; the zxdb hang read with a prototype of the layer; emulator gaps |
| [network-adapters-catalog.md](network-adapters-catalog.md) | Every network adapter for ZX-Evo / ATM / ZX-Bus machines: ports, chips, software, prior emulation, order of work |
| [tdd-network.md](tdd-network.md) | Technical design: virtual network + host bridge, ZXNETUSB / W5300, 16550 COM port with ESP modules (ESPNET, AT), ATM2 COM, TTD journaling, config, automation, debugging hooks, tests, steps N0..N6 |
| [tdd-network-debugging.md](tdd-network-debugging.md) | Network debugging TDD: taps at bus / line / socket / host level, per-adapter bus decoders, AT / ESPNET decoding, Wireshark (pcapng, extcap, Lua dissectors), monitoring, triggers, fault injection |
| [ideas-nedoos-development.md](ideas-nedoos-development.md) | Ideas for NedoOS development: app debugger, profiling, C to optimized asm with equivalence checks, unit tests, CI/CD, app and driver certification |
| [nedoos-bugs.md](nedoos-bugs.md) | Bugs found in NedoOS (W5300 driver, wizcfg) with proposed fixes |
| [reference-w5300-model.md](reference-w5300-model.md) | W5300 as the ZXNETUSB card uses it: bus, registers, commands, states, FIFO and packet formats, NedoOS checks, where the Unreal_NS model deviates |
| [reference-wizcfg.md](reference-wizcfg.md) | `wizcfg.com` disassembled: presence check, reset, MAC / IP setup, the `#AA` chip test, DHCP exchange |

Why together: NedoOS network programs (e.g. `zxdb.com`) are the main consumers
of the adapters, and the layer's socket view (NK-14) is how we debug them.

Related: [2026-09-17-nedoos-future-support](../2026-09-17-nedoos-future-support/)
(struct catalog, DSL), the ZX-Evo PS/2 keyboard design in the ATM folder.
