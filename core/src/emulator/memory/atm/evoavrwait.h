#pragma once

#include <cstdint>

class EmulatorContext;

/// The ZX-Evo AVR's answer to a /WAIT port, shared by every port it serves.
///
/// On both ZX-Evo configurations the FPGA holds the Z80 on /WAIT for a COM port access (#xxEF) and for a Gluk clock
/// data access (#BFF7, #BEF7 in shadow), and raises the AVR's INT6 for either (TS-Conf fpga/current/z80/zwait.v:31-43
/// wait_status_glu / wait_status_com, BaseConf fpga/base/z80/zwait.v:57-67 waits[0] / waits[1]). One firmware routine,
/// zx_wait_task, serves both from the AVR's main loop, so both ports share the AVR's time: an access waits for the
/// interrupt, for the main loop to reach its next look at the flag, and for the service up to the release (the CS edge
/// after the SPI #40 transfer). Research: reference-evo-com-port.md §3 (docs/inprogress/2026-09-30-nedoos-integration).
///
/// The model keeps where the main loop is: a pass starts again when the AVR is done with the previous access - at the
/// Z80's release, or later when the firmware has more work after it (a Gluk write to the I2C clock or the EEPROM) -
/// and when the AVR's EEPROM finishes its last write (the next EEPROM access busy-waits for it).
class EvoAvrWait
{
public:
    /// The firmware's main loop as the wait sees it (Uart16550::Params carries the presets)
    struct Timing
    {
        uint32_t avrClockHz = 11059200;  ///< the ATmega128's crystal (Q2 11.059 MHz)
        uint16_t isrCycles = 37;         ///< INT6: the wait flag noted
        uint16_t loopCycles = 260;       ///< one main-loop pass
        uint8_t waitChecksPerLoop = 1;   ///< flag tests per pass (BaseConf 1, TS since 2016-03: 8)
    };

    /// What the access does with the AVR's EEPROM (the cell's work busy-waits for the last EEPROM write)
    enum class Eeprom : uint8_t
    {
        None,
        Read,    ///< read_eeprom before the release
        Write,   ///< write_eeprom after the release: starts an 8.5 ms write
    };

    /// TTD state (fixed size)
    struct State
    {
        uint64_t loopResume;      ///< AVR cycle (absolute) the main loop resumed its pass after the last access
        uint64_t eepromReadyAt;   ///< AVR cycle (absolute) the EEPROM's last write ends
    };

    /// The ATmega128's EEPROM write time (datasheet: 8448 cycles of the calibrated 1 MHz RC oscillator, 8.5 ms),
    /// independent of the crystal
    static constexpr uint32_t kEepromWriteMicros = 8500;

    void SetTiming(const Timing& timing) { _timing = timing; }
    const Timing& GetTiming() const { return _timing; }

    /// One access: AVR cycles the Z80 waits = the interrupt + the AVR still busy with the previous access + the main
    /// loop reaching its flag test + `service` (to the release); then the AVR works `after` more cycles before its
    /// loop resumes. `now` in base-clock T-states at `baseClockHz`
    uint32_t Access(uint32_t service, uint32_t after, uint64_t now, uint32_t baseClockHz, Eeprom eeprom = Eeprom::None);

    /// AVR cycles in CPU clocks at `cpuHz` (rounded up)
    uint32_t ToCpuClocks(uint32_t avrCycles, uint64_t cpuHz) const;

    /// Absolute base-clock T-states now, and the CPU's clock at its current speed
    static uint64_t BaseNow(const EmulatorContext* context);
    static uint64_t CpuHz(const EmulatorContext* context);
    /// Hold the Z80 on /WAIT for `avrCycles` of this AVR (turbo waits longer in T-states)
    void HoldCpu(EmulatorContext* context, uint32_t avrCycles) const;

    void Reset() { _state = State{}; }
    const State& GetState() const { return _state; }
    void SetState(const State& state) { _state = state; }

private:
    Timing _timing;
    State _state{};
};
