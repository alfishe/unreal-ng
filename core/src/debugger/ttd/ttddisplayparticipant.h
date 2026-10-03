#pragma once

/// @file ttddisplayparticipant.h
/// @brief A device whose picture the monitor can show instead of the machine's
/// own raster (the TS-Conf VDAC2 card's FT812), taking part in what a TTD
/// position shows (display rule, docs/inprogress/2026-09-28-ttd-positioning-and-display/design.md §3;
/// VDAC2: docs/inprogress/2026-10-01-tsconf-vdac2/line-budget-metrics.md §3.4).
///
/// TimeTravelManager::ComposeDisplay replays the machine to the position in a
/// sandbox and copies what the Screen shows (the external picture while one is
/// active). The participant:
///   - asks for one more replayed machine frame before a frame-number target,
///     because its own frames do not line up with the machine's: the device frame
///     that finished last may have started before the target frame's checkpoint;
///   - prepares its picture for the target kind just before the copy: at a frame
///     boundary the device's last finished frame, inside a frame the device frame
///     drawn up to the position over its previous frame.

namespace ttd
{

class ITTDDisplayParticipant
{
public:
    virtual ~ITTDDisplayParticipant() = default;

    /// Machine frames to replay before a frame-number target (on top of the target frame)
    virtual unsigned TTDLeadInFrames() const = 0;

    /// The replay reached the position: make the picture the Screen shows the one
    /// for this target kind (frame boundary or a moment inside a frame)
    virtual void TTDPrepareComposedPicture(bool frameTarget) = 0;
};

}  // namespace ttd
