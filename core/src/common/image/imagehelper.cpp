#include "stdafx.h"

#include "common/logger.h"

#include "imagehelper.h"

void ImageHelper::savePNG(std::string filename, uint8_t* buffer, size_t size, unsigned width, unsigned height)
{
	// Transform image buffer to std::vector
	std::vector<uint8_t> image(buffer, buffer + size);

	unsigned error = lodepng::encode(filename, image, width, height);

	if (error)
	{
		Logger::Error("PNG encoder error: %s", lodepng_error_text(error));
	}
}