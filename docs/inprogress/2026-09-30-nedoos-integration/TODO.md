# TODO: NedoOS integration

Index: [README.md](README.md).

## Done

- 2026-09-30: network adapter catalog, W5300 model reference, wizcfg analysis,
  network TDD.

- 2026-09-30: requirements for the NedoOS layer (NK-1..NK-20).
- 2026-09-30: live trace of `zxdb.com` on the full card: it polls port `#03AB`
  (Wiznet W5300 family, not emulated) and hangs on "Sending request...".
- 2026-09-30: requirements checked live with a Python prototype over the
  WebAPI (tasks, page owners, pipes, sockets, open files, call trace, a direct
  kernel call, ending the stuck call); findings in the kernel reference, new
  NK-21..NK-23. The prototype is POC
  [020-nedoos-layer](../../../tools/poc/020-nedoos-layer/).

## Remaining

- Network adapters: N0, N1a, N1b done on branch `network-w5300`
  ([tdd-network.md](tdd-network.md) §15); A/B benchmark: no measurable change; zxdb
  checked live against the real server. Open there: card INT to the Z80. Next: N2 COM port, N3 ESP modules (ESPNET,
  then AT), N4 ATM2 COM, N5-N6 the rest; debugging per
  [tdd-network-debugging.md](tdd-network-debugging.md) later.
- Answer the open questions in the requirements (§5), then a design for the layer.
- Emulator prerequisites (NK-23): both ATM register sets in the paging state,
  page-qualified breakpoints on WebAPI / MCP, SD / IDE sector reads.
- Bug, separate fix: `GET /memory/ram/{page}/{offset}` with a negative offset
  returns host process memory.
- Implementation: layer core, surfaces, tests; adapters in priority order.
- Side note: AVR hard reset from the PS/2 stream (Ctrl-Alt-Del, F12,
  PrintScreen) and the hard vs. soft reset split (hard reset clears the PS/2
  log) are not emulated.
