#import "UNAudioOutput.h"

#import <AudioToolbox/AudioToolbox.h>

#include <atomic>
#include <algorithm>
#include <cstring>
#include <vector>

const double UNAudioSampleRate = 44100.0;   // core: AUDIO_SAMPLING_RATE
const NSUInteger UNAudioChannels = 2;       // core: AUDIO_CHANNELS (interleaved stereo)

namespace
{
    /// Lock-free single-producer / single-consumer ring buffer of interleaved int16 samples.
    /// Producer: emulator worker thread (audio callback from the core).
    /// Consumer: CoreAudio render thread.
    class SampleRing
    {
    public:
        explicit SampleRing(size_t capacity) : _buffer(capacity), _capacity(capacity) {}

        size_t capacity() const { return _capacity; }

        size_t available() const
        {
            const size_t w = _write.load(std::memory_order_acquire);
            const size_t r = _read.load(std::memory_order_acquire);
            return w - r;
        }

        void write(const int16_t* src, size_t count)
        {
            size_t w = _write.load(std::memory_order_relaxed);
            const size_t r = _read.load(std::memory_order_acquire);
            const size_t free = _capacity - (w - r);

            if (count > free)
            {
                // Overrun: drop the oldest samples instead of blocking the emulator thread
                const size_t drop = count - free;
                _read.store(r + drop, std::memory_order_release);
            }

            for (size_t i = 0; i < count; i++)
            {
                _buffer[(w + i) % _capacity] = src[i];
            }

            _write.store(w + count, std::memory_order_release);
        }

        /// Reads `count` samples, zero-filling whatever is missing (underrun => silence).
        void read(int16_t* dst, size_t count)
        {
            size_t r = _read.load(std::memory_order_relaxed);
            const size_t w = _write.load(std::memory_order_acquire);
            const size_t have = std::min(count, w - r);

            for (size_t i = 0; i < have; i++)
            {
                dst[i] = _buffer[(r + i) % _capacity];
            }
            if (have < count)
            {
                std::memset(dst + have, 0, (count - have) * sizeof(int16_t));
            }

            _read.store(r + have, std::memory_order_release);
        }

        void clear()
        {
            _read.store(0, std::memory_order_release);
            _write.store(0, std::memory_order_release);
        }

    private:
        std::vector<int16_t> _buffer;
        size_t _capacity;
        std::atomic<size_t> _read{0};
        std::atomic<size_t> _write{0};
    };

    // 882 samples per channel per 50Hz frame * 2 channels * 8 frames of slack
    constexpr size_t kRingCapacity = 882 * 2 * 8;
}

@implementation UNAudioOutput
{
    AudioUnit _unit;
    BOOL _unitCreated;
    SampleRing* _ring;
}

- (instancetype)init
{
    self = [super init];
    if (self)
    {
        _ring = new SampleRing(kRingCapacity);
    }
    return self;
}

- (void)dealloc
{
    [self stop];
    if (_unitCreated)
    {
        AudioUnitUninitialize(_unit);
        AudioComponentInstanceDispose(_unit);
        _unitCreated = NO;
    }
    delete _ring;
    _ring = nullptr;
}

#pragma mark - Render callback

static OSStatus UNRenderCallback(void* inRefCon,
                                 AudioUnitRenderActionFlags* ioActionFlags,
                                 const AudioTimeStamp* inTimeStamp,
                                 UInt32 inBusNumber,
                                 UInt32 inNumberFrames,
                                 AudioBufferList* ioData)
{
    (void)ioActionFlags;
    (void)inTimeStamp;
    (void)inBusNumber;

    UNAudioOutput* self = (__bridge UNAudioOutput*)inRefCon;
    return [self renderFrames:inNumberFrames into:ioData];
}

