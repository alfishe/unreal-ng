#include "gif.h"

// max, min, and abs functions
int GifIMax(int l, int r)
{
    return l > r ? l : r;
}

int GifIMin(int l, int r)
{
    return l < r ? l : r;
}
int GifIAbs(int i)
{
    return i < 0 ? -i : i;
}

// walks the k-d tree to pick the palette entry for a desired color.
// Takes as in/out parameters the current best color and its error -
// only changes them if it finds a better color in its subtree.
// this is the major hotspot in the code at the moment.
void GifGetClosestPaletteColor(GifPalette* pPal, int r, int g, int b, int& bestInd, int& bestDiff, int treeRoot)
{
    // base case, reached the bottom of the tree
    if (treeRoot > (1 << pPal->bitDepth) - 1)
    {
        int ind = treeRoot - (1 << pPal->bitDepth);
        if (ind == kGifTransIndex)
            return;

        // check whether this color is better than the current winner
        int r_err = r - ((int32_t)pPal->r[ind]);
        int g_err = g - ((int32_t)pPal->g[ind]);
        int b_err = b - ((int32_t)pPal->b[ind]);
        int diff = GifIAbs(r_err) + GifIAbs(g_err) + GifIAbs(b_err);

        if (diff < bestDiff)
        {
            bestInd = ind;
            bestDiff = diff;
        }

        return;
    }

    // take the appropriate color (r, g, or b) for this node of the k-d tree
    int comps[3];
    comps[0] = r;
    comps[1] = g;
    comps[2] = b;
    int splitComp = comps[pPal->treeSplitElt[treeRoot]];

    int splitPos = pPal->treeSplit[treeRoot];
    if (splitPos > splitComp)
    {
        // check the left subtree
        GifGetClosestPaletteColor(pPal, r, g, b, bestInd, bestDiff, treeRoot * 2);
        if (bestDiff > splitPos - splitComp)
        {
            // cannot prove there's not a better value in the right subtree, check that too
            GifGetClosestPaletteColor(pPal, r, g, b, bestInd, bestDiff, treeRoot * 2 + 1);
        }
    }
    else
    {
        GifGetClosestPaletteColor(pPal, r, g, b, bestInd, bestDiff, treeRoot * 2 + 1);
        if (bestDiff > splitComp - splitPos)
        {
            GifGetClosestPaletteColor(pPal, r, g, b, bestInd, bestDiff, treeRoot * 2);
        }
    }
}

void GifSwapPixels(uint8_t* image, int pixA, int pixB)
{
    uint8_t rA = image[pixA * 4];
    uint8_t gA = image[pixA * 4 + 1];
    uint8_t bA = image[pixA * 4 + 2];
    uint8_t aA = image[pixA * 4 + 3];

    uint8_t rB = image[pixB * 4];
    uint8_t gB = image[pixB * 4 + 1];
    uint8_t bB = image[pixB * 4 + 2];
    uint8_t aB = image[pixA * 4 + 3];

    image[pixA * 4] = rB;
    image[pixA * 4 + 1] = gB;
    image[pixA * 4 + 2] = bB;
    image[pixA * 4 + 3] = aB;

    image[pixB * 4] = rA;
    image[pixB * 4 + 1] = gA;
    image[pixB * 4 + 2] = bA;
    image[pixB * 4 + 3] = aA;
}

// just the partition operation from quicksort
int GifPartition(uint8_t* image, const int left, const int right, const int elt, int pivotIndex)
{
    const int pivotValue = image[(pivotIndex) * 4 + elt];
    GifSwapPixels(image, pivotIndex, right - 1);
    int storeIndex = left;
    bool split = 0;
    for (int ii = left; ii < right - 1; ++ii)
    {
        int arrayVal = image[ii * 4 + elt];
        if (arrayVal < pivotValue)
        {
            GifSwapPixels(image, ii, storeIndex);
            ++storeIndex;
        }
        else if (arrayVal == pivotValue)
        {
            if (split)
            {
                GifSwapPixels(image, ii, storeIndex);
                ++storeIndex;
            }
            split = !split;
        }
    }
    GifSwapPixels(image, storeIndex, right - 1);
    return storeIndex;
}

// Perform an incomplete sort, finding all elements above and below the desired median
void GifPartitionByMedian(uint8_t* image, int left, int right, int com, int neededCenter)
{
    if (left < right - 1)
    {
        int pivotIndex = left + (right - left) / 2;

        pivotIndex = GifPartition(image, left, right, com, pivotIndex);

        // Only "sort" the section of the array that contains the median
        if (pivotIndex > neededCenter)
            GifPartitionByMedian(image, left, pivotIndex, com, neededCenter);

        if (pivotIndex < neededCenter)
            GifPartitionByMedian(image, pivotIndex + 1, right, com, neededCenter);
    }
}

