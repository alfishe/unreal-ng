# Behavioral Analysis & Fingerprinting Heuristics

This document outlines the pipeline for transforming raw memory access counters into semantic fingerprints for procedures.

## 1. Spatial Pattern Detection (Clusters)
By observing contiguous memory regions accessed by the same code (`source_pc`), we can define **Access Clusters**.
- **Sequential**: High density, low variance in access (e.g., `memcpy`, screen clearing).
- **Sparse**: Low density, specific offsets (e.g., sprite rendering, font retrieval).
- **Random**: Non-linear access (e.g., complex data structure traversal).

## 2. Temporal Pattern Detection
Understanding *when* and *how often* memory is accessed reveals the role of the routine.
- **Periodicity**: Routines called every frame (50Hz) or 2nd frame (25Hz) are typically **Music Players** or **Refresh Effects**.
- **One-Shot**: Routines that run for thousands of cycles and then never again are likely **Decompressors** or **Initializers**.

## 3. Routine Classification Profiles

### Music Player
- **Features**: Highly periodic (frame-synced), sequential read from data region, writes to AY-3-8910 I/O ports (0xFFFD, 0xBFFD).
- **Heuristic**: If `is_periodic == true` AND `writes_io == {0xFFFD}`.

### Decompressor
- **Features**: Non-periodic, high read/write ratio (1:1 or 1:4), reads from one cluster, writes sequentially to another (destination).
- **Heuristic**: If `num_regions >= 2` AND `is_periodic == false` AND `total_accesses > 5000`.

### Screen Effect (Raster/Plasma)
- **Features**: Periodic, writes to screen memory (`0x4000-0x5B00`), often uses same destination addresses every frame.
- **Heuristic**: If `writes_screen == true` AND `is_periodic == true`.

### Sprite Masking / Blitting
- **Features**: Reads from two distinct clusters (sprite data + mask), performs logical operations (AND/OR), writes to screen cluster.
- **Heuristic**: If `num_source_regions >= 2` AND `logical_ops_detected == true` AND `writes_screen == true`.

### Scroll Routines
- **Features**: Intensive read/write within screen memory or intermediate buffer, shifting data by fixed offsets.
- **Heuristic**: If `is_sequential_rw == true` AND `addr_delta == 1 or 32` AND `target_region == screen`.

### Pure Calculation / Math
- **Features**: High execution count, frequent math operations, reads from constant tables (sine/log), no I/O or direct screen access.
- **Heuristic**: If `execution_to_io_ratio == high` AND `internal_data_access_only == true`.

## 4. Pipeline Flow
1. **Raw Collection**: Update `MemoryAccessCounters` in the emulator hot path.
2. **Cluster Detection**: Group addresses by `source_pc`.
3. **Temporal Analysis**: Calculate `access_period` across frame history.
4. **Feature Extraction**: Build a multidimensional vector (reads, writes, I/O, span, density).
5. **Adaptive Classification**: Use a decision tree or Bayes-lite approach to assign a `RoutineType` with a confidence score.
