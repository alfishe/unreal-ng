#pragma once

/// @file tapeslot.h
/// @brief The tape deck as a media manager slot (`tape`).
///
/// The medium owns the parsed tape image (a file through TapeLoaderRegistry,
/// or a folder built by FolderTapeBuilder); Attach hands it to the deck, which
/// plays a copy of its blocks from the first one. The deck keeps its transport
/// (play, stop, rewind, fast load); ejecting stops it. There is no guest-side
/// tape detection, so a swap has no delay.
/// Design: docs/inprogress/2026-09-28-storage-manager/integration-tape.md §2-§3.

#include <string>

#include "emulator/media/mediaslot.h"

class EmulatorContext;

class TapeSlot : public IMediaSlot
{
public:
    static constexpr const char* kId = "tape";

    /// Registers itself with the context's media manager, when there is one
    explicit TapeSlot(EmulatorContext* context);
    ~TapeSlot() override;

    TapeSlot(const TapeSlot&) = delete;
    TapeSlot& operator=(const TapeSlot&) = delete;

    const SlotDescriptor& Descriptor() const override { return _descriptor; }
    void Attach(Medium& medium) override;
    void Detach() override;

private:
    EmulatorContext* _context = nullptr;
    SlotDescriptor _descriptor;
    bool _registered = false;
};
