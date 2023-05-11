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



#ifndef _TINY_WAV_
#define _TINY_WAV_

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// http://soundfile.sapp.org/doc/WaveFormat/

typedef struct TinyWavHeader
{
  uint32_t ChunkID;
  uint32_t ChunkSize;
  uint32_t Format;
  uint32_t Subchunk1ID;
  uint32_t Subchunk1Size;
  uint16_t AudioFormat;
  uint16_t NumChannels;
  uint32_t SampleRate;
  uint32_t ByteRate;
  uint16_t BlockAlign;
  uint16_t BitsPerSample;
  uint32_t Subchunk2ID;
  uint32_t Subchunk2Size;
} TinyWavHeader;
  
typedef enum TinyWavChannelFormat
{
  TW_INTERLEAVED, // channel buffer is interleaved e.g. [LRLRLRLR]
  TW_INLINE,      // channel buffer is inlined e.g. [LLLLRRRR]
  TW_SPLIT        // channel buffer is split e.g. [[LLLL],[RRRR]]
} TinyWavChannelFormat;

typedef enum TinyWavSampleFormat
{
  TW_INT16 = 2,  // two byte signed integer
  TW_FLOAT32 = 4 // four byte IEEE float
} TinyWavSampleFormat;

typedef struct TinyWav
{
  FILE *file = nullptr;
  TinyWavHeader h;
  int16_t numChannels;
  int32_t numFramesInHeader;         ///< number of samples per channel declared in wav header (only populated when reading)
  uint32_t totalFramesReadWritten;     ///< total numSamples per channel which have been read or written
  TinyWavChannelFormat chanFmt;
  TinyWavSampleFormat sampleFmt;
} TinyWav;

/**
 * Open a file for writing as file
 *
 * @param numChannels  The number of channels to write.
 * @param sampleRate   The sample rate of the audio.
 * @param sampleFmt      The sample format (e.g. 16-bit integer or 32-bit float) to be used in the file.
 * @param channelFmt      The channel format (how the channel data is layed out in memory)
 * @param path         The path of the file to write to. The file will be overwritten.
 *
 * @return  The error code. Zero if no error.
 */
int tinywav_open_write(TinyWav *tw,
                       int16_t numChannels, int32_t sampleRate,
                       TinyWavSampleFormat sampleFmt, TinyWavChannelFormat channelFmt,
                       const char *path);

/**
 * Open a file for reading.
 *
 * @param path     The path of the file to read.
 * @param chanFmt  The channel format (how the channel data is layed out in memory) when read.
 *
 * @return  The error code. Zero if no error.
 */
int tinywav_open_read(TinyWav *tw, const char *path, TinyWavChannelFormat chanFmt);

/**
 * Read sample data from the file.
 *
 * @param tw   The TinyWav structure which has already been prepared.
 * @param data  A pointer to the data structure to read to. This data is expected to have the
 *              correct memory layout to match the specifications given in tinywav_open_read().
 * @param len   The number of frames to read.
 *
 * @return The number of frames (samples per channel) read from file.
 */
int tinywav_read_f(TinyWav *tw, void *data, int len);

/** Stop reading the file. The Tinywav struct is now invalid. */
void tinywav_close_read(TinyWav *tw);

/**
 * Write sample data to file.
 *
 * @param tw   The TinyWav structure which has already been prepared.
 * @param in   A pointer to the sample data to write (samples are int16)
 * @param lenSamples  The number of frames to write.
 *
 * @return The number of frames (samples per channel) written to file.
 */
int tinywav_write_i(TinyWav *tw, void *in, int lenSamples);

/**
 * Write sample data to file.
 *
 * @param tw   The TinyWav structure which has already been prepared.
 * @param f    A pointer to the sample data to write (samples are float)
 * @param lenSamples  The number of frames to write.
 *
 * @return The number of frames (samples per channel) written to file.
 */
int tinywav_write_f(TinyWav *tw, void *f, int lenSamples);

/** Stop writing to the file. The Tinywav struct is now invalid. */
void tinywav_close_write(TinyWav *tw);

/** Returns true if the Tinywav struct is available to write or write. False otherwise. */
bool tinywav_isOpen(TinyWav *tw);
  
#ifdef __cplusplus
}
#endif

#endif // _TINY_WAV_
