#pragma  once
#include "stdafx.h"

#include "3rdparty/lodepng/lodepng.h"
#include <vector>

class ImageHelper
{
public:
	static void SavePNG(std::string filename, uint8_t* buffer, size_t size, unsigned width, unsigned height);
	static void SaveFrameToPNG(uint8_t* buffer, size_t size, unsigned width, unsigned height);
};
