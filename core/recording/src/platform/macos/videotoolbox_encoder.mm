#ifdef __APPLE__

#include "videotoolbox_encoder.h"
#include "emulator/video/screen.h"

// macOS framework imports (Objective-C++)
#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <AudioToolbox/AudioToolbox.h>

#include <sys/stat.h>
#include <cstring>

// ============================================================================
// Helper: convert string to NSString
// ============================================================================

static NSString* toNSString(const std::string& s)
{
    return [NSString stringWithUTF8String:s.c_str()];
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

VideoToolboxEncoder::VideoToolboxEncoder()
{
}

VideoToolboxEncoder::~VideoToolboxEncoder()
{
    Stop();
}

// ============================================================================
// Start
// ============================================================================

bool VideoToolboxEncoder::Start(const std::string& filename, const EncoderConfig& config)
{
    if (_isRecording)
    {
        _lastError = "Already recording";
        return false;
    }

    _filename = filename;
    _srcWidth = config.videoWidth;
    _srcHeight = config.videoHeight;
    _scale = config.scaleFactor < 1 ? 1 : (config.scaleFactor > 4 ? 4 : config.scaleFactor);
    _width = _srcWidth * _scale;
    _height = _srcHeight * _scale;
    _fps = config.frameRate;
    _hasAudio = (config.audioChannels > 0);

    // Determine codec
    std::string codec = config.videoCodec;
    std::transform(codec.begin(), codec.end(), codec.begin(), ::tolower);
    _useHEVC = (codec == "h265" || codec == "h.265" || codec == "hevc");

    // Validate dimensions
    if (_width == 0 || _height == 0)
    {
        _lastError = "Invalid video dimensions";
        return false;
    }

    // Initialize components
    if (!initAssetWriter(filename, config))
    {
        Stop();
        return false;
    }

    if (_hasAudio && !initAudioConverter(config))
    {
        Stop();
        return false;
    }

    _isRecording = true;
    _framesEncoded = 0;
    _audioSamplesEncoded = 0;

    return true;
}

// ============================================================================
// initAssetWriter
// ============================================================================

bool VideoToolboxEncoder::initAssetWriter(const std::string& filename, const EncoderConfig& config)
{
    @autoreleasepool
    {
        // Ensure the output directory exists
        size_t lastSlash = filename.rfind('/');
        if (lastSlash != std::string::npos && lastSlash > 0)
        {
            std::string dir = filename.substr(0, lastSlash);
            mkdir(dir.c_str(), 0755); // Create if needed, ignore if exists
        }

        // Pre-flight: verify the path is writable by creating and removing a test file
        {
            FILE* test = fopen(filename.c_str(), "wb");
            if (!test)
            {
                _lastError = "Cannot write to output path '" + filename + "' (errno: "
                    + std::string(strerror(errno)) + ")";
                return false;
            }
            fclose(test);
            unlink(filename.c_str());
        }

        NSURL* outputURL = [NSURL fileURLWithPath:toNSString(filename)];

        // Remove existing file (AVAssetWriter refuses to overwrite)
        [[NSFileManager defaultManager] removeItemAtURL:outputURL error:nil];

        // Determine file type from extension
        AVFileType fileType = AVFileTypeMPEG4;
        std::string ext;
        size_t dotPos = filename.rfind('.');
        if (dotPos != std::string::npos)
        {
            ext = filename.substr(dotPos + 1);
        }

        if (ext == "mov")
            fileType = AVFileTypeQuickTimeMovie;
        else if (ext == "m4v")
            fileType = AVFileTypeAppleM4V;
        else
            fileType = AVFileTypeMPEG4;

        // Check HEVC availability
        if (_useHEVC)
        {
            if (@available(macOS 10.13, *))
            {
                // HEVC is available
            }
            else
            {
                _lastError = "HEVC encoding requires macOS 10.13 or later";
                return false;
            }
        }

        NSError* error = nil;
        AVAssetWriter* writer = [[AVAssetWriter alloc] initWithURL:outputURL
                                                           fileType:fileType
                                                              error:&error];

        if (!writer)
        {
            _lastError = "AVAssetWriter init returned nil";
            if (error)
                _lastError += ": " + std::string([[error localizedDescription] UTF8String])
                    + " [code " + std::to_string(error.code) + "]";
            return false;
        }

        // Video input with outputSettings: AVAssetWriter owns compression and
        // uses VideoToolbox hardware internally. We feed it raw BGRA pixel
        // buffers through the pixel buffer adaptor.
        uint32_t bitrate = config.videoBitrate > 0 ? config.videoBitrate * 1000 : 0;
        if (bitrate == 0)
        {
            // Derive from quality preset (0-10) scaled to frame area,
            // referenced to ~5 Mbps for a 352x288@50 ZX frame at preset 5
            double area = static_cast<double>(_width) * _height;
            double scale = area / (352.0 * 288.0);
            double presetFactor = 0.4 + 0.12 * config.qualityPreset;  // 0.4x .. 1.6x
            bitrate = static_cast<uint32_t>(5000000.0 * scale * presetFactor);
        }

        NSDictionary* compressionProps = @{
            AVVideoAverageBitRateKey: @(bitrate),
            AVVideoMaxKeyFrameIntervalKey: @(static_cast<int>(_fps * 2)),
            AVVideoAllowFrameReorderingKey: @NO  // No B-frames: low latency, simple PTS
        };

        // Explicit BT.709 tagging — untagged output leaves players guessing
        // the color matrix, which shows up as pale/shifted colors
        NSDictionary* colorProps = @{
            AVVideoColorPrimariesKey: AVVideoColorPrimaries_ITU_R_709_2,
            AVVideoTransferFunctionKey: AVVideoTransferFunction_ITU_R_709_2,
            AVVideoYCbCrMatrixKey: AVVideoYCbCrMatrix_ITU_R_709_2
        };

        NSDictionary* videoSettings = @{
            AVVideoCodecKey: _useHEVC ? AVVideoCodecTypeHEVC : AVVideoCodecTypeH264,
            AVVideoWidthKey: @(_width),
            AVVideoHeightKey: @(_height),
            AVVideoCompressionPropertiesKey: compressionProps,
            AVVideoColorPropertiesKey: colorProps
        };

        AVAssetWriterInput* videoInput = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo
                                                                              outputSettings:videoSettings];
        if (!videoInput)
        {
            _lastError = std::string("Video codec ") + (_useHEVC ? "HEVC" : "H.264")
                + " is not supported by AVAssetWriter for this configuration";
            [writer release];
            return false;
        }

        videoInput.expectsMediaDataInRealTime = YES;

        if (![writer canAddInput:videoInput])
        {
            _lastError = "Cannot add video input to AVAssetWriter (codec "
                + std::string(_useHEVC ? "HEVC" : "H.264") + " may not be supported for "
                + ext + " container)";
            [writer release];
            return false;
        }
        [writer addInput:videoInput];

        // Pixel buffer adaptor provides a CVPixelBuffer pool matching the input
        NSDictionary* pixelBufferAttributes = @{
            (NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
            (NSString*)kCVPixelBufferWidthKey: @(_width),
            (NSString*)kCVPixelBufferHeightKey: @(_height)
        };

        AVAssetWriterInputPixelBufferAdaptor* adaptor =
            [AVAssetWriterInputPixelBufferAdaptor
                assetWriterInputPixelBufferAdaptorWithAssetWriterInput:videoInput
                                                sourcePixelBufferAttributes:pixelBufferAttributes];

        // Audio input
        if (_hasAudio)
        {
            uint32_t sampleRate = config.audioSampleRate > 0 ? config.audioSampleRate : 44100;
            uint32_t channels = config.audioChannels > 0 ? config.audioChannels : 2;
            uint32_t audioBitrate = config.audioBitrate > 0 ? config.audioBitrate * 1000 : 192000;

            AudioChannelLayout channelLayout = {};
            channelLayout.mChannelLayoutTag = (channels == 2) ? kAudioChannelLayoutTag_Stereo : kAudioChannelLayoutTag_Mono;

            NSData* channelLayoutData = [NSData dataWithBytes:&channelLayout length:sizeof(AudioChannelLayout)];

            NSDictionary* audioSettings = @{
                AVFormatIDKey: @(kAudioFormatMPEG4AAC),
                AVSampleRateKey: @(sampleRate),
                AVNumberOfChannelsKey: @(channels),
                AVEncoderBitRateKey: @(audioBitrate),
                AVChannelLayoutKey: channelLayoutData
            };

            AVAssetWriterInput* audioInput = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeAudio
                                                                                  outputSettings:audioSettings];
            audioInput.expectsMediaDataInRealTime = YES;

            if ([writer canAddInput:audioInput])
            {
                [writer addInput:audioInput];
                _audioInput = (__bridge void*)[audioInput retain];
            }
            else
            {
                NSLog(@"[VT] Warning: cannot add audio input, continuing video-only");
            }
        }

        _videoInput = (__bridge void*)[videoInput retain];
        _pixelBufferAdaptor = (__bridge void*)[adaptor retain];
        _assetWriter = (__bridge void*)writer;  // alloc/init already owns +1

        if (![writer startWriting])
        {
            NSError* writerError = [writer error];
            _lastError = "AVAssetWriter startWriting failed";
            if (writerError)
            {
                _lastError += ": " + std::string([[writerError localizedDescription] UTF8String])
                    + " [code " + std::to_string(writerError.code) + "]";
            }
            else
            {
                _lastError += " (no NSError). Writer status: " + std::to_string((int)writer.status);
            }
            return false;
        }

        [writer startSessionAtSourceTime:kCMTimeZero];
        return true;
    }
}

