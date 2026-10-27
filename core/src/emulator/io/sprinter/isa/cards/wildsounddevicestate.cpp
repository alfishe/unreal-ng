// DeviceState::WildSound (emulator/state/devicestate.h): the Sprinter ISA Wild Sound's report for every automation
// surface (docs/inprogress/2026-10-09-wildsound/design.md §8), built beside the card so shared code names no card type

#include "emulator/state/devicestate.h"

#include <string>

#include "emulator/emulatorcontext.h"
#include "emulator/io/sprinter/isa/cards/isawildsoundcard.h"
#include "emulator/ports/models/portdecoder_sprinter.h"

namespace
{

StateNode PlayerReport(const wildsound::WildSoundXmPlayer& player, const wildsound::WildSoundStorage& storage)
{
    const auto& s = player.GetState();
    StateNode p = StateNode::Object();
    p["playing"] = s.playing;
    if (!s.playing)
        return p;
    const auto& track = storage.Track();
    p["order"] = s.order;
    p["pattern"] = track.valid && s.order < track.songLength ? track.order[s.order] : 0;
    p["row"] = s.row;
    p["tick"] = s.tick;
    p["speed"] = s.speed;
    p["bpm"] = s.bpm;
    p["global_volume"] = s.globalVolume;
    p["rows_played"] = s.rows;
    p["output_rate"] = s.outputRate;
    StateNode channels = StateNode::Array();
    for (int c = 0; c < s.channelCount; ++c)
    {
        const auto& ch = s.channels[c];
        StateNode n = StateNode::Object();
        n["active"] = ch.active;
        n["instrument"] = ch.ins;
        n["note"] = ch.baseNote;
        n["volume"] = ch.volume;
        n["panning"] = ch.panning;
        n["period"] = ch.period;
        n["hz"] = wildsound::WildSoundXmPlayer::PeriodToHz(s.linear, ch.outPeriod ? ch.outPeriod : ch.period);
        channels.push(n);
    }
    p["channels"] = channels;
    return p;
}

}  // namespace

namespace DeviceState
{

StateNode WildSound(EmulatorContext* context)
{
    StateNode out = StateNode::Object();
    auto* decoder = context ? dynamic_cast<PortDecoder_Sprinter*>(context->pPortDecoder) : nullptr;
    StateNode cards = StateNode::Array();
    if (decoder)
    {
        for (int n = 0; n < SprinterIsaBus::kSlots; ++n)
        {
            auto* card = dynamic_cast<sprinterisa::IsaWildSoundCard*>(decoder->GetIsaBus().Card(n));
            if (!card)
                continue;
            StateNode c = StateNode::Object();
            c["slot"] = n + 1;
            card->Describe(c);
            if (auto* audio = card->Audio())
            {
                c["player"] = PlayerReport(audio->Player(), card->Core().Storage());
                StateNode chips = StateNode::Array();
                for (int i = 0; i < 2; ++i)
                    chips.push(AyChipReport(audio->Chip(i), i, wildsound::WildSoundAudio::kAyClockHz));
                c["ay_chips"] = chips;
            }
            cards.push(c);
        }
    }
    if (cards.size() == 0)
    {
        out["available"] = false;
        out["description"] = "no Wild Sound card: a SPRINTER with [ISA] Slot1=WILDSOUND (or create with "
                             "sprinter isa_slot1 = wildsound)";
        return out;
    }
    out["available"] = true;
    out["cards"] = cards;
    return out;
}

}  // namespace DeviceState
