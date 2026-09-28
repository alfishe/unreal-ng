#pragma once

/// @file gsslot.h
/// @brief Fit a GS-slot card in a test.
///
/// The shipped configs fit NeoGS on every model ([SOUND] GSType=NGS). A test
/// about a particular card fits it here instead of relying on that, and gets
/// a fresh card built from the context's current config - [NGS] settings the
/// test has just changed (SD image, MP3 decoder, RAM size) included, even
/// when that card type is already in the slot (a switch to the fitted type
/// is a no-op, so the card is swapped out and back).

#include "emulator/platform.h"
#include "emulator/sound/soundmanager.h"

inline bool FitGeneralSoundCard(SoundManager* sound, GSTypeKind kind)
{
    if (!sound)
        return false;
    if (sound->fittedGeneralSoundKind() == kind &&
        !sound->switchGeneralSoundCard(kind == GSTypeKind::Z80 ? GSTypeKind::LW : GSTypeKind::Z80))
        return false;
    return sound->switchGeneralSoundCard(kind);
}