// Builds a palette by creating a balanced k-d tree of all pixels in the image
void GifSplitPalette(uint8_t* image, int numPixels, int firstElt, int lastElt, int splitElt, int splitDist,
                     int treeNode, bool buildForDither, GifPalette* pal)
{
    if (lastElt <= firstElt || numPixels == 0)
        return;

    // base case, bottom of the tree
    if (lastElt == firstElt + 1)
    {
        if (buildForDither)
        {
            // Dithering needs at least one color as dark as anything
            // in the image and at least one brightest color -
            // otherwise it builds up error and produces strange artifacts
            if (firstElt == 1)
            {
                // special case: the darkest color in the image
                uint32_t r = 255, g = 255, b = 255;
                for (int ii = 0; ii < numPixels; ++ii)
                {
                    r = (uint32_t)GifIMin((int32_t)r, image[ii * 4 + 0]);
                    g = (uint32_t)GifIMin((int32_t)g, image[ii * 4 + 1]);
                    b = (uint32_t)GifIMin((int32_t)b, image[ii * 4 + 2]);
                }

                pal->r[firstElt] = (uint8_t)r;
                pal->g[firstElt] = (uint8_t)g;
                pal->b[firstElt] = (uint8_t)b;

                return;
            }

            if (firstElt == (1 << pal->bitDepth) - 1)
            {
                // special case: the lightest color in the image
                uint32_t r = 0, g = 0, b = 0;
                for (int ii = 0; ii < numPixels; ++ii)
                {
                    r = (uint32_t)GifIMax((int32_t)r, image[ii * 4 + 0]);
                    g = (uint32_t)GifIMax((int32_t)g, image[ii * 4 + 1]);
                    b = (uint32_t)GifIMax((int32_t)b, image[ii * 4 + 2]);
                }

                pal->r[firstElt] = (uint8_t)r;
                pal->g[firstElt] = (uint8_t)g;
                pal->b[firstElt] = (uint8_t)b;

                return;
            }
        }

        // otherwise, take the average of all colors in this subcube
        uint64_t r = 0, g = 0, b = 0;
        for (int ii = 0; ii < numPixels; ++ii)
        {
            r += image[ii * 4 + 0];
            g += image[ii * 4 + 1];
            b += image[ii * 4 + 2];
        }

        r += (uint64_t)numPixels / 2;  // round to nearest
        g += (uint64_t)numPixels / 2;
        b += (uint64_t)numPixels / 2;

        r /= (uint64_t)numPixels;
        g /= (uint64_t)numPixels;
        b /= (uint64_t)numPixels;

        pal->r[firstElt] = (uint8_t)r;
        pal->g[firstElt] = (uint8_t)g;
        pal->b[firstElt] = (uint8_t)b;

        return;
    }

    // Find the axis with the largest range
    int minR = 255, maxR = 0;
    int minG = 255, maxG = 0;
    int minB = 255, maxB = 0;
    for (int ii = 0; ii < numPixels; ++ii)
    {
        int r = image[ii * 4 + 0];
        int g = image[ii * 4 + 1];
        int b = image[ii * 4 + 2];

        if (r > maxR)
            maxR = r;
        if (r < minR)
            minR = r;

        if (g > maxG)
            maxG = g;
        if (g < minG)
            minG = g;

        if (b > maxB)
            maxB = b;
        if (b < minB)
            minB = b;
    }

    int rRange = maxR - minR;
    int gRange = maxG - minG;
    int bRange = maxB - minB;

    // and split along that axis. (incidentally, this means this isn't a "proper" k-d tree but I don't know what else to
    // call it)
    int splitCom = 1;
    if (bRange > gRange)
        splitCom = 2;
    if (rRange > bRange && rRange > gRange)
        splitCom = 0;

    int subPixelsA = numPixels * (splitElt - firstElt) / (lastElt - firstElt);
    int subPixelsB = numPixels - subPixelsA;

    GifPartitionByMedian(image, 0, numPixels, splitCom, subPixelsA);

    pal->treeSplitElt[treeNode] = (uint8_t)splitCom;
    pal->treeSplit[treeNode] = image[subPixelsA * 4 + splitCom];

    GifSplitPalette(image, subPixelsA, firstElt, splitElt, splitElt - splitDist, splitDist / 2, treeNode * 2,
                    buildForDither, pal);
    GifSplitPalette(image + subPixelsA * 4, subPixelsB, splitElt, lastElt, splitElt + splitDist, splitDist / 2,
                    treeNode * 2 + 1, buildForDither, pal);
}

// Finds all pixels that have changed from the previous image and
// moves them to the fromt of th buffer.
// This allows us to build a palette optimized for the colors of the
// changed pixels only.
int GifPickChangedPixels(const uint8_t* lastFrame, uint8_t* frame, int numPixels)
{
    int numChanged = 0;
    uint8_t* writeIter = frame;

    for (int ii = 0; ii < numPixels; ++ii)
    {
        if (lastFrame[0] != frame[0] || lastFrame[1] != frame[1] || lastFrame[2] != frame[2])
        {
            writeIter[0] = frame[0];
            writeIter[1] = frame[1];
            writeIter[2] = frame[2];
            ++numChanged;
            writeIter += 4;
        }
        lastFrame += 4;
        frame += 4;
    }

    return numChanged;
}

