#include "ttdconfigcapture.h"

#include "base/featuremanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/sound/audio.h"

namespace ttd
{

TTDConfigFingerprint CaptureConfigFingerprint(const EmulatorContext& context, uint64_t romSignature)
{
    const CONFIG& c = context.config;
    const EmulatorState& s = context.emulatorState;
    TTDConfigFingerprint fp;

    // The machine and its memory: a checkpoint does not fit another one
    fp.Add("machine.model", static_cast<uint64_t>(c.mem_model), true);
    fp.Add("machine.ram_kb", c.ramsize, true);

    // Frame and interrupt timing, the TTD time unit
    fp.Add("timing.frame", c.frame);
    fp.Add("timing.line", c.t_line);
    fp.Add("timing.int_start", c.intstart);
    fp.Add("timing.int_length", c.intlen);
    fp.Add("timing.frame_duration_us", c.frame_duration_us);
    fp.Add("timing.clock_units", s.ttd_clock_units ? s.ttd_clock_units : 1);

    // Audio and screen rendering: what the recorded frames sound and look like
    fp.Add("sound.core_rate", AUDIO_SAMPLING_RATE);
    fp.Add("sound.decimator_high_fidelity", c.sound.decimatorHighFidelity ? 1 : 0);
    fp.Add("sound.gs_ram_kb", c.sound.gsRamKB);
    if (context.pEmulator)
        if (FeatureManager* features = context.pEmulator->GetFeatureManager())
        {
            fp.Add("render.sound_hq", features->isEnabled(Features::kSoundHQ) ? 1 : 0);
            fp.Add("render.screen_hq", features->isEnabled(Features::kScreenHQ) ? 1 : 0);
        }

    // Board options that change timing or decoding (the model's own only)
    fp.Add("fdc.turbo_vg", static_cast<uint64_t>(static_cast<int64_t>(c.fdcTurboVg)));
    if (context.pPortDecoder)
        context.pPortDecoder->AddTTDBoardSettings(fp);

    // The slot set and every card's options (ZX-bus slots R-NF-2): `slots.<slot>`, `slots.builtin.<id>`; a
    // checkpoint holds the devices of its slot set, so a difference affects a restore
    if (context.pSlotManager)
        context.pSlotManager->AddTtdFingerprint(fp);

    // Every model's ROM set, the Sprinter BIOS (its flash) among them
    if (romSignature)
        fp.Add(kConfigRomSignature, romSignature);
    return fp;
}

}  // namespace ttd
