#include "stdafx.h"

#include "spectrum128ayioport.h"

#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/soundmanager.h"

void Spectrum128AyIoPort::Attach(EmulatorContext* context, const IAyIoPortInput* wiring)
{
    if (!context || !context->pSoundManager)
        return;

    if (SoundChip_AY8910* socketChip = context->pSoundManager->getAYChip(0))
        socketChip->setIoPortInput(wiring);
}