// Creates a palette by placing all the image pixels in a k-d tree and then averaging the blocks at the bottom.
// This is known as the "modified median split" technique
void GifMakePalette(const uint8_t* lastFrame, const uint8_t* nextFrame, uint32_t width, uint32_t height, int bitDepth,
                    bool buildForDither, GifPalette* pPal)
{
    pPal->bitDepth = bitDepth;

    // SplitPalette is destructive (it sorts the pixels by color) so
    // we must create a copy of the image for it to destroy
    size_t imageSize = (size_t)(width * height * 4 * sizeof(uint8_t));
    uint8_t* destroyableImage = (uint8_t*)GIF_TEMP_MALLOC(imageSize);
    memcpy(destroyableImage, nextFrame, imageSize);

    int numPixels = (int)(width * height);
    if (lastFrame)
        numPixels = GifPickChangedPixels(lastFrame, destroyableImage, numPixels);

    const int lastElt = 1 << bitDepth;
    const int splitElt = lastElt / 2;
    const int splitDist = splitElt / 2;

    GifSplitPalette(destroyableImage, numPixels, 1, lastElt, splitElt, splitDist, 1, buildForDither, pPal);

    GIF_TEMP_FREE(destroyableImage);

    // add the bottom node for the transparency index
    pPal->treeSplit[1 << (bitDepth - 1)] = 0;
    pPal->treeSplitElt[1 << (bitDepth - 1)] = 0;

    pPal->r[0] = pPal->g[0] = pPal->b[0] = 0;
}

// Implements Floyd-Steinberg dithering, writes palette value to alpha
void GifDitherImage(const uint8_t* lastFrame, const uint8_t* nextFrame, uint8_t* outFrame, uint32_t width,
                    uint32_t height, GifPalette* pPal)
{
    int numPixels = (int)(width * height);

    // quantPixels initially holds color*256 for all pixels
    // The extra 8 bits of precision allow for sub-single-color error values
    // to be propagated
    int32_t* quantPixels = (int32_t*)GIF_TEMP_MALLOC(sizeof(int32_t) * (size_t)numPixels * 4);

    for (int ii = 0; ii < numPixels * 4; ++ii)
    {
        uint8_t pix = nextFrame[ii];
        int32_t pix16 = int32_t(pix) * 256;
        quantPixels[ii] = pix16;
    }

    for (uint32_t yy = 0; yy < height; ++yy)
    {
        for (uint32_t xx = 0; xx < width; ++xx)
        {
            int32_t* nextPix = quantPixels + 4 * (yy * width + xx);
            const uint8_t* lastPix = lastFrame ? lastFrame + 4 * (yy * width + xx) : NULL;

            // Compute the colors we want (rounding to nearest)
            int32_t rr = (nextPix[0] + 127) / 256;
            int32_t gg = (nextPix[1] + 127) / 256;
            int32_t bb = (nextPix[2] + 127) / 256;

            // if it happens that we want the color from last frame, then just write out
            // a transparent pixel
            if (lastFrame && lastPix[0] == rr && lastPix[1] == gg && lastPix[2] == bb)
            {
                nextPix[0] = rr;
                nextPix[1] = gg;
                nextPix[2] = bb;
                nextPix[3] = kGifTransIndex;
                continue;
            }

            int32_t bestDiff = 1000000;
            int32_t bestInd = kGifTransIndex;

            // Search the palete
            GifGetClosestPaletteColor(pPal, rr, gg, bb, bestInd, bestDiff);

            // Write the result to the temp buffer
            int32_t r_err = nextPix[0] - int32_t(pPal->r[bestInd]) * 256;
            int32_t g_err = nextPix[1] - int32_t(pPal->g[bestInd]) * 256;
            int32_t b_err = nextPix[2] - int32_t(pPal->b[bestInd]) * 256;

            nextPix[0] = pPal->r[bestInd];
            nextPix[1] = pPal->g[bestInd];
            nextPix[2] = pPal->b[bestInd];
            nextPix[3] = bestInd;

            // Propagate the error to the four adjacent locations
            // that we haven't touched yet
            int quantloc_7 = (int)(yy * width + xx + 1);
            int quantloc_3 = (int)(yy * width + width + xx - 1);
            int quantloc_5 = (int)(yy * width + width + xx);
            int quantloc_1 = (int)(yy * width + width + xx + 1);

            if (quantloc_7 < numPixels)
            {
                int32_t* pix7 = quantPixels + 4 * quantloc_7;
                pix7[0] += GifIMax(-pix7[0], r_err * 7 / 16);
                pix7[1] += GifIMax(-pix7[1], g_err * 7 / 16);
                pix7[2] += GifIMax(-pix7[2], b_err * 7 / 16);
            }

            if (quantloc_3 < numPixels)
            {
                int32_t* pix3 = quantPixels + 4 * quantloc_3;
                pix3[0] += GifIMax(-pix3[0], r_err * 3 / 16);
                pix3[1] += GifIMax(-pix3[1], g_err * 3 / 16);
                pix3[2] += GifIMax(-pix3[2], b_err * 3 / 16);
            }

            if (quantloc_5 < numPixels)
            {
                int32_t* pix5 = quantPixels + 4 * quantloc_5;
                pix5[0] += GifIMax(-pix5[0], r_err * 5 / 16);
                pix5[1] += GifIMax(-pix5[1], g_err * 5 / 16);
                pix5[2] += GifIMax(-pix5[2], b_err * 5 / 16);
            }

            if (quantloc_1 < numPixels)
            {
                int32_t* pix1 = quantPixels + 4 * quantloc_1;
                pix1[0] += GifIMax(-pix1[0], r_err / 16);
                pix1[1] += GifIMax(-pix1[1], g_err / 16);
                pix1[2] += GifIMax(-pix1[2], b_err / 16);
            }
        }
    }

    // Copy the palettized result to the output buffer
    for (int ii = 0; ii < numPixels * 4; ++ii)
    {
        outFrame[ii] = (uint8_t)quantPixels[ii];
    }

    GIF_TEMP_FREE(quantPixels);
}

