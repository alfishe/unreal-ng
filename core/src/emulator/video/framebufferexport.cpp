#include "stdafx.h"

#include "framebufferexport.h"

#include "emulator/emulatorcontext.h"
#include "emulator/video/screen.h"

namespace FramebufferExport
{
bool Capture(EmulatorContext* context, const std::string& format, Frame& frame, std::string& error)
{
    Screen* screen = context ? context->pScreen : nullptr;
    if (!screen)
    {
        error = "Screen not available";
        return false;
    }
    frame = Frame();
    if (format.empty() || format == "rgba")
    {
        const FramebufferDescriptor& fb = screen->GetFramebufferDescriptor();
        const size_t size = static_cast<size_t>(fb.width) * fb.height * 4;
        if (!fb.width || !fb.height)
        {
            error = "No frame yet";
            return false;
        }
        frame.bytes.resize(size);
        if (!screen->CopyPresentedFramebuffer(frame.bytes.data(), frame.bytes.size()))
        {
            error = "The frame changed size (a video mode switch): ask again";
            return false;
        }
        frame.width = fb.width;
        frame.height = fb.height;
        frame.format = "rgba";
        frame.encoding = "u8 R,G,B,A per pixel, rows top to bottom (the presented frame)";
        return true;
    }
    if (format == "index")
    {
        std::vector<uint16_t> pens;
        uint16_t width = 0, height = 0;
        std::string encoding;
        if (!screen->IndexedFrame(pens, width, height, encoding))
        {
            error = "This machine's picture has no indexed form (format=rgba)";
            return false;
        }
        frame.width = width;
        frame.height = height;
        frame.format = "index";
        frame.encoding = encoding;
        frame.bytes.resize(pens.size() * 2);
        for (size_t i = 0; i < pens.size(); i++)
        {
            frame.bytes[2 * i] = static_cast<uint8_t>(pens[i] & 0xFF);
            frame.bytes[2 * i + 1] = static_cast<uint8_t>(pens[i] >> 8);
        }
        return true;
    }
    error = "format must be rgba or index";
    return false;
}
}  // namespace FramebufferExport
