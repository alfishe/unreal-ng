# P-18 logind sleep hook (gate)

**Question.** Does a game in SteamOS Game Mode get `PrepareForSleep(true)` through a `sleep` **delay**
inhibitor before the device sleeps? How much time does it get, and does audio come back without a
click? ([architecture.md §7](../../../../docs/inprogress/2026-10-08-steamdeck-client/architecture.md))

**Pass.** The signal arrives every time, at least 100 ms is granted, and there is no click.

## What it does

- Takes `Inhibit("sleep", "p18-logind-sleep", "save emulator state", "delay")` at start and again
  after every wake (libdbus, system bus).
- **On `PrepareForSleep(true)`:** logs, pauses and clears the audio stream, simulates a state write
  (`--save-ms`, default 50), then closes the inhibitor fd.
- **On `PrepareForSleep(false)`:** logs, takes the inhibitor again, clears and resumes audio.
- **Heartbeat thread:** every 10 ms it compares `CLOCK_MONOTONIC` with `CLOCK_BOOTTIME`. A gap
  shows when the process stopped running (frozen by Steam, or asleep) and how long the device slept.
  If the gap starts before the signal arrives, Steam froze the game first, which is a design-relevant
  finding.
- **SDL window / focus events** are logged too.
- `--tone`: a continuous 440 Hz tone, to hear clicks at sleep and wake.

Log: `~/steamdeck-poc-logs/p18-events-*.csv` (monotonic ns, boottime ns, event). The same lines are
shown on screen.

## Run

1. Launch as a non-Steam game with `--tone`. Press Power, wait 10 s, then wake. Repeat 10 times.
2. Sleep overnight (8 h).
3. Repeat with `--save-ms 500` and `--save-ms 3000` to find the delay limit (logind's
   `InhibitDelayMaxSec`, 5 s by default).
4. Repeat with low battery (< 5 %).

## Results

| Date | Deck | Runs | Signal received | Time granted (ms) | Frozen before signal? | Click at wake | Notes |
|------|------|------|-----------------|-------------------|-----------------------|---------------|-------|
| — | | | | | | | |
