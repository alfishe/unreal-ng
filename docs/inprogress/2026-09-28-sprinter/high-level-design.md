# Sprinter Sp2000 — high-level design

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Status** | Review round 1 done (2026-09-28); decisions D1-D11 below |
| **Details** | [technical-design.md](technical-design.md) (index of the per-area designs), [unreal-ng-mapping.md](unreal-ng-mapping.md) |

## 1. The idea in five sentences

The Sprinter is emulated as **one decoder class that owns the PLD state**: the internal
registers ("cells" `#C0-#FF`), the port-table lookup into RAM page `#40`, and the dispatch of
each internal code to a device. Memory mapping, the video shadow writes and the accelerator are
a `SprinterMemory` subclass plus three generic hooks that TSConf also needs (write intercept, M1
hook, interrupt source). Video is a new renderer that reads the mode table and palettes from
video RAM each line. Storage reuses the shared pieces: WD1793, the IDE core (AtaChannel ×2 with a
Sprinter adapter) and the media manager with a Sprinter boot profile for folder volumes. The
Z84C15's own devices (SIO, CTC, PIO) become a small reusable `io/z84c15` package.

## 2. Components

```mermaid
flowchart TB
    subgraph CPU["Z80 core (existing) + Z84C15 on-chip"]
        Z80["Z80<br/>M1 hook, clock ×6"]
        SIO["Z84Sio<br/>A: keyboard, B: mouse"]
        CTC["Z84Ctc"]
        PIO["Z84Pio (register file)"]
        SYS["system regs EE/EF, WDT"]
    end

    subgraph PLD["PortDecoder_Sprinter (owns the PLD state)"]
        DCP["DcpLookup<br/>index → page #40 byte"]
        CELLS["PldCells #C0-#FF<br/>pages, 1FFD, 7FFD, CNF, RGMOD, PORT_Y"]
        DISP["Code dispatch<br/>#10-#9F devices"]
        CFG["PldConfigLoader<br/>bitstream sink, fast start"]
        REG["SprinterPldConfiguration registry<br/>Standard, Game (2026-10-03);<br/>DooM / Video later"]
    end

    subgraph MEM["SprinterMemory : Memory"]
        MAP["window mapping<br/>RAM, vROM, ROM, fast RAM, ISA"]
        SHADOW["write intercept<br/>VRAM shadow, graphics pages"]
        ACC["SprinterAccelerator<br/>256-byte buffer"]
    end

    subgraph VID["Video"]
        VRAM["VideoRam 256 KB"]
        REND["ScreenSprinter<br/>mode table, palettes"]
        INT["SprinterIntSource<br/>INT from mode bytes"]
    end

    subgraph IO["Peripherals"]
        FDC["WD1793 (existing)<br/>+ density"]
        IDE["IdeAdapterSprinter<br/>latch, channel select"]
        ATA["AtaChannel ×2<br/>(shared IDE core)"]
        RTC["Ds12887 CMOS<br/>(shared RTC)"]
        AY["AY (existing)"]
        CBL["CovoxBlaster"]
        KBD["Keyboard matrix (existing)<br/>+ AT scan codes"]
    end

    MM["MediaManager (PLAN #58)<br/>fdd.a-d, ide0/1.*"]

    Z80 -- "IN/OUT" --> DCP
    Z80 -- "IN/OUT #10-#1F, #EE-#F4" --> SIO
    DCP --> DISP
    CFG -- "hashes after loading" --> REG
    REG -.->|"overrides: port codes"| DISP
    REG -.->|"overrides: mapping"| MAP
    REG -.->|"overrides: renderer, INT"| REND
    REG -.->|"overrides: accelerator"| ACC
    DISP --> CELLS
    DISP --> FDC
    DISP --> IDE
    DISP --> RTC
    DISP --> AY
    DISP --> CBL
    DISP --> KBD
    CELLS --> MAP
    CELLS --> REND
    IDE --> ATA
    Z80 -- "memory" --> MAP
    MAP --> SHADOW
    SHADOW --> VRAM
    Z80 -- "M1 opcode" --> ACC
    ACC --> MAP
    VRAM --> REND
    VRAM --> INT
    INT --> Z80
    KBD --> SIO
    MM --> FDC
    MM --> ATA
```