// Picks palette colors for the image using simple thresholding, no dithering
void GifThresholdImage(const uint8_t* lastFrame, const uint8_t* nextFrame, uint8_t* outFrame, uint32_t width,
                       uint32_t height, GifPalette* pPal)
{
    uint32_t numPixels = width * height;
    for (uint32_t ii = 0; ii < numPixels; ++ii)
    {
        // if a previous color is available, and it matches the current color,
        // set the pixel to transparent
        if (lastFrame && lastFrame[0] == nextFrame[0] && lastFrame[1] == nextFrame[1] && lastFrame[2] == nextFrame[2])
        {
            outFrame[0] = lastFrame[0];
            outFrame[1] = lastFrame[1];
            outFrame[2] = lastFrame[2];
            outFrame[3] = kGifTransIndex;
        }
        else
        {
            // palettize the pixel
            int32_t bestDiff = 1000000;
            int32_t bestInd = 1;
            GifGetClosestPaletteColor(pPal, nextFrame[0], nextFrame[1], nextFrame[2], bestInd, bestDiff);

            // Write the resulting color to the output buffer
            outFrame[0] = pPal->r[bestInd];
            outFrame[1] = pPal->g[bestInd];
            outFrame[2] = pPal->b[bestInd];
            outFrame[3] = (uint8_t)bestInd;
        }

        if (lastFrame)
            lastFrame += 4;
        outFrame += 4;
        nextFrame += 4;
    }
}

// insert a single bit
void GifWriteBit(GifBitStatus& stat, uint32_t bit)
{
    bit = bit & 1;
    bit = bit << stat.bitIndex;
    stat.byte |= bit;

    ++stat.bitIndex;
    if (stat.bitIndex > 7)
    {
        // move the newly-finished byte to the chunk buffer
        stat.chunk[stat.chunkIndex++] = stat.byte;
        // and start a new byte
        stat.bitIndex = 0;
        stat.byte = 0;
    }
}

// write all bytes so far to the file
void GifWriteChunk(FILE* f, GifBitStatus& stat)
{
    fputc((int)stat.chunkIndex, f);
    fwrite(stat.chunk, 1, stat.chunkIndex, f);

    stat.bitIndex = 0;
    stat.byte = 0;
    stat.chunkIndex = 0;
}

void GifWriteCode(FILE* f, GifBitStatus& stat, uint32_t code, uint32_t length)
{
    for (uint32_t ii = 0; ii < length; ++ii)
    {
        GifWriteBit(stat, code);
        code = code >> 1;

        if (stat.chunkIndex == 255)
        {
            GifWriteChunk(f, stat);
        }
    }
}

// write a 256-color (8-bit) image palette to the file
void GifWritePalette(const GifPalette* pPal, FILE* f)
{
    fputc(0, f);  // first color: transparency
    fputc(0, f);
    fputc(0, f);

    for (int ii = 1; ii < (1 << pPal->bitDepth); ++ii)
    {
        uint32_t r = pPal->r[ii];
        uint32_t g = pPal->g[ii];
        uint32_t b = pPal->b[ii];

        fputc((int)r, f);
        fputc((int)g, f);
        fputc((int)b, f);
    }
}

