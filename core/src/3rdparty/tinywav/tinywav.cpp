/**
 * Copyright (c) 2015-2022, Martin Roth (mhroth@gmail.com)
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */



#include <assert.h>
#include <string.h>
#if _WIN32
#include <winsock.h>
#include <malloc.h>
#ifdef _MSC_VER
    // Only use pragma comment for MSVC
    #pragma comment(lib, "Ws2_32.lib")
#endif
#if defined(__GNUC__) || defined(__clang__)
    // For MinGW or other Windows compilers
    // No pragma, link via compiler flag (-lws2_32)
#endif
#else
#include <alloca.h>
#include <netinet/in.h>
#endif
#include "tinywav.h"
#include <cstdlib>

int tinywav_open_write(TinyWav *tw,
                       int16_t numChannels, int32_t sampleRate,
                       TinyWavSampleFormat sampleFmt, TinyWavChannelFormat channelFmt,
                       const char *path)
{
#if _WIN32
    [[maybe_unused]] errno_t err = fopen_s(&tw->file, path, "wb");
    char buf[1024];
    strerror_s(buf, sizeof(buf), err);
    fprintf(stderr, "Failed to open file: %s\n", buf);
    assert(err == 0);
#else
    tw->file = fopen(path, "wb");
#endif
    assert(tw->file != NULL);
    tw->numChannels = numChannels;
    tw->numFramesInHeader = -1; // not used for writer
    tw->totalFramesReadWritten = 0;
    tw->sampleFmt = sampleFmt;
    tw->chanFmt = channelFmt;

    uint32_t sampleSize = tw->sampleFmt; // (2 for PCM and 4 for IEEE float)

    // Prepare WAV header
    TinyWavHeader wavHeader;
    wavHeader.ChunkID = htonl(0x52494646);          // "RIFF"
    wavHeader.ChunkSize = sizeof(TinyWavHeader) - 8;// Will be updated on file-close (=Total file size - 8)
    wavHeader.Format = htonl(0x57415645);           // "WAVE"
    wavHeader.Subchunk1ID = htonl(0x666d7420);      // "fmt "
    wavHeader.Subchunk1Size = 16;                   // PCM
    wavHeader.AudioFormat = (tw->sampleFmt - 1);    // 1 PCM, 3 IEEE float
    wavHeader.NumChannels = numChannels;
    wavHeader.SampleRate = sampleRate;
    wavHeader.ByteRate = sampleRate * numChannels * sampleSize;
    wavHeader.BlockAlign = numChannels * tw->sampleFmt;
    wavHeader.BitsPerSample = 8 * tw->sampleFmt;
    wavHeader.Subchunk2ID = htonl(0x64617461);      // "data"
    wavHeader.Subchunk2Size = 0;                    // Will be populated on file-close (=Sample data size)

    // write WAV header
    fwrite(&wavHeader, sizeof(TinyWavHeader), 1, tw->file);

    return 0;
}