// ============================================================================
// initAudioConverter
// ============================================================================

bool VideoToolboxEncoder::initAudioConverter(const EncoderConfig& config)
{
    // AVAssetWriter handles AAC compression internally when given PCM input.
    // We only need a CMAudioFormatDescription describing the incoming PCM stream.
    _audioChannels = config.audioChannels > 0 ? config.audioChannels : 2;
    _audioSampleRate = config.audioSampleRate > 0 ? config.audioSampleRate : 44100;

    AudioStreamBasicDescription asbd = {};
    asbd.mSampleRate = _audioSampleRate;
    asbd.mFormatID = kAudioFormatLinearPCM;
    asbd.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    asbd.mBytesPerPacket = _audioChannels * sizeof(int16_t);
    asbd.mFramesPerPacket = 1;
    asbd.mBytesPerFrame = _audioChannels * sizeof(int16_t);
    asbd.mChannelsPerFrame = _audioChannels;
    asbd.mBitsPerChannel = 16;

    CMAudioFormatDescriptionRef formatDesc = nullptr;
    OSStatus status = CMAudioFormatDescriptionCreate(kCFAllocatorDefault, &asbd, 0, nullptr, 0, nullptr, nullptr, &formatDesc);
    if (status != noErr || formatDesc == nullptr)
    {
        _lastError = "CMAudioFormatDescriptionCreate failed: " + std::to_string(status);
        return false;
    }

    _audioFormatDesc = (void*)formatDesc;
    return true;
}

