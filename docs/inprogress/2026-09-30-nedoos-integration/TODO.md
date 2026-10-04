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

- MoonSound under NedoOS: the release (2026-10-04) has no OPL4 player; `ngsplay.com` is the NeoGS MOD/S3M/MP3 player (plays MOD on ATM3 + NeoGS in the emulator). The MoonSound pack (MoonBlaster `.MWM`, OPL `.VGZ`) needs a player written or ported; decision pending with the owner.
- "SD card lost" with the whole release as the `sd.zc` folder (`nedogame/`, 6245 files): bisect by file count / size, find whether `HostFolderFat` or the NedoOS FAT reader is at fault ([overview](nedoos-overview-and-release.md)).

- Network adapters ([tdd-network.md](tdd-network.md) §15): N0, N1a, N1b (ZXNETUSB / W5300 +
  virtual network) and the card INT on master; N2 COM port on master
  ([reference-evo-com-port.md](reference-evo-com-port.md)); N3 ESP modules (ESPNET 1.27 and AT)
  on master 8237edc2 ([reference-esp-modules.md](reference-esp-modules.md)), zxdb live over
  both. Also on master: settings per slot from the machine's capabilities
  (`PortDecoder::DescribeNetwork`; `Card=` a list with `ZXWIFI`, `ZxWifi=`, `ComFlavor=` gone),
  the ZX-Evo AVR UART always present, every AVR firmware release as `[EVO] Avr=`, the /WAIT
  model ISR + loop phase + service ([reference-evo-com-port.md](reference-evo-com-port.md) §3, §9).
  Open: Moon Rabbit / Karabas net-tools check, the ZX-Evo AVR FIFO question
  (reference-esp-modules.md Part 4); the AVR version string (Gluk cells #F0..#FF) still
  says "ZXEvo 4M" 07.01.2026 whatever `Avr=` picks (the old releases' tags and CRCs are not
  in the sources); the ZiFi API under a TS firmware on BaseConf (N5). The Qt Network window
  (Tools > Network, Ctrl+5: settings built from the machine's capabilities, live state tree)
  is on master 157bc4fae; settings changed there or by automation are not written back to the INI. N4 (ATM2 COM
  through the keyboard controller, the ATM2IOESP card) done 2026-10-02; N5 ZiFi: the TS AVR side with
  `ZiFi=AT` done 2026-10-02 ([2026-10-02-tsconf-zifi](../2026-10-02-tsconf-zifi/TODO.md)), the native
  protocol, DMA and the ESP-AT 2.2.x dialect next; N6 AY-UART open; debugging per
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
- Postponed (hardware on the desk): test a real ESP on USB through `SERIAL:` (ESPNET
  and AT firmware, auto-reset adapters), sources in
  [reference-evo-com-port.md](reference-evo-com-port.md) §8; alongside the Greaseweazle /
  KryoFlux bridge ([PLAN.md](../PLAN.md) #12).
