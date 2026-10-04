// libsam2695 - internal-rate to output-rate resampler (the render layer, never chip state).
//
// Kaiser-windowed sinc, polyphase (256 phases, linear interpolation between neighboring phases), at
// an exact rational step: the output position advances by inRate / outRate kept as an integer
// numerator over outRate, so it never drifts. Upsampling uses 32 taps; downsampling widens the kernel
// in proportion and lowers the cutoff below the output Nyquist. Equal rates bypass it (bit-exact copy).
// Every buffer is sized by Reserve(); Configure() for any rate up to that size does not allocate.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sam2695
{

class Resampler
{
public:
    void Reserve(uint32_t inRate, uint32_t minOutRate);
    void Configure(uint32_t inRate, uint32_t outRate);
    void Reset();

    // Consumes interleaved stereo input as needed, writes up to maxOut frames. Returns frames written;
    // `consumed` receives the input frames taken.
    size_t Process(const float* in, size_t inFrames, size_t& consumed, float* out, size_t maxOut);

    bool Bypass() const { return _bypass; }
    int Taps() const { return _taps; }

private:
    static constexpr int kPhases = 256;
    uint32_t _inRate = 0, _outRate = 0;
    bool _bypass = true;
    int _taps = 0;
    std::vector<float> _kernel;  // (kPhases + 1) x _taps
    std::vector<float> _history; // 2 x _taps stereo frames (each frame written twice for contiguous reads)
    uint32_t _write = 0;         // next history slot
    uint64_t _num = 0;           // output phase numerator over _outRate
    uint32_t _pending = 0;       // input frames to take before the next output
};

} // namespace sam2695
