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
        case MediaError::DoesNotFit: return "does-not-fit";
        case MediaError::IoError: return "io-error";
        case MediaError::AmbiguousSlot: return "ambiguous-slot";
        case MediaError::BadRequest: return "bad-request";
    }
    return "unknown";
}

int MediaErrorHttpStatus(MediaError error)
{
    switch (error)
    {
        case MediaError::None: return 200;
        case MediaError::UnknownSlot: return 404;
        case MediaError::Dirty:
        case MediaError::Recording:
        case MediaError::InUse: return 409;
        case MediaError::NotSupported: return 501;
        case MediaError::IoError: return 500;
        case MediaError::KindMismatch:
        case MediaError::UnreadableSource:
        case MediaError::UnknownFormat:
        case MediaError::DoesNotFit:
        case MediaError::AmbiguousSlot:
        case MediaError::BadRequest: return 400;
    }
    return 500;
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