// ============================================================================
// OnVideoFrame
// ============================================================================

void VideoToolboxEncoder::OnVideoFrame(const FramebufferDescriptor& framebuffer, double timestampSec)
{
    (void)timestampSec;
    if (!_isRecording || !_videoInput || !_pixelBufferAdaptor)
        return;

    @autoreleasepool
    {
        uint32_t expectedWidth = _srcWidth > 0 ? _srcWidth : framebuffer.width;
        uint32_t expectedHeight = _srcHeight > 0 ? _srcHeight : framebuffer.height;
        size_t expectedSize = static_cast<size_t>(expectedWidth) * expectedHeight * 4;

        if (framebuffer.memoryBuffer == nullptr || framebuffer.memoryBufferSize < expectedSize)
            return;

        AVAssetWriterInput* videoInput = (__bridge AVAssetWriterInput*)_videoInput;
        AVAssetWriterInputPixelBufferAdaptor* adaptor =
            (__bridge AVAssetWriterInputPixelBufferAdaptor*)_pixelBufferAdaptor;

        // Wait briefly for the input to become ready. VideoToolbox HW keeps up
        // with ZX-sized frames easily, so a bounded wait protects turbo-mode
        // capture from drops without risking a long stall.
        int waitedUs = 0;
        while (![videoInput isReadyForMoreMediaData] && waitedUs < 100000)  // max 100ms
        {
            usleep(1000);
            waitedUs += 1000;
        }
        if (![videoInput isReadyForMoreMediaData])
            return;  // Drop frame — writer is stuck

        // Get a pixel buffer from the adaptor's pool and copy the frame in
        CVPixelBufferRef pixelBuffer = nullptr;
        CVPixelBufferPoolRef pool = adaptor.pixelBufferPool;
        OSStatus status;
        if (pool)
        {
            status = CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, pool, &pixelBuffer);
        }
        else
        {
            status = CVPixelBufferCreate(kCFAllocatorDefault, expectedWidth, expectedHeight,
                                         kCVPixelFormatType_32BGRA, nullptr, &pixelBuffer);
        }

        if (status != kCVReturnSuccess || pixelBuffer == nullptr)
        {
            _lastError = "Failed to obtain CVPixelBuffer: " + std::to_string(status);
            return;
        }

        CVPixelBufferLockBaseAddress(pixelBuffer, 0);
        uint8_t* dst = static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(pixelBuffer));
        size_t dstStride = CVPixelBufferGetBytesPerRow(pixelBuffer);
        const uint8_t* src = static_cast<const uint8_t*>(framebuffer.memoryBuffer);
        size_t srcStride = static_cast<size_t>(expectedWidth) * 4;

        // The emulator framebuffer is RGBA; the pixel buffer is 32BGRA (the
        // only raw format VideoToolbox accepts everywhere) — swizzle R<->B
        // while copying, replicating pixels for the integer upscale.
        // Build each output row once, then memcpy it for the repeated rows.
        for (uint32_t sy = 0; sy < expectedHeight; sy++)
        {
            const uint8_t* s = src + sy * srcStride;
            uint8_t* firstRow = dst + static_cast<size_t>(sy) * _scale * dstStride;

            uint8_t* d = firstRow;
            for (uint32_t sx = 0; sx < expectedWidth; sx++)
            {
                for (uint32_t r = 0; r < _scale; r++)
                {
                    d[0] = s[2];  // B
                    d[1] = s[1];  // G
                    d[2] = s[0];  // R
                    d[3] = s[3];  // A
                    d += 4;
                }
                s += 4;
            }

            for (uint32_t r = 1; r < _scale; r++)
                memcpy(firstRow + r * dstStride, firstRow, static_cast<size_t>(_width) * 4);
        }
        CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);

        // PTS based on frame count (perfect 50Hz guarantee)
        CMTime pts = CMTimeMake(static_cast<int64_t>(_framesEncoded), static_cast<int32_t>(_fps));

        if (![adaptor appendPixelBuffer:pixelBuffer withPresentationTime:pts])
        {
            AVAssetWriter* writer = (__bridge AVAssetWriter*)_assetWriter;
            NSString* errDesc = writer.error ? [writer.error localizedDescription] : @"unknown";
            _lastError = std::string("appendPixelBuffer failed: ") + [errDesc UTF8String];
        }
        else
        {
            _framesEncoded++;
        }

        CVPixelBufferRelease(pixelBuffer);
    }
}