int tinywav_open_read(TinyWav *tw, const char *path, TinyWavChannelFormat chanFmt) {
  tw->file = fopen(path, "rb");
  if (tw->file == NULL) return -1;
  
  size_t ret = fread(&tw->h, sizeof(TinyWavHeader), 1, tw->file);
  if (ret != 1) return -1;

  if (tw->h.ChunkID != htonl(0x52494646) ||        // "RIFF"
      tw->h.Format != htonl(0x57415645) ||         // "WAVE"
      tw->h.Subchunk1ID != htonl(0x666d7420))      // "fmt "
  {
    return -1;
  }
  
  // skip over any other chunks before the "data" chunk
  bool additionalHeaderDataPresent = false;
  while (tw->h.Subchunk2ID != htonl(0x64617461)) {   // "data"
    fseek(tw->file, 4, SEEK_CUR);
    size_t bytes_read = fread(&tw->h.Subchunk2ID, 4, 1, tw->file);
    if (bytes_read != 1) {
      return -1; // Error reading file
    }
    uint32_t bytes_to_read;
    if (fread(&bytes_to_read, 4, 1, tw->file) != 1) return -1;
    // Skip the extra data chunk
    if (fseek(tw->file, bytes_to_read, SEEK_CUR) != 0) return -1;
    additionalHeaderDataPresent = true;
  }
  assert(tw->h.Subchunk2ID == htonl(0x64617461));    // "data"
  if (additionalHeaderDataPresent) {
    // read the value of Subchunk2Size, the one populated when reading 'TinyWavHeader' structure is wrong
    size_t bytes_read;
    bytes_read = fread(&tw->h.Subchunk2Size, 4, 1, tw->file);
    if (bytes_read != 1) return -1;
    assert(bytes_read == 1); // Ensure we read the expected amount of data
  }
    
  tw->numChannels = tw->h.NumChannels;
  tw->chanFmt = chanFmt;

  if (tw->h.BitsPerSample == 32 && tw->h.AudioFormat == 3) {
    tw->sampleFmt = TW_FLOAT32; // file has 32-bit IEEE float samples
  } else if (tw->h.BitsPerSample == 16 && tw->h.AudioFormat == 1) {
    tw->sampleFmt = TW_INT16; // file has 16-bit int samples
  } else {
    tw->sampleFmt = TW_FLOAT32;
    printf("Warning: wav file has %d bits per sample (int), which is not natively supported yet. Treating them as float; you may want to convert them manually after reading.\n", tw->h.BitsPerSample);
  }

  tw->numFramesInHeader = tw->h.Subchunk2Size / (tw->numChannels * tw->sampleFmt);
  tw->totalFramesReadWritten = 0;
  
  return 0;
}

int tinywav_read_f(TinyWav *tw, void *data, int len) {
  switch (tw->sampleFmt) {
    case TW_INT16: {
      int16_t *interleaved_data = (int16_t *) alloca(tw->numChannels*len*sizeof(int16_t));
      size_t samples_read = fread(interleaved_data, sizeof(int16_t), tw->numChannels*len, tw->file);
      int valid_len = (int) samples_read / tw->numChannels;
      switch (tw->chanFmt) {
        case TW_INTERLEAVED: { // channel buffer is interleaved e.g. [LRLRLRLR]
          for (int pos = 0; pos < tw->numChannels * valid_len; pos++) {
            ((float *) data)[pos] = (float) interleaved_data[pos] / INT16_MAX;
          }
          return valid_len;
        }
        case TW_INLINE: { // channel buffer is inlined e.g. [LLLLRRRR]
          for (int i = 0, pos = 0; i < tw->numChannels; i++) {
            for (int j = i; j < valid_len * tw->numChannels; j += tw->numChannels, ++pos) {
              ((float *) data)[pos] = (float) interleaved_data[j] / INT16_MAX;
            }
          }
          return valid_len;
        }
        case TW_SPLIT: { // channel buffer is split e.g. [[LLLL],[RRRR]]
          for (int i = 0, pos = 0; i < tw->numChannels; i++) {
            for (int j = 0; j < valid_len; j++, ++pos) {
              ((float **) data)[i][j] = (float) interleaved_data[j*tw->numChannels + i] / INT16_MAX;
            }
          }
          return valid_len;
        }
        default: return 0;
      }
    }
    case TW_FLOAT32: {
      float *interleaved_data = (float *) alloca(tw->numChannels*len*sizeof(float));
      size_t samples_read = fread(interleaved_data, sizeof(float), tw->numChannels*len, tw->file);
      int valid_len = (int) samples_read / tw->numChannels;
      switch (tw->chanFmt) {
        case TW_INTERLEAVED: { // channel buffer is interleaved e.g. [LRLRLRLR]
          memcpy(data, interleaved_data, tw->numChannels*valid_len*sizeof(float));
          return valid_len;
        }
        case TW_INLINE: { // channel buffer is inlined e.g. [LLLLRRRR]
          for (int i = 0, pos = 0; i < tw->numChannels; i++) {
            for (int j = i; j < valid_len * tw->numChannels; j += tw->numChannels, ++pos) {
              ((float *) data)[pos] = interleaved_data[j];
            }
          }
          return valid_len;
        }
        case TW_SPLIT: { // channel buffer is split e.g. [[LLLL],[RRRR]]
          for (int i = 0, pos = 0; i < tw->numChannels; i++) {
            for (int j = 0; j < valid_len; j++, ++pos) {
              ((float **) data)[i][j] = interleaved_data[j*tw->numChannels + i];
            }
          }
          return valid_len;
        }
        default: return 0;
      }
    }
    default: return 0;
  }

  return len;
}

