#pragma once
//
// screen_renderer.h — ZX Spectrum screen renderer (port from Python).
//
// Renders the 256×192 screen + border to a QImage, selecting bank 5 or 7
// based on the checkpoint's chipset.p7ffd bit 3.
//

#include <QImage>
#include <cstdint>
#include <vector>
#include "ttd_reader.h"

namespace ttd {

/// Render the ZX screen for one checkpoint into a QImage.
///
/// @param cp        Checkpoint (chipset.p7ffd selects bank; border_attr paints border)
/// @param ram       Materialized RAM image (from MaterializeRam)
/// @param borderPx  Border thickness in pixels (default 32)
/// @return          QImage(256+2*borderPx, 192+2*borderPx, Format_RGB888)
QImage RenderScreen(const Checkpoint& cp, const std::vector<uint8_t>& ram,
                    int borderPx = kBorderDefaultPx);

/// Which RAM bank the ULA is displaying (5 or 7), based on p7ffd bit 3.
inline int SelectedScreenBank(uint8_t p7ffd) {
    return (p7ffd & 0x08) ? 7 : 5;
}

} // namespace ttd