// ============================================================================
// OnAudioSamples
// ============================================================================

void VideoToolboxEncoder::OnAudioSamples(const int16_t* samples, size_t sampleCount, double timestampSec)
{
    (void)timestampSec;
    if (!_isRecording || !_audioInput || !_hasAudio || samples == nullptr || sampleCount == 0)
        return;

    @autoreleasepool
    {
        AVAssetWriterInput* audioInput = (__bridge AVAssetWriterInput*)_audioInput;

        // Drop audio if input isn't ready — never block the calling thread
        // (Blocking here causes CoreAudio HAL overload and main-thread warnings)
        if (![audioInput isReadyForMoreMediaData])
            return;

        // Format description created once in initAudioConverter from config
        CMAudioFormatDescriptionRef formatDesc = (CMAudioFormatDescriptionRef)_audioFormatDesc;
        if (formatDesc == nullptr)
            return;

        const uint32_t channels = _audioChannels;
        const uint32_t sampleRate = _audioSampleRate;

        size_t dataSize = sampleCount * sizeof(int16_t);
        CMBlockBufferRef blockBuffer = nullptr;

        CMBlockBufferCreateWithMemoryBlock(
            kCFAllocatorDefault,
            nullptr,
            dataSize,
            kCFAllocatorDefault,
            nullptr,
            0,
            dataSize,
            0,
            &blockBuffer);

        if (blockBuffer == nullptr)
            return;

        CMBlockBufferReplaceDataBytes(samples, blockBuffer, 0, dataSize);

        size_t numFrames = sampleCount / channels;
        CMSampleBufferRef sampleBuffer = nullptr;

        CMSampleTimingInfo timingInfo = {};
        timingInfo.presentationTimeStamp = CMTimeMake(static_cast<int64_t>(_audioSamplesEncoded / channels), sampleRate);

        size_t sampleSize = dataSize;
        CMSampleBufferCreate(
            kCFAllocatorDefault,
            blockBuffer,
            true,
            nullptr,
            nullptr,
            formatDesc,
            numFrames,
            1,
            &timingInfo,
            1,
            &sampleSize,
            &sampleBuffer);

        if (sampleBuffer)
        {
            [audioInput appendSampleBuffer:sampleBuffer];
            CFRelease(sampleBuffer);
        }

        if (blockBuffer)
            CFRelease(blockBuffer);

        _audioSamplesEncoded += sampleCount;
    }
}