- (OSStatus)renderFrames:(UInt32)frameCount into:(AudioBufferList*)ioData
{
    if (ioData->mNumberBuffers < 1)
        return noErr;

    float* out = (float*)ioData->mBuffers[0].mData;
    const size_t sampleCount = (size_t)frameCount * UNAudioChannels;

    // Small stack scratch, resized only for unusually large render quanta
    int16_t stack[4096];
    int16_t* scratch = stack;
    std::vector<int16_t> heap;
    if (sampleCount > sizeof(stack) / sizeof(stack[0]))
    {
        heap.resize(sampleCount);
        scratch = heap.data();
    }

    _ring->read(scratch, sampleCount);

    for (size_t i = 0; i < sampleCount; i++)
    {
        out[i] = (float)scratch[i] / 32768.0f;
    }

    // Let the emulator know it may produce more audio (drives frame pacing in MainLoop)
    if (_ring->available() < _ring->capacity() / 2)
    {
        void (^handler)(void) = self.needsMoreDataHandler;
        if (handler)
            handler();
    }

    return noErr;
}

#pragma mark - Lifecycle

- (BOOL)createUnitIfNeeded
{
    if (_unitCreated)
        return YES;

    AudioComponentDescription desc = {};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent comp = AudioComponentFindNext(NULL, &desc);
    if (comp == NULL)
    {
        NSLog(@"[UNAudioOutput] Default output AudioUnit not found");
        return NO;
    }

    OSStatus status = AudioComponentInstanceNew(comp, &_unit);
    if (status != noErr)
    {
        NSLog(@"[UNAudioOutput] AudioComponentInstanceNew failed: %d", (int)status);
        return NO;
    }

    // Interleaved float32 stereo. The core hands us int16; we convert in the render callback.
    AudioStreamBasicDescription fmt = {};
    fmt.mSampleRate = UNAudioSampleRate;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    fmt.mChannelsPerFrame = (UInt32)UNAudioChannels;
    fmt.mBitsPerChannel = 32;
    fmt.mFramesPerPacket = 1;
    fmt.mBytesPerFrame = (UInt32)(UNAudioChannels * sizeof(float));
    fmt.mBytesPerPacket = fmt.mBytesPerFrame;

    status = AudioUnitSetProperty(_unit, kAudioUnitProperty_StreamFormat,
                                  kAudioUnitScope_Input, 0, &fmt, sizeof(fmt));
    if (status != noErr)
    {
        NSLog(@"[UNAudioOutput] Failed to set stream format: %d", (int)status);
        AudioComponentInstanceDispose(_unit);
        return NO;
    }

    AURenderCallbackStruct cb = {};
    cb.inputProc = UNRenderCallback;
    cb.inputProcRefCon = (__bridge void*)self;
    status = AudioUnitSetProperty(_unit, kAudioUnitProperty_SetRenderCallback,
                                  kAudioUnitScope_Input, 0, &cb, sizeof(cb));
    if (status != noErr)
    {
        NSLog(@"[UNAudioOutput] Failed to set render callback: %d", (int)status);
        AudioComponentInstanceDispose(_unit);
        return NO;
    }

    status = AudioUnitInitialize(_unit);
    if (status != noErr)
    {
        NSLog(@"[UNAudioOutput] AudioUnitInitialize failed: %d", (int)status);
        AudioComponentInstanceDispose(_unit);
        return NO;
    }

    _unitCreated = YES;
    return YES;
}

- (BOOL)start
{
    if (_isRunning)
        return YES;
    if (![self createUnitIfNeeded])
        return NO;

    OSStatus status = AudioOutputUnitStart(_unit);
    if (status != noErr)
    {
        NSLog(@"[UNAudioOutput] AudioOutputUnitStart failed: %d", (int)status);
        return NO;
    }

    _isRunning = YES;
    return YES;
}

- (void)stop
{
    if (!_isRunning)
        return;

    AudioOutputUnitStop(_unit);
    _isRunning = NO;
    _ring->clear();
}

- (void)enqueueSamples:(const int16_t*)samples count:(NSUInteger)count
{
    if (samples == NULL || count == 0)
        return;
    _ring->write(samples, (size_t)count);
}

- (void)flush
{
    _ring->clear();
}

@end
