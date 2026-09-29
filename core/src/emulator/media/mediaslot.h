#pragma once

/// @file mediaslot.h
/// @brief A place a medium goes (a floppy drive, the tape deck, an SD slot, an
/// IDE unit). Peripherals own their slots and register them with the
/// MediaManager; the manager owns the media and attaches them.
///
/// Attach / Detach are always called on the emulation thread (or while the
/// emulator is not running), so a slot never needs its own locking.

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/media/mediatypes.h"

class Medium;

struct SlotDescriptor
{
    /// `<kind>.<controller>[<n>][.<unit>]`, tied to the controller, not the
    /// machine: sd.zc, sd.ngs, fdd.a, ide0.master, tape
    std::string id;
    MediaKind kind = MediaKind::Block;
    std::string label;                   ///< for people: "SD card", "Drive A"
    bool removable = true;               ///< false: insert / eject only while paused
    bool required = false;               ///< the machine cannot start without it
    uint32_t swapDelayMs = 0;            ///< the slot stays empty this long on a swap
    bool acceptsFolder = false;          ///< a host folder can be inserted
    AccessMode defaultAccess = AccessMode::Session;
    FatType defaultFs = FatType::Fat16;  ///< folder volumes, unless the request says otherwise
    bool hasCardDetect = false;          ///< the slot reports "present" to its peripheral
    bool hasWriteProtectSwitch = false;  ///< the slot reports its switch to its peripheral

    /// What the slot is, where it sits, what it is for: "sd", "zcontroller",
    /// "primary", "boot", "wd1793", "trdos" ... The manager adds the kind name,
    /// "removable" and "folder" itself (media-control-design.md §3.2)
    std::vector<std::string> tags;
    /// Short names people type: "A" (fdd.a), "sd" (the primary SD slot), "hd".
    /// Matched case-insensitively; a trailing ':' is ignored ("a:")
    std::vector<std::string> aliases;
    /// What the guest OS calls it, when that differs (informative only):
    /// "E: (NedoOS / ERS, first FAT partition)"
    std::string guestName;
};

class IMediaSlot
{
public:
    virtual ~IMediaSlot() = default;

    virtual const SlotDescriptor& Descriptor() const = 0;

    /// A medium goes in. The manager keeps owning it; the slot keeps a pointer
    /// until Detach
    virtual void Attach(Medium& medium) = 0;

    /// The medium goes out; drop every pointer to it
    virtual void Detach() = 0;

    /// A transfer is in flight (a multi-block read, a sector write): a swap
    /// waits for the next frame
    virtual bool IsBusy() const { return false; }

    /// The slot's write-protect switch (reported to the guest; never enforced
    /// by the medium - that is AccessMode's job)
    virtual void SetWriteProtectSwitch(bool on) { (void)on; }

    /// The attached medium now stands for another file (a save to a new path)
    virtual void SourceChanged(Medium& medium) { (void)medium; }
};