void tinywav_close_read(TinyWav *tw) {
    if (tw->file) {
        fflush(tw->file);
        fclose(tw->file);
        tw->file = NULL;
    }
}

int tinywav_write_i(TinyWav *tw, void *in, int lenSamples)
{
    if (!tw->file)
        return -1;

    int result = 0;
    bool saveFile = true;
    void* buffer = nullptr;

    switch (tw->sampleFmt)
    {
        case TW_INT16:
        {
            buffer = malloc(tw->numChannels * lenSamples * sizeof(int16_t));    // Allocates memory. Will be freed at the end of function execution
            int16_t *z = (int16_t *)buffer;

            switch (tw->chanFmt)
            {
                case TW_INTERLEAVED:
                {
                    // No need to copy data since already in place
                    tw->totalFramesReadWritten += lenSamples;
                    result = (int)fwrite(in, sizeof(uint16_t), tw->numChannels * lenSamples, tw->file);
                    saveFile = false;
                    break;
                }
                case TW_INLINE:
                {
                    const int16_t *x = (const int16_t *)in;
                    for (int i = 0, k = 0; i < lenSamples; ++i)
                    {
                        for (int j = 0; j < tw->numChannels; ++j)
                        {
                            z[k++] = x[j * lenSamples + i];
                        }
                    }
                    break;
                }
                case TW_SPLIT:
                {
                    const int16_t **x = (const int16_t **)in;
                    for (int i = 0, k = 0; i < lenSamples; ++i)
                    {
                        for (int j = 0; j < tw->numChannels; ++j)
                        {
                            z[k++] = (int16_t)x[j][i];
                        }
                    }
                    break;
                }
                default:
                    return 0;
            }

            if (saveFile)
            {
                tw->totalFramesReadWritten += lenSamples;
                size_t samples_written = fwrite(z, sizeof(int16_t), tw->numChannels * lenSamples, tw->file);
                result = (int)samples_written / tw->numChannels;
            }
            break;
        }
        case TW_FLOAT32:
        {
            buffer = alloca(tw->numChannels * lenSamples * sizeof(float));
            float *z = (float *)buffer;

            switch (tw->chanFmt)
            {
                case TW_INTERLEAVED:
                {
                    const int16_t *x = (const int16_t *)in;
                    for (int i = 0; i < tw->numChannels * lenSamples; ++i)
                    {
                        z[i] = (float)x[i] / (float)INT16_MAX;
                    }
                    break;
                }
                case TW_INLINE:
                {
                    const int16_t *x = (const int16_t *)in;
                    for (int i = 0, k = 0; i < lenSamples; ++i)
                    {
                        for (int j = 0; j < tw->numChannels; ++j)
                        {
                            z[k++] = (float)x[j * lenSamples + i] / (float)INT16_MAX;
                        }
                    }
                    break;
                }
                case TW_SPLIT:
                {
                    const int16_t **x = (const int16_t **)in;
                    for (int i = 0, k = 0; i < lenSamples; ++i)
                    {
                        for (int j = 0; j < tw->numChannels; ++j)
                        {
                            z[k++] = (float)x[j][i] / (float)INT16_MAX;
                        }
                    }
                    break;
                }
                default:
                    result = 0;
                    break;
            }

            if (saveFile)
            {
                tw->totalFramesReadWritten += lenSamples;
                size_t samples_written = fwrite(z, sizeof(float), tw->numChannels * lenSamples, tw->file);
                result = (int) samples_written / tw->numChannels;
            }
            break;
        }
        default:
            result = 0;
            break;
    }

    if (buffer)
    {
        free(buffer);
    }

    return result;
}