// write the image header, LZW-compress and write out the image
void GifWriteLzwImage(FILE* f, uint8_t* image, uint32_t left, uint32_t top, uint32_t width, uint32_t height,
                      uint32_t delay, GifPalette* pPal)
{
    // graphics control extension
    fputc(0x21, f);
    fputc(0xf9, f);
    fputc(0x04, f);
    fputc(0x05, f);  // leave prev frame in place, this frame has transparency
    fputc(delay & 0xff, f);
    fputc((delay >> 8) & 0xff, f);
    fputc(kGifTransIndex, f);  // transparent color index
    fputc(0, f);

    fputc(0x2c, f);  // image descriptor block

    fputc(left & 0xff, f);  // corner of image in canvas space
    fputc((left >> 8) & 0xff, f);
    fputc(top & 0xff, f);
    fputc((top >> 8) & 0xff, f);

    fputc(width & 0xff, f);  // width and height of image
    fputc((width >> 8) & 0xff, f);
    fputc(height & 0xff, f);
    fputc((height >> 8) & 0xff, f);

    // fputc(0, f); // no local color table, no transparency
    // fputc(0x80, f); // no local color table, but transparency

    fputc(0x80 + pPal->bitDepth - 1, f);  // local color table present, 2 ^ bitDepth entries
    GifWritePalette(pPal, f);

    const int minCodeSize = pPal->bitDepth;
    const uint32_t clearCode = 1 << pPal->bitDepth;

    fputc(minCodeSize, f);  // min code size 8 bits

    GifLzwNode* codetree = (GifLzwNode*)GIF_TEMP_MALLOC(sizeof(GifLzwNode) * 4096);

    memset(codetree, 0, sizeof(GifLzwNode) * 4096);
    int32_t curCode = -1;
    uint32_t codeSize = (uint32_t)minCodeSize + 1;
    uint32_t maxCode = clearCode + 1;

    GifBitStatus stat;
    stat.byte = 0;
    stat.bitIndex = 0;
    stat.chunkIndex = 0;

    GifWriteCode(f, stat, clearCode, codeSize);  // start with a fresh LZW dictionary

    for (uint32_t yy = 0; yy < height; ++yy)
    {
        for (uint32_t xx = 0; xx < width; ++xx)
        {
#ifdef GIF_FLIP_VERT
            // bottom-left origin image (such as an OpenGL capture)
            uint8_t nextValue = image[((height - 1 - yy) * width + xx) * 4 + 3];
#else
            // top-left origin
            uint8_t nextValue = image[(yy * width + xx) * 4 + 3];
#endif

            // "loser mode" - no compression, every single code is followed immediately by a clear
            // WriteCode( f, stat, nextValue, codeSize );
            // WriteCode( f, stat, 256, codeSize );

            if (curCode < 0)
            {
                // first value in a new run
                curCode = nextValue;
            }
            else if (codetree[curCode].m_next[nextValue])
            {
                // current run already in the dictionary
                curCode = codetree[curCode].m_next[nextValue];
            }
            else
            {
                // finish the current run, write a code
                GifWriteCode(f, stat, (uint32_t)curCode, codeSize);

                // insert the new run into the dictionary
                codetree[curCode].m_next[nextValue] = (uint16_t)++maxCode;

                if (maxCode >= (1ul << codeSize))
                {
                    // dictionary entry count has broken a size barrier,
                    // we need more bits for codes
                    codeSize++;
                }
                if (maxCode == 4095)
                {
                    // the dictionary is full, clear it out and begin anew
                    GifWriteCode(f, stat, clearCode, codeSize);  // clear tree

                    memset(codetree, 0, sizeof(GifLzwNode) * 4096);
                    codeSize = (uint32_t)(minCodeSize + 1);
                    maxCode = clearCode + 1;
                }

                curCode = nextValue;
            }
        }
    }

    // compression footer
    GifWriteCode(f, stat, (uint32_t)curCode, codeSize);
    GifWriteCode(f, stat, clearCode, codeSize);
    GifWriteCode(f, stat, clearCode + 1, (uint32_t)minCodeSize + 1);

    // write out the last partial chunk
    while (stat.bitIndex)
        GifWriteBit(stat, 0);
    if (stat.chunkIndex)
        GifWriteChunk(f, stat);

    fputc(0, f);  // image block terminator

    GIF_TEMP_FREE(codetree);
}

// Creates a gif file.
// The input GIFWriter is assumed to be uninitialized.
// The delay value is the time between frames in hundredths of a second - note that not all viewers pay much attention
// to this value.
bool GifBegin(GifWriter* writer, const char* filename, uint32_t width, uint32_t height, uint32_t delay,
              int32_t bitDepth, bool dither)
{
    (void)bitDepth;
    (void)dither;  // Mute "Unused argument" warnings
#if defined(_MSC_VER) && (_MSC_VER >= 1400)
    writer->f = 0;
    fopen_s(&writer->f, filename, "wb");
#else
    writer->f = fopen(filename, "wb");
#endif
    if (!writer->f)
        return false;

    writer->firstFrame = true;

    // allocate
    writer->oldImage = (uint8_t*)GIF_MALLOC(width * height * 4);

    fputs("GIF89a", writer->f);

    // screen descriptor
    fputc(width & 0xff, writer->f);
    fputc((width >> 8) & 0xff, writer->f);
    fputc(height & 0xff, writer->f);
    fputc((height >> 8) & 0xff, writer->f);

    fputc(0xf0, writer->f);  // there is an unsorted global color table of 2 entries
    fputc(0, writer->f);     // background color
    fputc(0, writer->f);     // pixels are square (we need to specify this because it's 1989)

    // now the "global" palette (really just a dummy palette)
    // color 0: black
    fputc(0, writer->f);
    fputc(0, writer->f);
    fputc(0, writer->f);
    // color 1: also black
    fputc(0, writer->f);
    fputc(0, writer->f);
    fputc(0, writer->f);

    if (delay != 0)
    {
        // animation header
        fputc(0x21, writer->f);           // extension
        fputc(0xff, writer->f);           // application specific
        fputc(11, writer->f);             // length 11
        fputs("NETSCAPE2.0", writer->f);  // yes, really
        fputc(3, writer->f);              // 3 bytes of NETSCAPE2.0 data

        fputc(1, writer->f);  // JUST BECAUSE
        fputc(0, writer->f);  // loop infinitely (byte 0)
        fputc(0, writer->f);  // loop infinitely (byte 1)

        fputc(0, writer->f);  // block terminator
    }

    return true;
}

