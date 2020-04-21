#pragma  once
#include "stdafx.h"

#include "3rdparty/lodepng/lodepng.h"
#include <vector>

class ImageHelper
{
	void savePNG(std::string filename, uint8_t* buffer, size_t size, unsigned width, unsigned height);
};
