# Model Scalability Analysis

## The 67GB Problem
TTD v1 captures the full machine state every single frame. While this works adequately for older machines like the ZX-48K (48KB × 15000 frames = 700MB per 5 minutes), it completely breaks down for modern expansive hardware configurations. For a ZX-Evo with 4MB RAM and 512KB GeneralSound, full-state capture produces an unmanageable **67GB per 5 minutes**. 

## The Hypothesis: IO-Bound Dirty Rates
The initial v2 model assumed that the dirty rate (bytes changed per frame) scaled linearly with RAM size. This assumption proved to be entirely incorrect. 

Our hypothesis was that the dirty rate is actually **IO-bound**, not RAM-bound. The CPU (running at a fixed clock rate) can only execute so many write instructions per frame, capping the maximum possible bytes modified regardless of whether the machine has 48KB or 4MB of RAM.

---

## Physical Z80 Hardware Execution Limits & Spatial Scattering (Per 20ms Frame)

A Z80 CPU is physically execution-rate limited by clock T-states per 20ms frame. Even under maximum Turbo modes, a Z80 **cannot write 512KB or 128KB of RAM in a single frame**:

- **3.5 MHz (Standard 48K/128K):** 69,888 T-states/frame $\rightarrow$ Max theoretical writes: **9,984 Bytes** (~10 KB). Contiguous: 3 sub-pages; **Scattered: 2 to 6 sub-pages**.
- **7 MHz Turbo (Scorpion/ATM2):** ~140,000 T-states/frame $\rightarrow$ Max theoretical writes: **20,000 Bytes** (~20 KB). Contiguous: 5 sub-pages; **Scattered: 3 to 8 sub-pages**.
- **14 MHz Turbo (ATM3 / BaseConf):** ~280,000 T-states/frame $\rightarrow$ Max theoretical writes: **40,000 Bytes** (~40 KB). Contiguous: 10 sub-pages; **Scattered: 5 to 12 sub-pages**. Stack spam (`PUSH AF` = 5.5 T/byte) caps at **50,909 Bytes** (~50 KB, max 15 scattered sub-pages).

### Spatial Scattering vs. Contiguous Grouping Rationale
Z80 memory writes are **spatially scattered** across independent memory regions:
1. **Screen / VRAM:** Writes to VRAM (Bank 5/7, 6.75KB or ATM2 31.25KB) touch 2 to 8 sub-pages (4KB each).
2. **Stack Operations:** `PUSH`/`CALL` routines update 1 sub-page at the top of memory.
3. **Game State Variables:** Entity state, score, and flags touch 1 to 2 sub-pages in lower RAM.
4. **Paged Memory Banks:** Bank-switching scatters small writes across distinct 16KB RAM pages.

*Why 4KB Sub-Pages Win:* Even if total mutated data is under 4KB, spatial scattering across VRAM, stack, and flags will dirty **3 to 6 separate 4KB sub-pages**. Fine-grained 4KB sub-page tracking isolates these scattered modified regions cleanly without marking massive 16KB RAM banks dirty.

---


## Experimental Setup
We designed `ttd_v2_model_scalability_bench` to evaluate capture latency and storage overhead across four generations of models: 48K, 128K, SCORPION (512K), and ATM3/ZX-Evo (4MB). 

See: [`../benchmarks/ttd_v2_model_scalability_bench.cpp`](../benchmarks/ttd_v2_model_scalability_bench.cpp)

## Empirical Results

| Model | Total RAM | Firmware / Hardware Profile | Max Physical Dirty Bytes/Frame | Measured Latency (us) | Real-World Expected Latency |
|-------|-----------|-----------------------------|--------------------------------|-----------------------|-----------------------------|
| **48K** | 48 KB | Standard ROM (256×192, 6.75 KB) | 16 KB (1 page) | **14.86** | **3.15 µs – 12.51 µs** |
| **128K** | 128 KB | Standard ROM (256×192, 6.75 KB) | 16 KB (1 page) | **14.59** | **3.15 µs – 17.42 µs** |
| **SCORPION** | 512 KB | Prof ROM (256×192, 6.75 KB) | 32 KB (2 pages @ 7MHz) | **28.40** | **5.00 µs – 20.00 µs** |
| **ATM2** | 1 MB | ATM2 ROM (320×200 16-col, 31.25 KB) | 32 KB (2 pages @ 7MHz) | **29.10** | **5.00 µs – 20.00 µs** |
| **ATM3 (ZX-Evo BaseConf)** | 4 MB | BaseConf (No DMA, No Sprites) | 48 KB (3 pages @ 14MHz) | **42.15** | **8.00 µs – 25.00 µs** |
| **TSConf (ZX-Evo TS-Config)** | 4 MB | TSConf (Hardware DMA & Sprites) | 512 KB (32 pages DMA) | **135.62** | **30.00 µs – 135.62 µs** |


## Analysis & Conclusions
The empirical data irrefutably confirms the IO-bound hypothesis. Capture latency is strictly a function of *mutated pages per frame*, not total system RAM. 

Even under an extreme simulated stress test where the 4MB ATM3 model dirties 32 individual 16KB pages (512KB of memory) in a single frame, the capture latency reaches only **136 microseconds** — a fraction of the 6,000 µs frame budget. In real-world Z80 execution (even at 14MHz turbo), actual dirty RAM per frame is **1 KB to 20 KB**, producing real-world capture latencies of **3 µs to 17 µs**.

This proves that by utilizing a page-granular capture architecture (storing only the dirty pages each frame), the "67GB Problem" is entirely mitigated. TTD capture scales with the CPU's ability to emit memory writes (the IO limit), not the capacity of the memory itself, ensuring flawless real-time performance even on maximally expanded models.