int tinywav_write_f(TinyWav *tw, void *f, int lenSamples)
{
    if (!tw->file)
        return -1;

    int result = 0;
    bool saveFile = true;
    void* buffer = nullptr;

    switch (tw->sampleFmt)
    {
        case TW_INT16:
        {
            buffer = malloc(tw->numChannels * lenSamples * sizeof(int16_t));
            int16_t *z = (int16_t *)buffer;

            switch (tw->chanFmt)
            {
                case TW_INTERLEAVED:
                {
                    const float *x = (const float *) f;
                    for (int i = 0; i < tw->numChannels * lenSamples; ++i)
                    {
                        z[i] = (int16_t) (x[i] * (float) INT16_MAX);
                    }
                    break;
                }
                case TW_INLINE:
                {
                    const float *x = (const float *) f;
                    for (int i = 0, k = 0; i < lenSamples; ++i)
                    {
                        for (int j = 0; j < tw->numChannels; ++j)
                        {
                            z[k++] = (int16_t) (x[j * lenSamples + i] * (float) INT16_MAX);
                        }
                    }
                    break;
                }
                case TW_SPLIT:
                {
                    const float **x = (const float **) f;
                    for (int i = 0, k = 0; i < lenSamples; ++i)
                    {
                        for (int j = 0; j < tw->numChannels; ++j)
                        {
                            z[k++] = (int16_t) (x[j][i] * (float) INT16_MAX);
                        }
                    }
                    break;
                }
                default:
                    result = -1;
                    saveFile = false;
                    break;
            }

            if (saveFile)
            {
                tw->totalFramesReadWritten += lenSamples;
                size_t samples_written = fwrite(z, sizeof(int16_t), tw->numChannels * lenSamples, tw->file);
                result = (int)samples_written / tw->numChannels;
            }

            break;
        }
        case TW_FLOAT32:
        {
            buffer = malloc(tw->numChannels * lenSamples * sizeof(float));
            float *z = (float *)buffer;

            switch (tw->chanFmt)
            {
                case TW_INTERLEAVED:
                {
                    tw->totalFramesReadWritten += lenSamples;
                    result = (int)fwrite(f, sizeof(float), tw->numChannels * lenSamples, tw->file);
                    saveFile = false;
                    break;
                }
                case TW_INLINE:
                {
                    const float *x = (const float *) f;
                    for (int i = 0, k = 0; i < lenSamples; ++i)
                    {
                        for (int j = 0; j < tw->numChannels; ++j)
                        {
                            z[k++] = x[j * lenSamples + i];
                        }
                    }
                    break;
                }
                case TW_SPLIT:
                {
                    const float **x = (const float **) f;
                    for (int i = 0, k = 0; i < lenSamples; ++i)
                    {
                        for (int j = 0; j < tw->numChannels; ++j)
                        {
                            z[k++] = x[j][i];
                        }
                    }
                    break;
                }
                default:
                    result = 0;
                    saveFile = false;
                    break;
            }

            if (saveFile)
            {
                tw->totalFramesReadWritten += lenSamples;
                size_t samples_written = fwrite(z, sizeof(float), tw->numChannels * lenSamples, tw->file);
                result = (int)samples_written / tw->numChannels;
            }

            break;
        }
        default:
            result = 0;
            break;
    }

    if (buffer)
    {
        free(buffer);
    }

    return result;
}

void tinywav_close_write(TinyWav *tw)
{
    if (tw->file == nullptr)
        return;

    uint32_t data_len = tw->totalFramesReadWritten * tw->numChannels * tw->sampleFmt;

    // Update chunk_size field in WAVE file header
    fseek(tw->file, 4, SEEK_SET);
    uint32_t chunkSize_len = 36 + data_len;
    fwrite(&chunkSize_len, sizeof(uint32_t), 1, tw->file);

    // Update subchunk2_size field in WAVE file header (size of sample data in bytes)
    fseek(tw->file, 40, SEEK_SET);
    fwrite(&data_len, sizeof(uint32_t), 1, tw->file);

    fclose(tw->file);
    tw->file = nullptr;
}

bool tinywav_isOpen(TinyWav *tw)
{
  return (tw->file != nullptr);
}
