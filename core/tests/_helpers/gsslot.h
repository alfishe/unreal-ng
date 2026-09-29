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

#include <string>

#include "common/filehelper.h"

#include "emulator/emulatorcontext.h"
#include "emulator/media/mediamanager.h"
#include "emulator/platform.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"
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

/// Fit NeoGS with `sdImage` (an image file or a folder) in its SD slot
/// `sd.ngs`, as a config's [MEDIA] sd.ngs / [NGS] SDCardImage would: the card
/// finds it before it runs a single cycle. The machine's media manager owns
/// the medium, so this needs a full emulator (bare contexts use
/// NeoGSConfig::sdCardPath instead)
inline bool FitNeoGSWithSd(EmulatorContext* context, const std::string& sdImage)
{
    if (!context || !context->pMediaManager || !FitGeneralSoundCard(context->pSoundManager, GSTypeKind::NGS))
        return false;
    MediaSource source;
    source.path = sdImage;
    source.type = FileHelper::IsFolder(sdImage) ? MediaSourceType::Folder : MediaSourceType::File;
    InsertOptions options;
    options.immediate = true;
    options.disposition = Disposition::Discard;
    const MediaResult result = context->pMediaManager->Insert(SoundChip_NeoGS::SD_SLOT_ID, source, options);
    auto* card = dynamic_cast<SoundChip_NeoGS*>(context->pSoundManager->getGeneralSound());
    return result.Ok() && card && card->sdCardPresent();
}
