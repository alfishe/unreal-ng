# ZX-bus slots: open questions for the owner

| | |
|---|---|
| **Date** | 2026-10-03 |
| **For** | the ZX-bus slot design (PLAN row #82); first consumer: [ZX-MultiSound](../2026-10-03-zx-multisound/) |
| **Order** | most important first (Q8 added 2026-10-05; Q9, Q10 from SL-6, decided 2026-10-05) |

## Q1. What happens when a card is incompatible with cards already plugged in?

**Owner decision (2026-10-03):**
- **Qt UI:** the new card replaces the incompatible ones, and the user gets a warning that names what was removed.
- **Automation (CLI, WebAPI, MCP, Lua, Python):** by default the request is refused with the reason. With an
  explicit `replaceIfIncompatible` flag the card is plugged in, the incompatible cards are removed, and the reply
  lists what was incompatible and what replaced it.
- One new card can push out several installed cards at once. The design must handle that case.

**Rules the design follows (derived from the decision; to be confirmed in review):**

1. **Compatibility is declared, not hard-coded per pair.** Each card declares the *functions* it occupies
   (`ay-socket` for TS / TSFM, `gs`, `saa`, `soundrive`, `midi`, ...). Two cards that occupy the same function are
   incompatible. The matrix in the design is generated from these declarations.
2. **The claims follow the card's current configuration.** A ZX-MultiSound with its GS switched off by its DIP switch
   does not occupy `gs`, so a separate GS card can stay. Turning the switch back on goes through the same check (and
   the same replace flow) as plugging a card in.
3. **One plan, applied atomically.** Before anything changes, the slot manager computes the whole plan: the union of
   every installed card that clashes with any function of the new card. The plan is applied all at once by restarting
   the machine with the new configuration (Q6), or not at all.
   Example: TSFM + GS + a SounDrive card are installed; plugging in a ZX-MultiSound (TSFM, GS, SAA, SounDrive) removes
   all three in one step.
4. **Lost functions are reported.** Replacing a card that offered more than the new one loses functions. Example:
   swapping the ZX-MultiSound for a plain TSFM loses GS, SAA and SounDrive. The UI warning and the API reply both list
   these lost functions. Nothing is re-added automatically.
5. **Built-in devices are never removed.** A machine function that is part of the board (for example the ZX-Evo's own
   TurboSound) is not evictable. The request is refused even with the flag, unless the machine declares that built-in
   function as switchable. In that case the reply says the built-in function was switched off.
6. **Removing a card releases its media.** A removed card's media slots (for example the NeoGS `sd.ngs` card) follow
   the media manager's eject rules. A medium with unsaved changes blocks the replacement unless the request carries a
   disposition (`save` / `discard`), the same as a media eject.
7. **The reply can be used to undo.** The reply (and the UI warning) carries the full configuration of every removed
   card, so a client can put them back. A `dryRun` request returns the plan without applying it.
8. **Not while TTD records.** The device set is fixed for a TTD session. A replacement while recording is refused
   (the existing session population guard).
9. **Accidental port clashes are a separate rule.** Clashes that the function matrix does not cover keep the earlier
   rule: the clashing card is plugged in but disabled, with the reason, and adding more cards stays possible.

## Q2. What happens to the machine's built-in AY when a bus card answers the same ports?

**Owner decision (2026-10-03): A - model IORQGE faithfully.**

- **Two slot kinds.** The **AY socket** (`ay-socket`) holds the board's own AY, or a TurboSound / TurboSound FM
  plugged in place of the chip (128K, Pentagon, Scorpion), as `[SOUND]` does today. The **ZX-bus slots** hold cards.
- **Shadowing.** A bus card that drives IORQGE on the ports of a built-in device *shadows* it: the built-in device
  stays fitted but neither answers nor sounds. Example: a ZX-MultiSound drives IORQGE on `#FFFD` / `#BFFD` and
  shadows whatever sits in the AY socket, including the ZX-Evo's TurboSound built into the FPGA.
- **Reporting.** The API reply and the UI warning list shadowed devices as `shadowed`, separately from `removed`.
- **Matrix.** "TSFM in the AY socket + ZX-MultiSound" is listed as pointless (the TSFM would be shadowed). The UI puts
  the plain AY back into the socket and warns; the API refuses without `replaceIfIncompatible`.
- **To research:** what the real hardware does on a shared `#FFFD` read (which side drives the data bus), recorded in
  the hardware reference.

## Q3. In what order do existing cards move onto slots?

**Owner decision (2026-10-03): A.** The slot design covers every card from the start, and the compatibility matrix is
complete. Implementation order: (1) slot core, bus declarations per machine, IORQGE and shadowing; (2) existing cards
(GS / NeoGS, MoonSound, Covox / SounDrive, network cards) move over one at a time, each with an A/B benchmark of the
port hot path and a TTD corpus run; (3) the ZX-MultiSound, the first card written for slots from the start. The two
port-claim mechanisms never live side by side, and the MultiSound is built from modules that already sit on slots.

## Q4. What happens to the old INI keys?

**Owner decision (2026-10-03): A.** One `[SLOTS]` section is the single source of truth (key form decided in the
design, for example `ay-socket = tsfm`, `zxbus.1 = multisound`, `zxbus.1.dip = ym,saa,gs,sd`). The old keys (`[GS]` /
`[NGS]`, TSFM in `[SOUND]`, MoonSound, `[NETWORK] Card=`, Covox) are read at load time and translated to slots, with a
log warning "deprecated key X -> slot Y". The shipped configs in `data/configs` move to the new form at once. The
old keys are removed after a few releases. User INI files keep working.

## Q5. Does a card have to physically fit the machine's bus?

**Owner decision (2026-10-03): A, with an override.** The machine declares its bus kind and the signals on it
(NemoBus / ZX-bus: IORQGE, /IODOS, +12 V; the Sinclair edge connector on 48K / 128K / +2 / +3; the Scorpion's own
layout). A card declares the signals it needs. A missing signal is an incompatibility, the same as in the matrix.
An explicit **adapter** is a slot entity of its own (for example "ZX-bus to edge connector"); a card behind an
adapter is shown as such in every report.

- **UI:** explains that this does not work on real hardware and asks the user to confirm an override.
- **Automation:** refuses by default. With the explicit override flag (the same `replaceIfIncompatible` the owner
  named for incompatible cards) the card is plugged in.
- **With the override** the card works logically, as if the bus carried every signal it needs. The slot report
  marks the fit as `unrealistic`, so it is never mistaken for real-hardware behavior.

## Q6. Are cards changed in a running machine?

**Owner decision (2026-10-03): no.** Every slot change is followed by a restart of the machine with the new
configuration ("why look for adventures out of nothing"). No hot plug: cards are created only when a machine starts.
A change goes: plan -> refusal or confirmation -> new configuration written -> restart through the model-switch path
(media carried over by its rules). The General Sound personality switch, which today rebuilds the card at run time,
becomes a slot replace with a restart. A model switch carries the slot set and plans it against the new machine.

## Q7. How is the ZX-MultiSound modeled on the ZX-Evo?

**Owner decision (2026-10-03): A - faithfully, with an empty socket.** Facts from research (research-machines.md §13,
review correction): the ZX-Evo hides its own ports from the slots by masking /IORQ (`porthit`), has no data buffers,
and its AY is one socketed YM2149. The MultiSound ignores /IORQ (it detects I/O cycles as RD / WR without MREQ and M1),
so its writes reach both chips and a `#FFFD` read is driven by both (a bus fight) unless the YM2149 is taken out.

- The ZX-Evo declares its AY as a **socketed, removable** built-in.
- Plugging a MultiSound plans "take the YM2149 out of its socket" and reports it
  (`ay-socket: chip removed - otherwise #FFFD reads are a bus fight`). Qt: warning + done; automation: refused without
  `replaceIfIncompatible`.
- With the override that keeps the chip, reads follow the bus's read rule and the fit is reported as `unrealistic`.
- The TS-Conf `FREE_IORQ` FPGA build stays a possible later machine option (not in this work).

## Q8. What happens when the configured cards conflict with each other?

**Owner decision (2026-10-05): the machine is not created.** Until now an INI whose `[SLOTS]` entries conflicted was
planned "first wins": the later card was left out with a log line and the machine started (R-CFG-3). A config that adds
`zxbus.1 = multisound` to a shipped Pentagon or ZX-Evo config (TurboSound FM or TurboSound in the socket, NeoGS,
SounDrive) then started without the MultiSound, and nothing on screen said why.

- **Rule:** when two configured entries conflict under the compatibility matrix, creating the machine fails with an
  error (HTTP 400 on the WebAPI, the same reason on every surface). The reason lists each conflicting pair, the rule
  and what it says, for example: `the [SLOTS] cards conflict, the machine is not created (Q8): zxbus.2 = gs and
  zxbus.1 = multisound: shares `gs` (D1: one function, one card)`.
- **Conflicts:** a shared function (D1, matrix code **D**), a socket board a card would shadow or fight (D3 / D12,
  **⊘**), an accidental port clash (D7, **P**), and a socketed chip kept by an explicit `ay-socket = ay` while a card
  needs it out of its socket (Q7). This covers every card, not only the MultiSound.
- **Q7 stays:** with no `ay-socket` line the card may take the ZX-Evo's YM2149 out of its socket. `ay-socket = ay`
  together with the MultiSound on the ZX-Evo is now a conflict and refuses the machine. On the Pentagon the board AY
  under the card is shadowed (matrix **S**), as on real hardware: no conflict.
- **Not a conflict between entries:** a card the machine itself cannot take (a fixed built-in holds its function, a bus
  signal is missing, the card is not emulated) is still left out with its reason, and the machine starts.
- **Shipped configs:** every shipped config still creates (`SlotManagerShipped_Test`).

**Owner addition (2026-10-05): the shipped configs do not fit the MultiSound.** No shipped config has a
`zxbus.N = multisound` line. The MultiSound is fitted only in tests (test-local configs, or slot entries the test
builds). A user who wants it writes the line into a config that has no conflicting card, for example without
`ay-socket = tsfm` and without a NeoGS / SounDrive card.

## Q9. What does a model switch do with the new machine's own configured cards?

**Owner decision (2026-10-05): A, merge** (as built in SL-6).

A model switch carries the old machine's cards (Q6, R-OP-9). The new machine's config also names cards: the shipped
Pentagon fits a TurboSound FM, a NeoGS, a MoonSound and a SounDrive; the shipped 48K only a TurboSound FM.

- **A (built in SL-6):** merge. The carried cards come first; the new machine's own cards fill the slots and functions
  they leave free and give way where they conflict. 48K -> Pentagon gives the Pentagon its shipped cards, as before
  SL-6; Pentagon -> ZX-Evo keeps the Pentagon's cards and adds the ZX-Evo's that do not clash. A card the user removed
  from the old machine comes back if the new machine's config fits it (removals are not carried).
- **B:** carry only. The new machine gets exactly the carried cards (planned against it); its own configured cards are
  ignored. 48K -> Pentagon gives a Pentagon with only the TurboSound FM.
- **C:** carry only what the user changed (the difference between the instance's set and its model's config), merged
  with the new machine's config. Needs the instance to remember its changes.

**Recommendation: A.** It keeps a model switch of an untouched machine as it was (the shipped config of the target),
and every card the user plugged in follows it. If removals should follow too, C on top of A later.

## Q10. Does the running machine's General Sound personality switch stay in place?

**Decision (2026-10-05), following Q6 / R-OP-8:** the explicit personality switch on every surface becomes a slot
replace applied by a restart (SL-7); the in-place frame-boundary switch stays only for the `gs_lightweight` feature,
an emulation shortcut that changes no hardware.

R-OP-8 makes the personality switch a slot replace applied by a restart. SL-6 built that (`GeneralSoundRequest` +
`SlotChange::Run`). The surfaces (WebAPI `switch_personality`, CLI `gs`, MCP, Lua, Python, the Qt audio settings) and
the `gs_lightweight` feature still use the frame-boundary switch of the running machine, which keeps the running
program, hands the card's mailbox over and replays an uploaded module; SL-6 routes it through the plan and makes the
plan and the TTD fingerprint follow it.

- **A:** SL-7 moves every surface to the restart (R-OP-8 as written); the running program is lost on a switch, the
  `gs_lightweight` feature becomes a slot replace too.
- **B:** keep the in-place switch for the personality only (one card, one slot, the same function and ports; planned and
  followed as built), the restart for everything else.

**Recommendation: B** for the `gs_lightweight` feature (an emulation shortcut, not a hardware change: restarting the
machine for it would surprise), **A** for the explicit personality switch on the surfaces, so a user-visible card change
behaves like every other slot change.

