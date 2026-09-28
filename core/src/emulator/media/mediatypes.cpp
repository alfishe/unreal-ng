#include "stdafx.h"

#include "mediatypes.h"

#include <algorithm>
#include <cctype>

const char* MediaErrorCode(MediaError error)
{
    switch (error)
    {
        case MediaError::None: return "ok";
        case MediaError::UnknownSlot: return "unknown-slot";
        case MediaError::KindMismatch: return "kind-mismatch";
        case MediaError::UnreadableSource: return "unreadable-source";
        case MediaError::UnknownFormat: return "unknown-format";
        case MediaError::Dirty: return "dirty";
        case MediaError::Recording: return "recording";
        case MediaError::InUse: return "in-use";
        case MediaError::NotSupported: return "not-supported";
        case MediaError::IoError: return "io-error";
    }
    return "unknown";
}

const char* MediaKindName(MediaKind kind)
{
    switch (kind)
    {
        case MediaKind::Floppy: return "floppy";
        case MediaKind::Tape: return "tape";
        case MediaKind::Block: return "block";
        case MediaKind::Optical: return "optical";
    }
    return "unknown";
}

const char* AccessModeName(AccessMode access)
{
    switch (access)
    {
        case AccessMode::ReadOnly: return "readonly";
        case AccessMode::Session: return "session";
        case AccessMode::WriteThrough: return "writethrough";
    }
    return "unknown";
}

bool ParseAccessMode(const std::string& text, AccessMode& out)
{
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lower == "readonly" || lower == "off")
        out = AccessMode::ReadOnly;
    else if (lower == "session")
        out = AccessMode::Session;
    else if (lower == "writethrough" || lower == "persist")
        out = AccessMode::WriteThrough;
    else
        return false;
    return true;
}
