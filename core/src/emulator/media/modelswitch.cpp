#include "stdafx.h"

#include "modelswitch.h"

#include <algorithm>

#include "common/stringhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"

namespace
{
    /// Dirty media of `from` (in a slot or detached) that `to` has no slot of the same kind for
    std::vector<SlotInfo> StrandedDirty(const MediaManager& from, const MediaManager& to)
    {
        const std::vector<SlotInfo> target = to.List();
        auto follows = [&target](const SlotInfo& info) {
            return std::any_of(target.begin(), target.end(), [&info](const SlotInfo& t) {
                return t.descriptor.id == info.descriptor.id && t.descriptor.kind == info.descriptor.kind;
            });
        };
        std::vector<SlotInfo> stranded;
        std::vector<SlotInfo> media = from.List();
        const std::vector<SlotInfo> detached = from.Detached();
        media.insert(media.end(), detached.begin(), detached.end());
        for (const SlotInfo& info : media)
        {
            if ((info.present || info.detached) && info.dirty && !follows(info))
                stranded.push_back(info);
        }
        return stranded;
    }

    std::string Names(const std::vector<SlotInfo>& media)
    {
        std::string text;
        for (const SlotInfo& info : media)
            text += (text.empty() ? "" : ", ") + info.descriptor.id + " (" + info.changes + ")";
        return text;
    }
}  // namespace

bool ModelSwitch::ParseStranded(const std::string& text, StrandedMedia& value)
{
    const std::string name = StringHelper::ToLower(text);
    if (name.empty() || name == "refuse")
        value = StrandedMedia::Refuse;
    else if (name == "save")
        value = StrandedMedia::Save;
    else if (name == "discard")
        value = StrandedMedia::Discard;
    else if (name == "keep" || name == "detach")
        value = StrandedMedia::Keep;
    else
        return false;
    return true;
}

const char* ModelSwitch::StrandedName(StrandedMedia value)
{
    switch (value)
    {
        case StrandedMedia::Refuse: return "refuse";
        case StrandedMedia::Save: return "save";
        case StrandedMedia::Discard: return "discard";
        case StrandedMedia::Keep: return "keep";
    }
    return "?";
}

ModelSwitchResult ModelSwitch::Run(const ModelSwitchRequest& request)
{
    ModelSwitchResult out;
    EmulatorManager& emulators = *EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> old = emulators.GetEmulator(request.emulatorId);
    if (!old || !old->GetContext() || !old->GetContext()->pMediaManager)
    {
        out.result = MediaResult::Fail(MediaError::BadRequest, "no emulator '" + request.emulatorId + "'");
        return out;
    }

    // The new machine first: its slots decide what follows. The old one runs on
    std::string error;
    std::shared_ptr<Emulator> created =
        request.ramKb > 0
            ? emulators.CreateEmulatorWithModelAndRAM(old->GetSymbolicId(), request.model, request.ramKb,
                                                      LoggerLevel::LogWarning, &error)
            : emulators.CreateEmulatorWithModel(old->GetSymbolicId(), request.model, LoggerLevel::LogWarning, &error);
    if (!created || !created->GetContext() || !created->GetContext()->pMediaManager)
    {
        if (created)
            emulators.RemoveEmulator(created->GetId());
        out.result = MediaResult::Fail(MediaError::BadRequest,
                                       error.empty() ? "cannot create a '" + request.model + "' machine" : error);
        return out;
    }

    MediaManager& from = *old->GetContext()->pMediaManager;
    MediaManager& to = *created->GetContext()->pMediaManager;
    out.stranded = StrandedDirty(from, to);
    if (!out.stranded.empty() && request.stranded == StrandedMedia::Refuse)
    {
        emulators.RemoveEmulator(created->GetId());
        out.result = MediaResult::Fail(MediaError::Dirty, "unsaved writes on media the new model has no slot for: " +
                                                              Names(out.stranded) + ": say save, discard or keep");
        return out;
    }

    // The old machine stops executing; stranded writes are dealt with while it
    // can still go on (a failed save leaves it as it was)
    const bool wasRunning = old->IsRunning() && !old->IsPaused();
    if (wasRunning)
        old->Pause(false);
    for (const SlotInfo& info : out.stranded)
    {
        MediaResult kept = MediaResult::Success();
        if (request.stranded == StrandedMedia::Save)
            kept = from.Save(info.descriptor.id);
        else if (request.stranded == StrandedMedia::Discard)
            kept = from.Discard(info.descriptor.id);
        if (!kept.Ok())
        {
            emulators.RemoveEmulator(created->GetId());
            if (wasRunning)
                old->Resume(false);
            out.result = MediaResult::Fail(kept.error, info.descriptor.id + ": " + kept.message);
            return out;
        }
    }

    if (old->IsRunning())
        old->Stop();
    MediaTransfer transfer = from.TakeMediaSet();
    if (request.beforeRelease)
        request.beforeRelease(*old);
    const bool wasSelected = emulators.GetSelectedEmulatorId() == old->GetId();
    const std::string oldId = old->GetId();
    old.reset();
    emulators.RemoveEmulator(oldId);

    out.media = to.AdoptMediaSet(std::move(transfer));
    if (wasSelected)
        emulators.SetSelectedEmulatorId(created->GetId());
    out.emulator = created;
    out.result = MediaResult::Success();
    out.result.report = out.media.lines;
    return out;
}
