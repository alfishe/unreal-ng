#pragma once

/// @file ttdp2666.h
/// @brief TTD serializers for the Pentagon 2.666 LE board (MM_P2666).
///
/// The board's state beyond the model-agnostic TTDChipsetState, in four blobs:
/// - P2666Board (71): the FPGA's latches (P2666State: #xx77, the window registers, the palette words, the DOS
///   latches, the INT pulse), the ATM text address, the CMOS RAM and its address, the MCU's copy of cells #00..#3F.
/// - P2666Sound (72): both YM2149 cores, their register files and address latches, the selected chip, Covox, the
///   beeper bits, the generators' time base.
/// - P2666BetaDisk (73): the RAM-disk Beta Disk emulator (betadisk.vhd); its disk bytes live in RAM pages #76 down
///   and ride with the RAM. Only when the RAM disk is the Beta Disk ([P2666] BetaDisk=ramdisk, the default).
/// - P2666Uart (74): the 16550-like UART with its FIFOs and the line in flight.
/// The MCU clock rides as Ds12887 (18), the SD card as EvoSdCard (15).
///
/// Every blob is written and read by the same field walk (P2666Codec), so save and load cannot disagree; a blob
/// starts with its version byte. Derived host values (the palette as host colours, the bright border bit, the
/// UART's INT line) are recomputed on load.

#include "debugger/ttd/ttdserializable.h"

#include <cstddef>
#include <cstdint>

class PortDecoderP2666;

namespace ttd {

class TTDP2666Device : public TTDSerializable
{
public:
    enum class Part : uint8_t
    {
        Board,
        Sound,
        BetaDisk,
        Uart,
    };

    TTDP2666Device(PortDecoderP2666& decoder, Part part);

    size_t TTDStateSize() const override { return _size; }
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override;
    TTDDeviceType TTDType() const override;
    uint64_t TTDHashState() const override;

private:
    PortDecoderP2666& _decoder;
    Part _part;
    size_t _size = 0;
};

}  // namespace ttd
