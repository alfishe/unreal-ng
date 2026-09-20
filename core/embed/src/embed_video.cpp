#include "embed_video.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/screen.h"

app_result EmbedVideo::GetFrameInfo(Emulator* emu, uint16_t* width, uint16_t* height, uint64_t* latchTimestampUs)
{
    if (!emu || !emu->GetContext())
        return APP_ERR_ARG;

    Screen* screen = emu->GetContext()->pScreen;
    if (!screen)
        return APP_ERR_STATE;

    const FramebufferDescriptor desc = screen->GetFramebufferDescriptor();
    if (width)
        *width = static_cast<uint16_t>(desc.width);
    if (height)
        *height = static_cast<uint16_t>(desc.height);
    if (latchTimestampUs)
        *latchTimestampUs = screen->GetLastLatchTimestampUs();

    return APP_OK;
}

app_result EmbedVideo::CopyFrame(Emulator* emu, void* dstRgba8, size_t dstSize)
{
    if (!emu || !emu->GetContext() || !dstRgba8)
        return APP_ERR_ARG;

    Screen* screen = emu->GetContext()->pScreen;
    if (!screen)
        return APP_ERR_STATE;

    bool result = screen->CopyPresentedFramebuffer(reinterpret_cast<uint8_t*>(dstRgba8), dstSize);
    return result ? APP_OK : APP_ERR_INTERNAL;
}

void EmbedVideo::SetPresentDelay(Emulator* emu, uint8_t frames)
{
    if (!emu || !emu->GetContext())
        return;

    Screen* screen = emu->GetContext()->pScreen;
    if (screen)
    {
        screen->SetPresentDelayFrames(frames);
    }
}
