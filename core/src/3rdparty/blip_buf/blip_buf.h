#pragma once

/**
 * blip_buf - Band-limited sound synthesis buffer
 * Clean C++ port of Shay Green's blip_buf library.
 */

#include <cstdint>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct blip_t blip_t;

enum {
    blip_max_ratio = 1 << 20,
    blip_max_frame = 4000
};

/** Creates new buffer that can hold at most sample_count samples. */
blip_t* blip_new(int sample_count);

/** Sets approximate input clock rate and output sample rate. */
void blip_set_rates(blip_t* buf, double clock_rate, double sample_rate);

/** Clears entire buffer. Afterwards, blip_samples_avail() == 0. */
void blip_clear(blip_t* buf);

/** Adds positive/negative delta into buffer at specified clock time. */
void blip_add_delta(blip_t* buf, unsigned int clock_time, int delta);

/** Same as blip_add_delta(), but uses faster, lower-quality synthesis. */
void blip_add_delta_fast(blip_t* buf, unsigned int clock_time, int delta);

/** Length of time frame, in clocks, needed to make sample_count additional samples available. */
int blip_clocks_needed(const blip_t* buf, int sample_count);

/** Makes input clocks before clock_duration available for reading as output samples. */
void blip_end_frame(blip_t* buf, unsigned int clock_duration);

/** Number of buffered samples available for reading. */
int blip_samples_avail(const blip_t* buf);

/** Reads and removes at most 'count' samples and writes them to 'out'. */
int blip_read_samples(blip_t* buf, short out[], int count, int stereo);

/** Frees buffer. No effect if NULL is passed. */
void blip_delete(blip_t* buf);

typedef blip_t blip_buffer_t;

#ifdef __cplusplus
}
#endif
