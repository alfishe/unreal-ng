#pragma once

#include <cstdint>

/// The Artix-7's own peripherals behind NR #F0 and #F8-#FA on the Issue 4 and 5 boards (zxnext.vhd gen_xper_44 7438-7590,
/// zxnext_top_issue4.vhd 2408-2520): the DNA_PORT (the FPGA's 57-bit unique id, shifted out a bit per read of NR #F0) and the XADC
/// (UG480: the die temperature and supply voltages, read through its DRP with NR #F8 / #F9 / #FA). The Issue 2 board's Spartan-6 has
/// neither: there the registers read 0 and the board does not call this.
///
/// The XADC is a fixed die: 45 C and the nominal supplies on every read, the minimum and maximum the same, so a program's output is
/// the same on every host. It runs the default sequence the core configures (INIT_41 = #00F0), a conversion every couple of
/// microseconds: the sticky EOC / EOS a read of NR #F0 clears are set again before the next read, so they read 1 (BUSY 0)
class NextXilinx
{
public:
    /// The id the DNA_PORT shifts out: the core's SIM_DNA_VALUE (zxnext_top_issue4.vhd 2412); a real board has its own
    static constexpr uint64_t kDna = 0x101100111000111ull;
    static constexpr unsigned kDnaBits = 57;
    /// The first byte the shift register holds after a load: the id's length (zxnext_top_issue4.vhd 2428)
    static constexpr uint8_t kDnaLength = 0x39;
    static constexpr unsigned kDrpRegisters = 128;

    /// The configuration load: the XADC's INIT values (zxnext_top_issue4.vhd 2437-2468) and a soft reset's state
    void PowerOn();
    /// The core's `reset` (zxnext.vhd 7446-7460): back to select mode, both devices off
    void Reset();

    /// NR #F0 as a read returns it (no side effect)
    uint8_t ReadCommand() const;
    /// A program's read of NR #F0 through port #253B (port_253b_rd_fe): the DNA shifts a bit
    void AfterProgramRead();
    void WriteCommand(uint8_t value);

    /// NR #F8: '0' & the DRP address. A write starts a DRP access: bit 7 = 1 writes D1:D0 to the address, 0 reads it into them
    uint8_t ReadAddress() const { return _drpAddress; }
    void WriteAddress(uint8_t value);
    uint8_t ReadData0() const { return _d0; }
    uint8_t ReadData1() const { return _d1; }
    void WriteData0(uint8_t value) { _d0 = value; }
    void WriteData1(uint8_t value) { _d1 = value; }

    /// A DRP register as the XADC answers it: the status registers (#00-#3F) are the fixed die, the control registers (#40-#7F) what
    /// was written
    uint16_t DrpRead(uint8_t address) const;

private:
    friend struct NextStateCodec;  // the TTD state codec (nextstatecodec.cpp)
    bool DnaBit() const;

    bool _select = true;       ///< nr_f0_select: reads return the enables
    bool _dnaEnabled = false;  ///< nr_f0_xdna_en
    bool _adcEnabled = false;  ///< nr_f0_xadc_en
    uint8_t _dnaOut = 0;       ///< xdna_out: the length byte, then the id bits as they shift in
    uint8_t _dnaShifted = 0;   ///< the DNA_PORT bits shifted out since the last load
    bool _drpWrite = false;    ///< nr_f8_xadc_dwe
    uint8_t _drpAddress = 0;   ///< nr_f8_xadc_daddr
    uint8_t _d0 = 0, _d1 = 0;  ///< nr_f9_xadc_d0 / nr_fa_xadc_d1
    uint16_t _control[kDrpRegisters / 2] = {};  ///< DRP #40-#7F
};