// Writes out a new frame to a GIF in progress.
// The GIFWriter should have been created by GIFBegin.
// AFAIK, it is legal to use different bit depths for different frames of an image -
// this may be handy to save bits in animations that don't change much.
bool GifWriteFrame(GifWriter* writer, const uint8_t* image, uint32_t width, uint32_t height, uint32_t delay,
                   int bitDepth, bool dither)
{
    if (!writer->f)
        return false;

    const uint8_t* oldImage = writer->firstFrame ? NULL : writer->oldImage;
    writer->firstFrame = false;

    GifPalette fakePal;
    GifMakePalette((dither ? NULL : oldImage), image, width, height, bitDepth, dither, &fakePal);

    if (writer->oldRawImage != nullptr && memcmp(writer->oldRawImage, image, width * height * 4) == 0)
    {
        // When adjacent frames have no differences at all - put fake image into resulting stream
        GifWriteLzwImage(writer->f, writer->oldImage, 0, 0, 1, 1, delay, &fakePal);
    }
    else
    {
        // When frames have differences - do full new frame encoding
        GifPalette pal;
        GifMakePalette((dither ? NULL : oldImage), image, width, height, bitDepth, dither, &pal);

        if (dither)
            GifDitherImage(oldImage, image, writer->oldImage, width, height, &pal);
        else
            GifThresholdImage(oldImage, image, writer->oldImage, width, height, &pal);

        GifWriteLzwImage(writer->f, writer->oldImage, 0, 0, width, height, delay, &pal);
    }

    if (writer->oldRawImage == nullptr)
    {
        writer->oldRawImage = new uint8_t[width * height * 4];
    }

    memcpy(writer->oldRawImage, image, width * height * 4);

    return true;
}

// Builds the k-d tree structure for a pre-built palette
// This must be called before using the palette with GifWriteFrameFast
void GifBuildPaletteTree(GifPalette* pPal)
{
    if (!pPal)
        return;

    const int numColors = 1 << pPal->bitDepth;

    // Build a simple balanced k-d tree for the palette
    // For small palettes (16-256 colors), we use a simplified approach
    // that still provides O(log n) lookup performance

    // Initialize tree with median splits
    for (int node = 1; node < numColors; node++)
    {
        // Determine which component to split on (cycle through R, G, B)
        int splitComp = 0;

        // Find the component with the largest range in this subtree
        int rMin = 255, rMax = 0, gMin = 255, gMax = 0, bMin = 255, bMax = 0;

        // Sample colors in this node's range
        int start = node;
        int end = node * 2;
        if (end > numColors)
            end = numColors;

        for (int i = start; i < end && i < numColors; i++)
        {
            if (pPal->r[i] < rMin)
                rMin = pPal->r[i];
            if (pPal->r[i] > rMax)
                rMax = pPal->r[i];
            if (pPal->g[i] < gMin)
                gMin = pPal->g[i];
            if (pPal->g[i] > gMax)
                gMax = pPal->g[i];
            if (pPal->b[i] < bMin)
                bMin = pPal->b[i];
            if (pPal->b[i] > bMax)
                bMax = pPal->b[i];
        }

        int rRange = rMax - rMin;
        int gRange = gMax - gMin;
        int bRange = bMax - bMin;

        if (gRange >= rRange && gRange >= bRange)
            splitComp = 1;
        else if (bRange >= rRange && bRange >= gRange)
            splitComp = 2;

        // Store split info
        pPal->treeSplitElt[node] = (uint8_t)splitComp;

        // Split at median value
        int splitVal = 0;
        switch (splitComp)
        {
            case 0:
                splitVal = (rMin + rMax) / 2;
                break;
            case 1:
                splitVal = (gMin + gMax) / 2;
                break;
            case 2:
                splitVal = (bMin + bMax) / 2;
                break;
        }
        pPal->treeSplit[node] = (uint8_t)splitVal;
    }
}

// Writes out a new frame using a pre-built palette (fast path - skips palette calculation)
bool GifWriteFrameFast(GifWriter* writer, const uint8_t* image, uint32_t width, uint32_t height, uint32_t delay,
                       GifPalette* palette, bool dither)
{
    if (!writer->f)
        return false;

    const uint8_t* oldImage = writer->firstFrame ? NULL : writer->oldImage;
    writer->firstFrame = false;

    // Skip GifMakePalette entirely - use the provided pre-built palette!

    // Check for identical frames (optimization)
    if (writer->oldRawImage != nullptr && memcmp(writer->oldRawImage, image, width * height * 4) == 0)
    {
        // Write a minimal 1x1 frame to maintain timing
        GifWriteLzwImage(writer->f, writer->oldImage, 0, 0, 1, 1, delay, palette);
    }
    else
    {
        // Apply palette to image (dithering or thresholding)
        if (dither)
            GifDitherImage(oldImage, image, writer->oldImage, width, height, palette);
        else
            GifThresholdImage(oldImage, image, writer->oldImage, width, height, palette);

        GifWriteLzwImage(writer->f, writer->oldImage, 0, 0, width, height, delay, palette);
    }

    // Track raw image for identical frame detection
    if (writer->oldRawImage == nullptr)
    {
        writer->oldRawImage = new uint8_t[width * height * 4];
    }
    memcpy(writer->oldRawImage, image, width * height * 4);

    return true;
}

// =============================================================================
// OPT-1: Direct ZX Spectrum Palette Index Lookup
// =============================================================================
// O(1) lookup bypassing k-d tree for exact ZX Spectrum colors.
// ZX Spectrum uses deterministic color encoding:
//   - Normal intensity: R/G/B = 0x00 or 0xCD
//   - Bright intensity: R/G/B = 0x00 or 0xFF
//   - Color index = (bright ? 8 : 0) + (blue ? 1 : 0) + (red ? 2 : 0) + (green ? 4 : 0)
// =============================================================================

