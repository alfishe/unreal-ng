# Peripheral Serialization Cost

## The Challenge
While core machine state (CPU, RAM) can be captured efficiently via page-granular storage, high-fidelity peripherals present a variable cost. Small peripherals (AY-3-8910 at 32 bytes) are trivial, but massive optional peripherals like the GeneralSound (512KB SRAM) could easily violate our strict 5-6ms per-frame latency budget if serialized naively.

## Experimental Setup
We modeled the TTD overhead for peripherals ranging from 256 bytes (basic registers) up to 512KB (GeneralSound). We benchmarked both the full capture logic and the restoral logic (critical for seeking).

See: [`../benchmarks/ttd_v2_peripheral_cost_bench.cpp`](../benchmarks/ttd_v2_peripheral_cost_bench.cpp)

## Empirical Results (Latency)

| Operation | AY (256B) | Typical (4KB) | FDC/Cache (32KB) | GS SRAM (512KB) |
|-----------|-----------|---------------|------------------|-----------------|
| Capture   | **0.005 µs** | **0.057 µs** | **0.507 µs** | **9.5 µs**      |
| Restore   | **0.005 µs** | **0.056 µs** | **0.472 µs** | **8.7 µs**      |

## GeneralSound Hardware Access Dynamics

A critical physical hardware distinction must be made regarding GeneralSound SRAM access:

1. **Audio Playback (Zero SRAM Mutation):**  
   When GeneralSound plays MOD tracks or audio samples, its onboard Z80 CPU only **READS** sample bytes from SRAM and sends them to DAC channels. Memory reads perform **0 bytes of RAM mutations** per frame!
2. **Host I/O Port Capture (<4 Bytes/Frame):**  
   During playback, the host ZX Spectrum Z80 sends only minor playback trigger/control commands across ports `#B3` / `#33` (e.g. "start sample #2, set volume #15"), totaling **<4 bytes per frame**.
3. **Sample Uploads Only (Host $\rightarrow$ GS SRAM):**  
   GS SRAM changes *only* when the host Z80 explicitly transfers new sample data blocks into GS RAM. During sample uploads, page-store 4KB sub-page diffs track the modified memory chunks cleanly.

---

## Analysis & Conclusions
1. **Audio Playback Overhead is Zero:** Because playing audio consists purely of DAC memory reads, GeneralSound audio playback generates **0 bytes of dirty SRAM per frame** ($0.00\ \mu\text{s}$ SRAM capture overhead).
2. **Host Port Logging:** TTD captures host-to-GS I/O port commands (<4 bytes/frame) to maintain deterministic playback state.
3. **Sample Upload Paging:** When the host Z80 loads new sample data into GS SRAM, 4KB sub-page tracking captures only the modified sample blocks ($3.15\ \mu\text{s}$ capture latency), avoiding unnecessary 512KB snapshot overhead.

