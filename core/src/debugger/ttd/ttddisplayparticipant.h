#pragma once

/// @file ttddisplayparticipant.h
/// @brief A device whose picture the monitor can show instead of the machine's
/// own raster (the TS-Conf VDAC2 card's FT812), taking part in what a TTD
/// position shows (display rule, docs/inprogress/2026-09-28-ttd-positioning-and-display/design.md §3;
/// VDAC2: docs/inprogress/2026-10-01-tsconf-vdac2/line-budget-metrics.md §3.4).
///
/// TimeTravelController::ComposeDisplay replays the machine to the position in a
/// sandbox and copies what the Screen shows (the external picture while one is
/// active). The participant:
///   - asks for more replayed machine frames before the target frame (a frame-number
///     target and a moment inside a frame alike), because its own frames do not line
///     up with the machine's: the device frame shown (or in flight) and the one under
///     it may have started before the target frame's checkpoint, and a line drawn
///     before the checkpoint is not in the replay;
///   - prepares its picture for the target kind just before the copy: at a frame
///     boundary the device's last finished frame, inside a frame the device frame
///     drawn up to the position over its previous frame;
///   - says whether the replay drew every line that picture shows (else the compose
///     goes back further, and where the session holds nothing earlier the seek's
///     check reports the picture as not the live one: picture_incomplete).

namespace ttd
{

class ITTDDisplayParticipant
{
public:
    virtual ~ITTDDisplayParticipant() = default;

    /// Machine frames to replay before the target frame (on top of the frame the target is in or ends)
    virtual unsigned TTDLeadInFrames() const = 0;

    /// The replay reached the position: make the picture the Screen shows the one
    /// for this target kind (frame boundary or a moment inside a frame)
    virtual void TTDPrepareComposedPicture(bool frameTarget) = 0;

    /// After TTDPrepareComposedPicture: every line of that picture was drawn by the replay since its checkpoint
    virtual bool TTDComposedPictureComplete() const = 0;
};

}  // namespace ttd
