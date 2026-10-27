# Wild Sound ISA card (Sprinter)

**Started:** 2026-10-09 · branch `wildsound-card` · status: W1, W2 and W4 built; W3 (test data, PT3 player) in progress

Robus's Wild Sound card in a Sprinter ISA slot: the STM32 "AYX-32 / Wild Sound II" firmware's bus protocol,
two AY-3-8910 and an XM player that plays what the Z80 uploads. The one Sprinter program for it,
`DEMOS/WILDSND/prosiak.exe`, was taken apart first
([prosiak-wildsound](../../disasm/software/sprinter/prosiak-wildsound/README.md)); the author's firmware and
tools are public (github.com/RobusAsmCoder/Wild-Sound, read into [reference-firmware.md](reference-firmware.md)).

| Document | What |
|:--|:--|
| [requirements.md](requirements.md) | goals, requirements with confidence, acceptance (several XM / S3M / PT3 tracks play) |
| [design.md](design.md) | core + ISA adapter, bus pipeline, streams and storage, FT2 replayer, AY pair, `ICardAudio`, TTD, tools, test data, tests, phases |
| [reference-firmware.md](reference-firmware.md) | the firmware, read from source (2021-04-23 build): bus protocol, stream commands, upload formats, its XM player and AY with their deviations, test music in the author's repository |
| [research-software.md](research-software.md) | software, music and documentation found online and locally |
| [TODO.md](TODO.md) | work list by phase |

Owner decision 2026-10-09: the card plays **as intended** (FT2 semantics, correct AY), not with the firmware's
bugs.

Related: the MASON music format of the same author, [docs/file-formats/music/mason.md](../../file-formats/music/mason.md);
the ISA bus design, [2026-10-02-sprinter-isa](../2026-10-02-sprinter-isa/tdd.md); the peripherals survey entry,
[peripherals-survey.md](../2026-09-28-sprinter/peripherals-survey.md) item 3 / Q3.
