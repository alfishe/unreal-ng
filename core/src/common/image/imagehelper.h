#pragma  once
#include "stdafx.h"

#include "3rdparty/lodepng/lodepng.h"
#include <vector>

class ImageHelper
{
    /// region <Asynchronous operations>
public:
    static void SavePNG_Async(std::string filename, uint8_t* buffer, size_t size, unsigned width, unsigned height);
    static void SaveFrameToPNG_Async(uint8_t* buffer, size_t size, unsigned width, unsigned height, int frameNumber = -1);
    static void SaveFrameToHex_Async(uint8_t* buffer, size_t size, int frameNumber = -1);
    /// endregion </Asynchronous operations>


    /// region <Synchronous operations>
public:
	static void SavePNG(std::string filename, uint8_t* buffer, size_t size, unsigned width, unsigned height);
	static void SaveFrameToPNG(uint8_t* buffer, size_t size, unsigned width, unsigned height, int frameNumber = -1);
    static void SaveFrameToHex(uint8_t* buffer, size_t size, int frameNumber = -1);

	static void SaveZXSpectrumNativeScreen(uint8_t* buffer);

    /// endregion </Synchronous operations>
};
