#include "stdafx.h"

#include "common/logger.h"

#include "imagehelper.h"
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

    string filename = StringHelper::Format("ZX_%04d.png", frameCount);
    ImageHelper::SavePNG(filename, buffer, size, width, height);
}