// ============================================================================
// Stop
// ============================================================================

void VideoToolboxEncoder::Stop()
{
    // Run cleanup both after a recording session and after a partially-failed
    // Start() (resources allocated but _isRecording never set)
    if (!_isRecording && !_assetWriter && !_audioFormatDesc)
        return;

    _isRecording = false;

    @autoreleasepool
    {
        AVAssetWriter* writer = _assetWriter ? (__bridge AVAssetWriter*)_assetWriter : nil;
        bool writerActive = writer && writer.status == AVAssetWriterStatusWriting;

        // 1. Mark inputs as finished — tells AVAssetWriter no more data is coming.
        //    Only legal while the writer is actually writing (guards the
        //    partially-failed-Start cleanup path).
        if (_videoInput)
        {
            AVAssetWriterInput* videoInput = (__bridge AVAssetWriterInput*)_videoInput;
            if (writerActive)
                [videoInput markAsFinished];
            [videoInput release];
            _videoInput = nullptr;
        }

        if (_audioInput)
        {
            AVAssetWriterInput* audioInput = (__bridge AVAssetWriterInput*)_audioInput;
            if (writerActive)
                [audioInput markAsFinished];
            [audioInput release];
            _audioInput = nullptr;
        }

        if (_pixelBufferAdaptor)
        {
            [(__bridge id)_pixelBufferAdaptor release];
            _pixelBufferAdaptor = nullptr;
        }

        // 2. Finalize the asset writer — this writes the MP4/MOV header + data
        //    (flushing the internal VideoToolbox compression pipeline).
        if (writer)
        {
            if (writerActive)
            {
                dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
                [writer finishWritingWithCompletionHandler:^{
                    dispatch_semaphore_signal(semaphore);
                }];

                // Wait up to 30 seconds for finalization (large files can take time)
                dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC));

                if (writer.status != AVAssetWriterStatusCompleted)
                {
                    NSString* errDesc = writer.error ? [writer.error localizedDescription] : @"unknown";
                    _lastError = std::string("AVAssetWriter failed: ") + [errDesc UTF8String];
                }
            }

            [writer release];
            _assetWriter = nullptr;
        }

        // 3. Clean up remaining resources
        if (_audioFormatDesc)
        {
            CFRelease((CMAudioFormatDescriptionRef)_audioFormatDesc);
            _audioFormatDesc = nullptr;
        }
    }
}

// ============================================================================
// GetOutputFileSize
// ============================================================================

uint64_t VideoToolboxEncoder::GetOutputFileSize() const
{
    // AVAssetWriter flushes to disk incrementally; final size lands after Stop()
    if (!_filename.empty())
    {
        struct stat st;
        if (stat(_filename.c_str(), &st) == 0 && st.st_size > 0)
            return static_cast<uint64_t>(st.st_size);
    }

    return 0;
}

#endif // __APPLE__