uint8_t GifGetZXPaletteIndexDirect(uint8_t r, uint8_t g, uint8_t b)
{
    // Detect bright intensity (any channel at 0xFF)
    bool isBright = (r == 0xFF || g == 0xFF || b == 0xFF);
    uint8_t base = isBright ? 8 : 0;

    // Encode color bits using threshold at 0x80
    // This handles both normal (0xCD) and bright (0xFF) intensities
    uint8_t color = 0;
    if (b >= 0x80)
        color |= 0x01;  // Bit 0 = Blue
    if (r >= 0x80)
        color |= 0x02;  // Bit 1 = Red
    if (g >= 0x80)
        color |= 0x04;  // Bit 2 = Green

    return base + color;
}

// Optimized threshold using direct ZX lookup
// NOTE: Framebuffer is ABGR (little-endian): byte[0]=B, byte[1]=G, byte[2]=R
void GifThresholdImageZX(const uint8_t* lastFrame, const uint8_t* nextFrame, uint8_t* outFrame, uint32_t width,
                         uint32_t height, GifPalette* pPal)
{
    uint32_t numPixels = width * height;

    for (uint32_t ii = 0; ii < numPixels; ++ii)
    {
        // Delta detection: if pixel unchanged, mark transparent
        if (lastFrame && lastFrame[0] == nextFrame[0] && lastFrame[1] == nextFrame[1] && lastFrame[2] == nextFrame[2])
        {
            outFrame[0] = lastFrame[0];
            outFrame[1] = lastFrame[1];
            outFrame[2] = lastFrame[2];
            outFrame[3] = kGifTransIndex;
        }
        else
        {
            // Direct ZX palette lookup - O(1) instead of O(log n) k-d tree
            // Framebuffer is BGRA (little-endian): [0]=B, [1]=G, [2]=R
            uint8_t b = nextFrame[0];
            uint8_t g = nextFrame[1];
            uint8_t r = nextFrame[2];
            uint8_t bestInd = GifGetZXPaletteIndexDirect(r, g, b);

            // Write the resulting color to the output buffer
            outFrame[0] = pPal->r[bestInd];
            outFrame[1] = pPal->g[bestInd];
            outFrame[2] = pPal->b[bestInd];
            outFrame[3] = bestInd;
        }

        if (lastFrame)
            lastFrame += 4;
        outFrame += 4;
        nextFrame += 4;
    }
}

// =============================================================================
// GifWriteFrameZX: Maximum performance path for ZX Spectrum content
// =============================================================================
// Combines:
//   1. Fixed palette (no GifMakePalette)
//   2. Direct index lookup O(1) (no k-d tree traversal)
// This provides the fastest possible encoding for ZX Spectrum content.
// =============================================================================

bool GifWriteFrameZX(GifWriter* writer, const uint8_t* image, uint32_t width, uint32_t height, uint32_t delay,
                     GifPalette* palette)
{
    if (!writer->f || !palette)
        return false;

    const uint8_t* oldImage = writer->firstFrame ? nullptr : writer->oldImage;
    writer->firstFrame = false;

    // Check for identical frames (optimization)
    if (writer->oldRawImage != nullptr && memcmp(writer->oldRawImage, image, width * height * 4) == 0)
    {
        // Write a minimal 1x1 frame to maintain timing
        GifWriteLzwImage(writer->f, writer->oldImage, 0, 0, 1, 1, delay, palette);
    }
    else
    {
        // Use ZX-optimized threshold with direct index lookup
        GifThresholdImageZX(oldImage, image, writer->oldImage, width, height, palette);
        GifWriteLzwImage(writer->f, writer->oldImage, 0, 0, width, height, delay, palette);
    }

    // Track raw image for identical frame detection
    if (writer->oldRawImage == nullptr)
    {
        writer->oldRawImage = new uint8_t[width * height * 4];
    }
    memcpy(writer->oldRawImage, image, width * height * 4);

    return true;
}

// Writes a frame using hash lookup for exact color matching
bool GifWriteFrameExact(GifWriter* writer, const uint8_t* image, uint32_t width, uint32_t height, uint32_t delay,
                        GifPalette* palette, const GifColorLookup* lookup)
{
    if (!writer->f || !palette || !lookup)
        return false;

    const uint8_t* oldImage = writer->firstFrame ? nullptr : writer->oldImage;
    writer->firstFrame = false;

    // Check for identical frames (optimization)
    if (writer->oldRawImage != nullptr && memcmp(writer->oldRawImage, image, width * height * 4) == 0)
    {
        // Write a minimal 1x1 frame to maintain timing
        GifWriteLzwImage(writer->f, writer->oldImage, 0, 0, 1, 1, delay, palette);
    }
    else
    {
        // Use hash lookup for exact color matching
        GifThresholdImageExact(oldImage, image, writer->oldImage, width, height, lookup, palette);
        GifWriteLzwImage(writer->f, writer->oldImage, 0, 0, width, height, delay, palette);
    }

    // Track raw image for identical frame detection
    if (writer->oldRawImage == nullptr)
    {
        writer->oldRawImage = new uint8_t[width * height * 4];
    }
    memcpy(writer->oldRawImage, image, width * height * 4);

    return true;
}

