// UNAudioOutput.h - CoreAudio (AudioUnit) playback of the emulator's int16 stereo stream.
//
// Plain Objective-C only - reachable from Swift through the bridging header.

#pragma once

#import <Foundation/Foundation.h>

/// Sample rate produced by the core (core/src/emulator/sound/audio.h: AUDIO_SAMPLING_RATE)
extern const double UNAudioSampleRate;
/// Channel count produced by the core (interleaved stereo)
extern const NSUInteger UNAudioChannels;

@interface UNAudioOutput : NSObject

/// Invoked from the CoreAudio render thread whenever the ring buffer drops below
/// half full. The emulator's main loop throttles on this signal, so the block must
/// be cheap and must not block.
@property (nonatomic, copy, nullable) void (^needsMoreDataHandler)(void);

- (BOOL)start;
- (void)stop;

/// Producer side: called from the emulator worker thread.
- (void)enqueueSamples:(const int16_t * _Nonnull)samples count:(NSUInteger)count;

/// Drop everything currently queued (used when the emulator stops).
- (void)flush;

@property (nonatomic, readonly) BOOL isRunning;

@end
