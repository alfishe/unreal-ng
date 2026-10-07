#pragma once

// Hardware source: the ZX-Evo FPGA / AVR / ERS sources of https://github.com/alfishe/pentevo at commit c24723db
// (project home https://github.com/tslabs/zx-evo, folder pentevo), taken from the public repository, not a local edit;
// the timing rules were run in Verilator. Pinned revisions, links and what was simulated versus read:
// docs/inprogress/2026-09-15-atm-baseconf-highres-ports/sources-and-provenance.md

/// @file evofontoverlay.h
/// @brief ZX-Evo BaseConf font RAM loader: `#BF` bit 2 mirrors every memory write into the text-mode font RAM
/// (docs/inprogress/2026-09-15-atm-baseconf-highres-ports/tdd-e8-wrprot-font-pal444-dosstall.md section 3.2).
///
/// RTL: `fnt_wr = fntw_en_reg && mem_wr_fclk`, font address = the low 11 address bits. Any memory write counts, ROM
/// windows and write-protected windows included, and the normal write still happens. A write-only overlay over
/// the whole address space, installed by the ATM3 port decoder only while `#BF` bit 2 is set.

#include <cstdint>

#include "emulator/memory/hostbusoverlay.h"

class EvoFontOverlay final : public HostBusOverlay
{
public:
    /// @param fontRam 2048 bytes, addressed code * 8 + row (AtmState::fontRam)
    explicit EvoFontOverlay(uint8_t* fontRam) : _fontRam(fontRam) { observesReads = false; }

    uint8_t onRead([[maybe_unused]] uint16_t addr, uint8_t normal, [[maybe_unused]] bool isExecution,
                   [[maybe_unused]] bool romPaged) override
    {
        return normal;
    }

    void onWrite(uint16_t addr, uint8_t value, [[maybe_unused]] bool romPaged) override
    {
        _fontRam[addr & 0x7FF] = value;
    }

private:
    uint8_t* _fontRam;
};
