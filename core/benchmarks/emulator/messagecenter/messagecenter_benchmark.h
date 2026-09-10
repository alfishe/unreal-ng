#pragma once

// Message Center Benchmarks
//
// Tests the FastEventQueue pub/sub system used for emulator notifications.
// Uses synchronous inline dispatch for lowest latency.
//
// Patterns tested:
//   Frame refresh      - ~50Hz video/audio notifications (hot path)
//   Multi-instance     - Videowall: N emulators posting to shared topic
//   Observer fanout    - Single post dispatched to N subscribers
//   Debug stepping     - Rapid CPU step notifications (instruction trace)
//   Payload allocation - Overhead of different payload types
//   Topic registration - Lookup/insert cost
//
// Run:
//   ./core-benchmarks                              # all benchmarks
//   ./core-benchmarks --benchmark_filter=BM_Frame  # filter by name
//
// See core/benchmarks/README.md for results and analysis.
