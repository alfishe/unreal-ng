#include "stdafx.h"

#include "common/logger.h"

#include "imagehelper.h"
#include <thread>
#include "common/dumphelper.h"
#include "common/filehelper.h"
#include "common/stringhelper.h"

/// region <Asynchronous operations>

void ImageHelper::SavePNG_Async(std::string filename, uint8_t* buffer, size_t size, unsigned width, unsigned height)
{
    // Make buffer snapshot to prevent capturing future updates
    uint8_t* copyBuffer = new uint8_t[size];
    memcpy(copyBuffer, buffer, size);

    std::thread
    {
        [filename, copyBuffer, size, width, height]()
        {
            SavePNG(filename, copyBuffer, size, width, height);

            delete [] copyBuffer;
        }
    }.detach();
}

void ImageHelper::SaveFrameToPNG_Async(uint8_t* buffer, size_t size, unsigned width, unsigned height, int frameNumber)
{
    // Make buffer snapshot to prevent capturing future updates
    uint8_t* copyBuffer = new uint8_t[size];
    memcpy(copyBuffer, buffer, size);

    std::thread
    {
        [copyBuffer, size, width, height, frameNumber]()
        {
            SaveFrameToPNG(copyBuffer, size, width, height, frameNumber);

            delete [] copyBuffer;
        }
    }.detach();
}

void ImageHelper::SaveFrameToHex_Async(uint8_t* buffer, size_t size, int frameNumber)
{
    // Make buffer snapshot to prevent capturing future updates
    uint8_t* copyBuffer = new uint8_t[size];
    memcpy(copyBuffer, buffer, size);

    std::thread
    {
        [copyBuffer, size, frameNumber]()
        {
            SaveFrameToHex(copyBuffer, size, frameNumber);

            delete [] copyBuffer;
        }
    }.detach();
}

/// endregion </Asynchronous operations>

/// region <Synchronous operations>

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

void ImageHelper::SaveFrameToPNG(uint8_t* buffer, size_t size, unsigned width, unsigned height, int frameNumber)
{
    static int frameCount = 0;
    frameCount++;

    if (buffer == nullptr || size == 0)
    {
        LOGWARNING("SaveFrameToPNG: empty buffer specified");
        return;
    }

    // Save as .png file
    string filename = StringHelper::Format("ZX_%04d.png", frameNumber >= 0 ? frameNumber : frameCount);
    ImageHelper::SavePNG(filename, buffer, size, width, height);
}

void ImageHelper::SaveFrameToHex(uint8_t* buffer, size_t size, int frameNumber)
{
    static int frameCount = 0;
    frameCount++;

    string filename = StringHelper::Format("ZX_%04d.rgba", frameNumber >= 0 ? frameNumber : frameCount);
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

/// endregion </Synchronous operations>