// Writes the EOF code, closes the file handle, and frees temp memory used by a GIF.
// Many if not most viewers will still display a GIF properly if the EOF code is missing,
// but it's still a good idea to write it out.
bool GifEnd(GifWriter* writer)
{
    if (!writer->f)
        return false;

    fputc(0x3b, writer->f);  // end of file
    fclose(writer->f);
    GIF_FREE(writer->oldImage);

    if (writer->oldRawImage != nullptr)
        delete[] writer->oldRawImage;

    writer->f = NULL;
    writer->oldImage = NULL;

    return true;
}

// =============================================================================
// Hash Table Color Lookup - O(1) exact color matching
// =============================================================================
// Uses FNV-1a hash with open addressing and linear probing
// Table size is 512 (power of 2) for fast modulo via bitmask

static inline uint32_t GifHashColor(uint32_t abgr)
{
    // FNV-1a hash for 32-bit value
    uint32_t h = 2166136261u;
    h = (h ^ (abgr & 0xFF)) * 16777619u;
    h = (h ^ ((abgr >> 8) & 0xFF)) * 16777619u;
    h = (h ^ ((abgr >> 16) & 0xFF)) * 16777619u;
    h = (h ^ ((abgr >> 24) & 0xFF)) * 16777619u;
    return h;
}

void GifBuildColorLookup(GifColorLookup* lookup, const GifPalette* pPal)
{
    if (!lookup || !pPal)
        return;

    // Clear table
    memset(lookup->keys, 0, sizeof(lookup->keys));
    memset(lookup->values, 0, sizeof(lookup->values));
    memset(lookup->occupied, 0, sizeof(lookup->occupied));

    int numColors = 1 << pPal->bitDepth;
    lookup->numColors = (uint16_t)numColors;

    // Insert each palette color
    for (int i = 0; i < numColors; i++)
    {
        // Build ABGR color from palette RGB (alpha = 0xFF)
        uint32_t abgr = 0xFF000000 | ((uint32_t)pPal->b[i] << 16) | ((uint32_t)pPal->g[i] << 8) | (uint32_t)pPal->r[i];

        // Find slot using linear probing
        uint32_t hash = GifHashColor(abgr);
        uint32_t slot = hash & (GifColorLookup::TABLE_SIZE - 1);

        while (lookup->occupied[slot] && lookup->keys[slot] != abgr)
        {
            slot = (slot + 1) & (GifColorLookup::TABLE_SIZE - 1);
        }

        lookup->keys[slot] = abgr;
        lookup->values[slot] = (uint8_t)i;
        lookup->occupied[slot] = true;
    }

    lookup->valid = true;
}

uint8_t GifGetColorIndex(const GifColorLookup* lookup, uint32_t abgrColor)
{
    if (!lookup || !lookup->valid)
        return 0;

    uint32_t hash = GifHashColor(abgrColor);
    uint32_t slot = hash & (GifColorLookup::TABLE_SIZE - 1);

    // Linear probe to find exact match
    int probes = 0;
    while (probes < GifColorLookup::TABLE_SIZE)
    {
        if (!lookup->occupied[slot])
        {
            // Empty slot means color not found
            return 0;
        }
        if (lookup->keys[slot] == abgrColor)
        {
            return lookup->values[slot];
        }
        slot = (slot + 1) & (GifColorLookup::TABLE_SIZE - 1);
        probes++;
    }

    return 0;  // Not found, return index 0
}

void GifThresholdImageExact(const uint8_t* lastFrame, const uint8_t* nextFrame, uint8_t* outFrame, uint32_t width,
                            uint32_t height, const GifColorLookup* lookup, GifPalette* pPal)
{
    uint32_t numPixels = width * height;

    for (uint32_t i = 0; i < numPixels; ++i)
    {
        // if a previous color is available, and it matches the current color,
        // set the pixel to transparent
        if (lastFrame && lastFrame[0] == nextFrame[0] && lastFrame[1] == nextFrame[1] && lastFrame[2] == nextFrame[2])
        {
            outFrame[0] = lastFrame[0];
            outFrame[1] = lastFrame[1];
            outFrame[2] = lastFrame[2];
            outFrame[3] = kGifTransIndex;
        }
        else
        {
            // Build ABGR from RGBA framebuffer (or BGRA on little-endian)
            // Memory layout: [R, G, B, A] or [B, G, R, A] depending on platform
            // The framebuffer is ABGR little-endian, so bytes are: [R, G, B, A]
            uint32_t abgr = 0xFF000000 | ((uint32_t)nextFrame[2] << 16) |  // B
                            ((uint32_t)nextFrame[1] << 8) |                // G
                            (uint32_t)nextFrame[0];                        // R

            uint8_t bestInd = GifGetColorIndex(lookup, abgr);

            // Write the resulting color to the output buffer
            outFrame[0] = pPal->r[bestInd];
            outFrame[1] = pPal->g[bestInd];
            outFrame[2] = pPal->b[bestInd];
            outFrame[3] = bestInd;
        }

        if (lastFrame)
            lastFrame += 4;
        outFrame += 4;
        nextFrame += 4;
    }
}
