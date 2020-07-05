#include "stdafx.h"

#include "common/logger.h"

#include "imagehelper.h"
#include "common/dumphelper.h"
#include "common/filehelper.h"
#include "common/stringhelper.h"

void ImageHelper::SavePNG(std::string filename, uint8_t* buffer, size_t size, unsigned width, unsigned height)
{
	// Transform image buffer to std::vector
	std::vector<uint8_t> image(buffer, buffer + size);

	unsigned error = lodepng::encode(filename, image, width, height);

	if (error)
	{
		Logger::Error("PNG encoder error: %s", lodepng_error_text(error));
	}
}

void ImageHelper::SaveFrameToPNG(uint8_t* buffer, size_t size, unsigned width, unsigned height)
{
    static int frameCount = 0;
    frameCount++;

    if (buffer == nullptr || size == 0)
    {
        LOGWARNING("SaveFrameToPNG: empty buffer specified");
        return;
    }

    // Save as .png file
    string filename = StringHelper::Format("ZX_%04d.png", frameCount);
    ImageHelper::SavePNG(filename, buffer, size, width, height);

    // Save as hex dump file
    filename = StringHelper::Format("ZX_%04d.rgba", frameCount);
    DumpHelper::SaveHexDumpToFile(filename, buffer, size);
}

void ImageHelper::SaveZXSpectrumNativeScreen(uint8_t* buffer)
{
    static int frameCount = 0;
    frameCount++;

    string filename = StringHelper::Format("ZX_%04d.scr", frameCount);
    FileHelper::SaveBufferToFile(filename, buffer, 6144 + 768);

    filename = StringHelper::Format("ZX_%04d.hex", frameCount);
    DumpHelper::SaveHexDumpToFile(filename, buffer, 6144 + 768);
}