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

    // Report the DISPLAY region (viewport-cropped), not the raw framebuffer:
    // hosts size their textures to these dimensions and CopyFrame delivers
    // exactly this rect, so e.g. an overscan Pentagon face with the symmetric
    // horizontal viewport shows the centered 352x304 view, not the full frame
    if (width)
        *width = screen->GetDisplayWidth();
    if (height)
        *height = screen->GetDisplayHeight();
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

    // Viewport-cropped copy matching GetFrameInfo's reported dimensions;
    // fails (hosts keep their previous staged frame) only across a video-mode
    // switch or a degenerate crop
    bool result = screen->CopyPresentedViewport(reinterpret_cast<uint8_t*>(dstRgba8), dstSize);
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