| Component | New / reused | Where it lives |
|---|---|---|
| `PortDecoder_Sprinter` | new | `core/src/emulator/ports/models/portdecoder_sprinter.{h,cpp}` |
| `SprinterPldConfiguration` (interface + registry), `SprinterPldStandard` (the first module) | new | `core/src/emulator/ports/models/sprinter/` |
| `SprinterMemory` | new, `Memory` subclass (the `ScorpionMemory` precedent) | `core/src/emulator/memory/sprinter/` |
| `SprinterAccelerator` | new | `core/src/emulator/memory/sprinter/` |
| `ScreenSprinter`, `SprinterIntSource` | new | `core/src/emulator/video/sprinter/` |
| `Z84Sio`, `Z84Ctc`, `Z84Pio`, `Z84SystemRegs` | new, reusable | `core/src/emulator/io/z84c15/` |
| `IdeAdapterSprinter` | new, on the shared IDE core | `core/src/emulator/io/hdd/adapters/` (IDE design §5) |
| `Ds12887` | the shared MC146818 core, extracted from the ATM3 `CMOS` before the Sprinter (PLAN #60) | `core/src/emulator/io/rtc/` |
| `CovoxBlaster` | new | `core/src/emulator/sound/sprinter/` |
| WD1793, AY, beeper, keyboard matrix, media manager | reused | existing locations |
| Generic hooks: write intercept, interrupt source (TSConf, PLAN #41); clock ratio, wait-state hook, per-model `Screen` (PLAN #60) | new shared infrastructure, landed before the Sprinter | `memory/`, `cpu/`, `video/` |

## 3. Port access: one lookup, then a device

```mermaid
flowchart LR
    A["Z80 IN/OUT<br/>address, data, R/W"] --> B{"Z84C15 own port?<br/>#10-#1F, #EE, #EF,<br/>#F0, #F1, #F4 (8-bit)"}
    B -- yes --> C["Z84Sio / Z84Ctc /<br/>Z84Pio / system regs"]
    B -- no --> D["build index:<br/>CNF, PN5, /DOS, /WR,<br/>A15 A14 A6 A5 A13 A7 A2 A1 A0"]
    D --> E["code = RAM[#40][index]"]
    E --> F{"code"}
    F -- "#00" --> G["no port: read #FF"]
    F -- "#10-#1E" --> H["WD1793, joystick,<br/>density, ISA, CMOS"]
    F -- "#20-#2E" --> I["IDE adapter, frame,<br/>PLD reload"]
    F -- "#40-#91" --> J["keyboard, AY, mouse,<br/>Covox-Blaster, ROM page"]
    F -- "#C0-#FF" --> K["PLD cells: 1FFD, 7FFD,<br/>border, pages, CNF..."]
    K --> L["remap windows,<br/>video mode"]
```

The lookup is one byte read from a host array plus a switch: cheaper than the predicate chains of
the other decoders. The port-trace and breakpoint surfaces tag each access with the **code** as
well as the address, so a trace line reads `OUT #21BC ← #21 [code #2B IDE select primary]`.

## 4. Boot

```mermaid
sequenceDiagram
    participant H as Host (create machine)
    participant L as PLD loader (ROM page 12)
    participant P as PortDecoder_Sprinter
    participant B as BIOS (ROM)
    participant D as Disk slot
    participant S as DSS loader + SYSTEM.DOS

    H->>P: reset: PLD unconfigured, loader visible
    alt fast start (default for tests)
        P->>P: jump straight to "configured", reset
    else full start
        L->>P: 59 215 bytes × 8 writes (bitstream sink)
        P->>P: hash, look up the configuration module, configured, reset
    end
    B->>P: fill page #40 through window 3
    B->>P: IN A,(SLOT3): port decoder opens
    B->>B: POST, CMOS, IDE detect, logo
    B->>D: DRV_READ LBA 1 (boot drive from CMOS #10)
    D-->>B: "Starting..." + loader
    B->>S: copy to #8000, jump #800C
    S->>D: LBA 2-3, MBR / BPB, root directory
    S->>S: SYSTEM.DOS into a page, init, EXEC SYSTEM.EXE
```

## 5. Memory write path

```mermaid
flowchart TB
    W["CPU write (addr, value)"] --> M["normal store through _bank_write<br/>(RAM, or trash page for ROM / vROM)"]
    M --> Q{"bank has the<br/>intercept flag?"}
    Q -- no --> Z["done (all other models)"]
    Q -- yes --> R{"what is mapped"}
    R -- "page #50-#5F" --> G["graphics address<br/>PORT_Y × 1024 + A[9:0];<br/>bit 3: skip #FF; bit 2: VRAM only"]
    R -- "Spectrum screen area,<br/>ALL_MODE bit 0 = 0" --> Z2["Spectrum shadow:<br/>VRAM address across lines"]
    R -- "page #A0 and 1FFD = #10" --> RS["soft reset"]
    R -- "page #D0-#DF, 1FFD bit 4" --> ISA["ISA write (ignored in v1)"]
    G --> V["VideoRam write<br/>+ mode/palette dirty"]
    Z2 --> V
    V --> I["mode byte changed?<br/>→ recompute INT list"]
```

The graphics pages also change the **read** path (reads come from main RAM at the video
address); `SprinterMemory` overrides the virtual read pair for those banks only.

## 6. Storage

```mermaid
flowchart LR
    subgraph Guest
        BIOS["BIOS disk API<br/>RST #08 fn #5x"]
        DSS["DSS drivers"]
        TR["TR-DOS 5.04Em"]
    end
    subgraph Ports
        FD["codes #10-#17<br/>WD1793 + density"]
        ID["codes #20-#2B<br/>IDE + channel select"]
    end
    subgraph Emulator
        WD["WD1793"]
        AD["IdeAdapterSprinter<br/>A8 latch, channel latch"]
        C0["AtaChannel 0<br/>primary"]
        C1["AtaChannel 1<br/>secondary"]
    end
    subgraph Media["MediaManager"]
        FA["fdd.a-d<br/>TRD / SCL / PC .img"]
        I0["ide0.master / slave"]
        I1["ide1.master / slave"]
        HF["HostFolderFat FAT16<br/>+ Sprinter boot profile"]
    end
    BIOS --> FD
    BIOS --> ID
    DSS --> FD
    DSS --> ID
    TR --> FD
    FD --> WD
    ID --> AD
    AD --> C0
    AD --> C1
    FA --> WD
    I0 --> C0
    I1 --> C1
    HF --> I0
```

## 7. Key decisions

| # | Decision | Alternatives rejected |
|---|---|---|
| D1 | Port decode **from page `#40`** (as hardware and MAME) | hard-coded standard map (ZXMAK2): breaks programs that edit the table and the four maps |
| D2 | PLD configuration: run the ROM loader into a **bitstream sink** that counts the real bitstream (59 215 bytes × 8 writes, watchdog timeout) and identifies it by two hashes (the first 4 096 writes, MAME-compatible, and the full stream); the hash selects a **configuration module** (D11). v1 models only the standard Sp2000 configuration. Full start is the user default; **fast start** (`FastStart=1`) is the test default | emulating the PLD (closed format); always skipping the loader (hides the reload path the BIOS uses for setup and games); MAME's 4 096-write end of load (ends in the middle of the stream) |
| D3 | Clock: generalize the Z80 frequency multiplier from a power-of-two shift to a small integer ratio (1, 2, 4, 6) | a Sprinter-only CPU loop |
| D4 | Turbo waits: MAME's "align to 6" model behind one function, replaceable when measured | cycle-exact PLD timing (no measurements) |
| D5 | Video: per-line renderer reading the mode table from VRAM (no caching of decoded tiles in v1) | MAME's tilemap caches: faster but complex invalidation; can come later as an optimization with a benchmark |
| D6 | INT from the mode table, recomputed when a mode byte with the blank/INT pattern changes, served through the shared `IInterruptSource` (TSConf §3.4) | fixed `intstart` from the ini: the BIOS moves INT at run time |
| D7 | Keyboard: one host key event feeds both the ZX matrix and AT set-2 scan codes (shared with ZX-Evo PS/2, PLAN #55 E2b) | two separate input paths that can disagree during TTD replay |
| D8 | Folder volumes boot DSS through a **Sprinter boot profile** in the folder builder: MBR entry 0 type `#06`, 3 reserved sectors at LBA 1-3 filled with the DSS loader taken from `BOOT.EXE` (from the folder, or a configured file) | patching the BIOS; requiring the user to run `BOOT.EXE` on a session volume every time |
| D9 | Snapshots: a native Sprinter state file only; no Spectrum formats | squeezing Sprinter state into `.sna`/`.z80` |
| D10 | Internal codes appear in port traces, breakpoints and the debugger | address-only traces (useless when the map changes) |
| D11 | PLD configurations are **modules** behind one extension point, `SprinterPldConfiguration`. A module is identified by a descriptor {name, full-stream hash, first-4 096-writes hash} and overrides only what its firmware changes: (1) port decoding (own codes, own cells), (2) memory mapping, (3) video renderer + INT source, (4) accelerator; everything else falls through to Standard. Standard is itself the first module. Unknown bitstream → Standard + a warning with the hash. Module id and state are TTD/snapshot state. v1 ships Standard only | one hard-coded configuration with a `configId` switch (every new firmware edits the decoder core); a whole separate machine per configuration (duplicates the board hardware they all share) |
