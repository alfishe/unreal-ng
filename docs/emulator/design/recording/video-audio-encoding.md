# Video and Audio Encoding Architecture

## Overview

This document describes the video and audio encoding infrastructure for the recording system, including FFmpeg/libav integration, codec selection, container muxing, and multi-track support.

## Encoder Architecture

### Component Hierarchy

```
┌─────────────────────────────────────────────────────────────────┐
│                     RecordingManager                            │
│  - Configuration management                                     │
│  - Recording lifecycle (start/stop/pause/resume)                │
│  - Frame/audio capture coordination                             │
└──────────┬───────────────────────────────────────────┬──────────┘
           │                                           │
    ┌──────▼──────┐                              ┌─────▼──────┐
    │  Video      │                              │  Audio     │
    │  Encoder    │                              │  Encoder   │
    └──────┬──────┘                              └─────┬──────┘
           │                                           │
           │  ┌───────────────────────────────────────┐│
           └──┤       FFmpeg / libav Integration      ├┘
              └───────────────┬───────────────────────┘
                              │
                    ┌─────────▼─────────┐
                    │  Container Muxer  │
                    │  (MP4/MKV/AVI)    │
                    └─────────┬─────────┘
                              │
                         Output File
```

## FFmpeg Integration

### Library Dependencies

```cmake
# CMakeLists.txt
find_package(PkgConfig REQUIRED)

pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVUTIL REQUIRED libavutil)
pkg_check_modules(SWSCALE REQUIRED libswscale)
pkg_check_modules(SWRESAMPLE REQUIRED libswresample)

target_link_libraries(core
    ${AVCODEC_LIBRARIES}
    ${AVFORMAT_LIBRARIES}
    ${AVUTIL_LIBRARIES}
    ${SWSCALE_LIBRARIES}
    ${SWRESAMPLE_LIBRARIES}
)
```

**Required FFmpeg libraries:**
- **libavcodec** - Video/audio codec implementations
- **libavformat** - Container format muxing/demuxing
- **libavutil** - Utility functions (memory, math, etc.)
- **libswscale** - Image scaling and color space conversion
- **libswresample** - Audio resampling and format conversion

### Encoder State Management

```cpp
class RecordingManager
{
private:
    // FFmpeg contexts
    AVFormatContext* _formatContext = nullptr;     // Container muxer
    
    // Video encoder
    AVCodecContext* _videoCodecContext = nullptr;  // Video codec state
    AVStream* _videoStream = nullptr;              // Video stream in container
    AVFrame* _videoFrame = nullptr;                // Video frame buffer
    SwsContext* _swsContext = nullptr;             // Color space converter
    
    // Audio encoder (single-track mode)
    AVCodecContext* _audioCodecContext = nullptr;  // Audio codec state
    AVStream* _audioStream = nullptr;              // Audio stream in container
    AVFrame* _audioFrame = nullptr;                // Audio frame buffer
    SwrContext* _swrContext = nullptr;             // Audio resampler
    
    // Audio encoders (multi-track mode)
    struct AudioTrackEncoder
    {
        AVCodecContext* codecContext = nullptr;
        AVStream* stream = nullptr;
        AVFrame* frame = nullptr;
        SwrContext* resampler = nullptr;
        AudioTrackConfig config;
    };
    std::vector<AudioTrackEncoder> _audioTrackEncoders;
    
    // Packet queue for interleaving
    std::priority_queue<AVPacket*, std::vector<AVPacket*>, PacketComparator> _packetQueue;
};
```

## Video Encoding

### Initialization

```cpp
bool RecordingManager::InitializeVideoEncoder()
{
    // Find video codec
    const AVCodec* codec = avcodec_find_encoder_by_name(_videoCodec.c_str());
    if (!codec)
    {
        MLOGERROR("RecordingManager::InitializeVideoEncoder - Codec '%s' not found", _videoCodec.c_str());
        return false;
    }
    
    // Create video stream
    _videoStream = avformat_new_stream(_formatContext, nullptr);
    if (!_videoStream)
    {
        MLOGERROR("RecordingManager::InitializeVideoEncoder - Failed to create video stream");
        return false;
    }
    
    // Allocate codec context
    _videoCodecContext = avcodec_alloc_context3(codec);
    if (!_videoCodecContext)
    {
        MLOGERROR("RecordingManager::InitializeVideoEncoder - Failed to allocate codec context");
        return false;
    }
    
    // Set codec parameters
    _videoCodecContext->codec_id = codec->id;
    _videoCodecContext->codec_type = AVMEDIA_TYPE_VIDEO;
    _videoCodecContext->width = _videoWidth;
    _videoCodecContext->height = _videoHeight;
    _videoCodecContext->time_base = av_make_q(1, (int)(_videoFrameRate * 1000));  // 1/50000 for 50 Hz
    _videoCodecContext->framerate = av_make_q((int)(_videoFrameRate * 1000), 1000);
    _videoCodecContext->gop_size = 12;  // Keyframe every 12 frames
    _videoCodecContext->pix_fmt = AV_PIX_FMT_YUV420P;  // Most compatible
    
    // Set bitrate if specified
    if (_videoBitrate > 0)
    {
        _videoCodecContext->bit_rate = _videoBitrate * 1000;  // Convert kbps to bps
    }
    
    // Codec-specific options
    AVDictionary* opts = nullptr;
    if (_videoCodec == "h264")
    {
        av_dict_set(&opts, "preset", "medium", 0);     // Encoding speed preset
        av_dict_set(&opts, "crf", "23", 0);            // Constant rate factor (quality)
        av_dict_set(&opts, "profile", "high", 0);      // H.264 profile
    }
    else if (_videoCodec == "h265" || _videoCodec == "hevc")
    {
        av_dict_set(&opts, "preset", "medium", 0);
        av_dict_set(&opts, "crf", "28", 0);
    }
    else if (_videoCodec == "vp9")
    {
        av_dict_set(&opts, "quality", "good", 0);
        av_dict_set(&opts, "speed", "2", 0);
    }
    
    // Open codec
    int ret = avcodec_open2(_videoCodecContext, codec, &opts);
    av_dict_free(&opts);
    if (ret < 0)
    {
        MLOGERROR("RecordingManager::InitializeVideoEncoder - Failed to open codec: %s", av_err2str(ret));
        return false;
    }
    
    // Copy codec parameters to stream
    avcodec_parameters_from_context(_videoStream->codecpar, _videoCodecContext);
    _videoStream->time_base = _videoCodecContext->time_base;
    
    // Allocate video frame
    _videoFrame = av_frame_alloc();
    if (!_videoFrame)
    {
        MLOGERROR("RecordingManager::InitializeVideoEncoder - Failed to allocate frame");
        return false;
    }
    
    _videoFrame->format = _videoCodecContext->pix_fmt;
    _videoFrame->width = _videoCodecContext->width;
    _videoFrame->height = _videoCodecContext->height;
    
    ret = av_frame_get_buffer(_videoFrame, 0);
    if (ret < 0)
    {
        MLOGERROR("RecordingManager::InitializeVideoEncoder - Failed to allocate frame buffer: %s", av_err2str(ret));
        return false;
    }
    
    // Initialize color space converter (RGB32 → YUV420P)
    _swsContext = sws_getContext(
        _videoWidth, _videoHeight, AV_PIX_FMT_BGRA,      // Source: BGRA (ZX Spectrum framebuffer)
        _videoWidth, _videoHeight, AV_PIX_FMT_YUV420P,   // Dest: YUV420P (for encoder)
        SWS_BILINEAR,                                     // Scaling algorithm
        nullptr, nullptr, nullptr
    );
    
    if (!_swsContext)
    {
        MLOGERROR("RecordingManager::InitializeVideoEncoder - Failed to create color space converter");
        return false;
    }
    
    MLOGINFO("RecordingManager::InitializeVideoEncoder - Video encoder initialized successfully");
    return true;
}
```

### Frame Encoding

```cpp
void RecordingManager::EncodeVideoFrame(const FramebufferDescriptor& framebuffer, double timestamp)
{
    // Make frame writable
    int ret = av_frame_make_writable(_videoFrame);
    if (ret < 0)
    {
        MLOGERROR("RecordingManager::EncodeVideoFrame - Failed to make frame writable: %s", av_err2str(ret));
        return;
    }
    
    // Convert RGB32 framebuffer → YUV420P frame
    const uint8_t* srcData[1] = { (const uint8_t*)framebuffer.memoryBuffer };
    int srcLinesize[1] = { (int)(framebuffer.width * 4) };  // 4 bytes per pixel (BGRA)
    
    sws_scale(
        _swsContext,
        srcData,
        srcLinesize,
        0,                          // Start from top
        framebuffer.height,         // Process full height
        _videoFrame->data,          // Destination buffers (Y, U, V)
        _videoFrame->linesize       // Destination line sizes
    );
    
    // Set presentation timestamp (PTS)
    // Convert emulated time (seconds) to stream time base
    int64_t pts = (int64_t)(timestamp / av_q2d(_videoStream->time_base));
    _videoFrame->pts = pts;
    
    // Send frame to encoder
    ret = avcodec_send_frame(_videoCodecContext, _videoFrame);
    if (ret < 0)
    {
        MLOGERROR("RecordingManager::EncodeVideoFrame - Failed to send frame: %s", av_err2str(ret));
        return;
    }
    
    // Receive encoded packets
    while (ret >= 0)
    {
        AVPacket* packet = av_packet_alloc();
        ret = avcodec_receive_packet(_videoCodecContext, packet);
        
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            av_packet_free(&packet);
            break;
        }
        else if (ret < 0)
        {
            MLOGERROR("RecordingManager::EncodeVideoFrame - Failed to receive packet: %s", av_err2str(ret));
            av_packet_free(&packet);
            break;
        }
        
        // Set stream index
        packet->stream_index = _videoStream->index;
        
        // Rescale timestamps to stream time base
        av_packet_rescale_ts(packet, _videoCodecContext->time_base, _videoStream->time_base);
        
        // Add to packet queue for interleaving
        _packetQueue.push(packet);
    }
}
```

## Audio Encoding

### Single-Track Audio Initialization

```cpp
bool RecordingManager::InitializeAudioEncoder()
{
    // Find audio codec
    const AVCodec* codec = avcodec_find_encoder_by_name(_audioCodec.c_str());
    if (!codec)
    {
        MLOGERROR("RecordingManager::InitializeAudioEncoder - Codec '%s' not found", _audioCodec.c_str());
        return false;
    }
    
    // Create audio stream
    _audioStream = avformat_new_stream(_formatContext, nullptr);
    if (!_audioStream)
    {
        MLOGERROR("RecordingManager::InitializeAudioEncoder - Failed to create audio stream");
        return false;
    }
    
    // Allocate codec context
    _audioCodecContext = avcodec_alloc_context3(codec);
    if (!_audioCodecContext)
    {
        MLOGERROR("RecordingManager::InitializeAudioEncoder - Failed to allocate codec context");
        return false;
    }
    
    // Set codec parameters
    _audioCodecContext->codec_id = codec->id;
    _audioCodecContext->codec_type = AVMEDIA_TYPE_AUDIO;
    _audioCodecContext->sample_rate = _audioSampleRate;
    _audioCodecContext->channels = _audioChannels;
    _audioCodecContext->channel_layout = av_get_default_channel_layout(_audioChannels);
    _audioCodecContext->sample_fmt = codec->sample_fmts ? codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
    _audioCodecContext->time_base = av_make_q(1, _audioSampleRate);
    
    // Set bitrate if specified
    if (_audioBitrate > 0)
    {
        _audioCodecContext->bit_rate = _audioBitrate * 1000;  // Convert kbps to bps
    }
    
    // Codec-specific options
    AVDictionary* opts = nullptr;
    if (_audioCodec == "aac")
    {
        av_dict_set(&opts, "profile", "aac_low", 0);
    }
    else if (_audioCodec == "mp3")
    {
        av_dict_set(&opts, "joint_stereo", "1", 0);
    }
    
    // Open codec
    int ret = avcodec_open2(_audioCodecContext, codec, &opts);
    av_dict_free(&opts);
    if (ret < 0)
    {
        MLOGERROR("RecordingManager::InitializeAudioEncoder - Failed to open codec: %s", av_err2str(ret));
        return false;
    }
    
    // Copy codec parameters to stream
    avcodec_parameters_from_context(_audioStream->codecpar, _audioCodecContext);
    _audioStream->time_base = _audioCodecContext->time_base;
    
    // Allocate audio frame
    _audioFrame = av_frame_alloc();
    if (!_audioFrame)
    {
        MLOGERROR("RecordingManager::InitializeAudioEncoder - Failed to allocate frame");
        return false;
    }
    
    _audioFrame->format = _audioCodecContext->sample_fmt;
    _audioFrame->channel_layout = _audioCodecContext->channel_layout;
    _audioFrame->sample_rate = _audioCodecContext->sample_rate;
    _audioFrame->nb_samples = _audioCodecContext->frame_size > 0 ? _audioCodecContext->frame_size : 1024;
    
    ret = av_frame_get_buffer(_audioFrame, 0);
    if (ret < 0)
    {
        MLOGERROR("RecordingManager::InitializeAudioEncoder - Failed to allocate frame buffer: %s", av_err2str(ret));
        return false;
    }
    
    // Initialize audio resampler (int16_t stereo → codec format)
    _swrContext = swr_alloc();
    if (!_swrContext)
    {
        MLOGERROR("RecordingManager::InitializeAudioEncoder - Failed to allocate resampler");
        return false;
    }
    
    av_opt_set_int(_swrContext, "in_channel_layout", AV_CH_LAYOUT_STEREO, 0);
    av_opt_set_int(_swrContext, "in_sample_rate", _audioSampleRate, 0);
    av_opt_set_sample_fmt(_swrContext, "in_sample_fmt", AV_SAMPLE_FMT_S16, 0);
    
    av_opt_set_int(_swrContext, "out_channel_layout", _audioCodecContext->channel_layout, 0);
    av_opt_set_int(_swrContext, "out_sample_rate", _audioCodecContext->sample_rate, 0);
    av_opt_set_sample_fmt(_swrContext, "out_sample_fmt", _audioCodecContext->sample_fmt, 0);
    
    ret = swr_init(_swrContext);
    if (ret < 0)
    {
        MLOGERROR("RecordingManager::InitializeAudioEncoder - Failed to initialize resampler: %s", av_err2str(ret));
        return false;
    }
    
    MLOGINFO("RecordingManager::InitializeAudioEncoder - Audio encoder initialized successfully");
    return true;
}
```

### Multi-Track Audio Initialization

```cpp
bool RecordingManager::InitializeMultiTrackAudioEncoders()
{
    for (size_t i = 0; i < _audioTracks.size(); i++)
    {
        const AudioTrackConfig& trackConfig = _audioTracks[i];
        AudioTrackEncoder encoder;
        encoder.config = trackConfig;
        
        // Find codec for this track
        const AVCodec* codec = avcodec_find_encoder_by_name(trackConfig.codec.c_str());
        if (!codec)
        {
            MLOGERROR("RecordingManager::InitializeMultiTrackAudioEncoders - Codec '%s' not found for track '%s'",
                     trackConfig.codec.c_str(), trackConfig.name.c_str());
            return false;
        }
        
        // Create stream
        encoder.stream = avformat_new_stream(_formatContext, nullptr);
        if (!encoder.stream)
        {
            MLOGERROR("RecordingManager::InitializeMultiTrackAudioEncoders - Failed to create stream for track '%s'",
                     trackConfig.name.c_str());
            return false;
        }
        
        // Set stream metadata
        av_dict_set(&encoder.stream->metadata, "title", trackConfig.name.c_str(), 0);
        
        // Allocate codec context and configure...
        // (similar to single-track initialization)
        
        _audioTrackEncoders.push_back(encoder);
        
        MLOGINFO("RecordingManager::InitializeMultiTrackAudioEncoders - Track %zu '%s' initialized (%s)",
                 i, trackConfig.name.c_str(), trackConfig.codec.c_str());
    }
    
    return true;
}
```

## Container Muxing

### Format Context Initialization

```cpp
bool RecordingManager::InitializeFormatContext()
{
    // Allocate format context
    int ret = avformat_alloc_output_context2(&_formatContext, nullptr, nullptr, _outputFilename.c_str());
    if (ret < 0)
    {
        MLOGERROR("RecordingManager::InitializeFormatContext - Failed to allocate format context: %s", av_err2str(ret));
        return false;
    }
    
    // Open output file
    if (!(_formatContext->oformat->flags & AVFMT_NOFILE))
    {
        ret = avio_open(&_formatContext->pb, _outputFilename.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0)
        {
            MLOGERROR("RecordingManager::InitializeFormatContext - Failed to open output file '%s': %s",
                     _outputFilename.c_str(), av_err2str(ret));
            return false;
        }
    }
    
    MLOGINFO("RecordingManager::InitializeFormatContext - Format context initialized for '%s'", _outputFilename.c_str());
    return true;
}
```

### Writing Container Header

```cpp
bool RecordingManager::WriteContainerHeader()
{
    // Write container header
    AVDictionary* opts = nullptr;
    int ret = avformat_write_header(_formatContext, &opts);
    av_dict_free(&opts);
    
    if (ret < 0)
    {
        MLOGERROR("RecordingManager::WriteContainerHeader - Failed to write header: %s", av_err2str(ret));
        return false;
    }
    
    MLOGINFO("RecordingManager::WriteContainerHeader - Container header written");
    return true;
}
```

### Packet Interleaving

```cpp
void RecordingManager::FlushPacketQueue()
{
    while (!_packetQueue.empty())
    {
        AVPacket* packet = _packetQueue.top();
        _packetQueue.pop();
        
        // Write packet to output file
        int ret = av_interleaved_write_frame(_formatContext, packet);
        if (ret < 0)
        {
            MLOGERROR("RecordingManager::FlushPacketQueue - Failed to write packet: %s", av_err2str(ret));
        }
        
        av_packet_free(&packet);
    }
}
```

### Writing Container Trailer

```cpp
void RecordingManager::WriteContainerTrailer()
{
    // Flush any remaining packets
    FlushPacketQueue();
    
    // Write trailer
    int ret = av_write_trailer(_formatContext);
    if (ret < 0)
    {
        MLOGERROR("RecordingManager::WriteContainerTrailer - Failed to write trailer: %s", av_err2str(ret));
    }
    
    // Close output file
    if (_formatContext && !(_formatContext->oformat->flags & AVFMT_NOFILE))
    {
        avio_closep(&_formatContext->pb);
    }
    
    MLOGINFO("RecordingManager::WriteContainerTrailer - Container finalized");
}
```

## Codec Selection Guide

### Video Codecs

| Codec | Quality | Speed | File Size | Compatibility | Recommendation |
|-------|---------|-------|-----------|---------------|----------------|
| **H.264** | Good | Fast | Small | Excellent | **Default** - Best all-around |
| **H.265/HEVC** | Excellent | Slow | Very Small | Good | High quality, small files |
| **VP9** | Excellent | Very Slow | Very Small | Good | Open source alternative |
| **AV1** | Excellent | Extremely Slow | Smallest | Poor | Future-proof but slow |
| **MJPEG** | Good | Very Fast | Large | Excellent | Real-time capture |
| **FFV1** | Lossless | Fast | Huge | Poor | Archival lossless |

### Animated Image Formats

| Format | Quality | File Size | Compatibility | Transparency | Recommendation |
|--------|---------|-----------|---------------|--------------|----------------|
| **GIF** | Low (256 colors) | Small | Universal | Yes | **Web sharing**, memes |
| **WebP** | Good | Small | Good | Yes | Modern web, better than GIF |
| **AVIF** | Excellent | Very Small | Fair | Yes | High quality web animations |
| **APNG** | Good | Medium | Good | Yes | PNG quality with animation |

### Audio Codecs

| Codec | Quality | File Size | Compatibility | Lossless | Recommendation |
|-------|---------|-----------|---------------|----------|----------------|
| **AAC** | Excellent | Small | Excellent | No | **Default** - Best all-around |
| **MP3** | Good | Small | Excellent | No | Universal compatibility |
| **FLAC** | Perfect | Medium | Good | **Yes** | **Music archival** |
| **Vorbis** | Excellent | Small | Good | No | Open source alternative |
| **Opus** | Excellent | Very Small | Fair | No | Low bitrate/streaming |
| **PCM** | Perfect | Huge | Excellent | **Yes** | Uncompressed |
| **ALAC** | Perfect | Medium | Fair | **Yes** | Apple ecosystem |

## Recommendations by Use Case

### YouTube/Streaming
```cpp
recordingManager->SetVideoCodec("h264");
recordingManager->SetVideoBitrate(5000);        // 5 Mbps
recordingManager->SetAudioCodec("aac");
recordingManager->SetAudioBitrate(192);         // 192 kbps
recordingManager->StartRecording("stream.mp4");
```

### Archival/Maximum Quality
```cpp
recordingManager->SetVideoCodec("h265");
recordingManager->SetVideoBitrate(10000);       // 10 Mbps
recordingManager->SetAudioCodec("flac");
recordingManager->StartRecording("archive.mkv");
```

### Music Recording
```cpp
recordingManager->SetVideoEnabled(false);
recordingManager->SetAudioCodec("flac");
recordingManager->StartRecording("music.flac");
```

### Real-Time Capture
```cpp
recordingManager->SetVideoCodec("mjpeg");
recordingManager->SetAudioCodec("pcm_s16le");
recordingManager->StartRecording("realtime.avi");
```

### Web Sharing (Animated GIF)
```cpp
recordingManager->SetVideoCodec("gif");
recordingManager->SetVideoFrameRate(25.0f);     // Reduce frame rate for smaller size
recordingManager->SetAudioEnabled(false);       // GIF doesn't support audio
recordingManager->StartRecording("gameplay.gif");
```

### Modern Web (Animated WebP)
```cpp
recordingManager->SetVideoCodec("libwebp");
recordingManager->SetVideoBitrate(1000);        // 1 Mbps (much smaller than video)
recordingManager->SetAudioEnabled(false);       // WebP animation doesn't support audio
recordingManager->StartRecording("gameplay.webp");
```

### High-Quality Web Animation (AVIF)
```cpp
recordingManager->SetVideoCodec("libaom-av1");  // AV1 encoder for AVIF
recordingManager->SetVideoBitrate(500);         // Very efficient compression
recordingManager->SetAudioEnabled(false);       // AVIF animation doesn't support audio
recordingManager->StartRecording("gameplay.avif");
```

### PNG-Quality Animation (APNG)
```cpp
recordingManager->SetVideoCodec("apng");
recordingManager->SetAudioEnabled(false);       // APNG doesn't support audio
recordingManager->StartRecording("gameplay.apng");
```

## Animated Image Formats

### Overview

Animated image formats are ideal for:
- **Web sharing** - Embed directly in web pages
- **Social media** - Twitter, Discord, forums
- **Short clips** - 5-30 second gameplay moments
- **No audio needed** - Visual-only content
- **Small file sizes** - Much smaller than video formats

### Format Comparison

#### GIF (Graphics Interchange Format)
**Pros:**
- Universal compatibility (works everywhere)
- Small file sizes for simple animations
- Supports transparency
- Auto-loops by default

**Cons:**
- Limited to 256 colors per frame
- No audio support
- Larger files for complex content
- Dithering required for >256 colors

**Best for:** Memes, simple gameplay moments, maximum compatibility

**FFmpeg codec:** `gif`

```cpp
// Basic GIF recording
recordingManager->SetVideoCodec("gif");
recordingManager->SetVideoFrameRate(15.0f);     // Lower FPS = smaller file
recordingManager->SetAudioEnabled(false);
recordingManager->StartRecording("clip.gif");
```

**Optimization tips:**
- Reduce frame rate (15-25 fps instead of 50 fps)
- Scale down resolution (256x192 → 128x96 for thumbnails)
- Use shorter clips (5-10 seconds max)
- FFmpeg palette optimization: `-lavfi "palettegen,paletteuse"`

#### WebP (Animated)
**Pros:**
- Better quality than GIF (24-bit color)
- Smaller file sizes than GIF
- Supports transparency (alpha channel)
- Lossless and lossy modes
- Good browser support (Chrome, Firefox, Edge)

**Cons:**
- No audio support
- Limited Safari support (iOS 14+, macOS 11+)
- Less universal than GIF

**Best for:** Modern web, better quality than GIF, smaller files

**FFmpeg codec:** `libwebp`

```cpp
// High-quality WebP animation
recordingManager->SetVideoCodec("libwebp");
recordingManager->SetVideoBitrate(1000);        // 1 Mbps
recordingManager->SetAudioEnabled(false);
recordingManager->StartRecording("clip.webp");
```

**Optimization tips:**
- Use `-quality 75` for good balance (0-100 scale)
- `-lossless 0` for lossy compression (smaller files)
- `-lossless 1` for lossless (larger but perfect quality)
- `-compression_level 6` for better compression (0-6, slower is better)

#### AVIF (Animated)
**Pros:**
- Excellent quality at low bitrates
- Much smaller than GIF/WebP
- Supports transparency
- Based on AV1 codec (modern)
- HDR support

**Cons:**
- Slow encoding (AV1 is computationally expensive)
- Limited browser support (Chrome 85+, Firefox 93+)
- No audio support in animation mode

**Best for:** High-quality web animations, smallest file sizes

**FFmpeg codec:** `libaom-av1` (for AVIF sequence)

```cpp
// Ultra-compressed AVIF animation
recordingManager->SetVideoCodec("libaom-av1");
recordingManager->SetVideoBitrate(300);         // Very efficient
recordingManager->SetAudioEnabled(false);
recordingManager->StartRecording("clip.avif");
```

**Optimization tips:**
- Use `-crf 30` for good quality (0-63, lower is better)
- `-cpu-used 4` for faster encoding (0-8, higher is faster)
- `-row-mt 1` for multi-threading
- Trade encoding time for smaller files

#### APNG (Animated PNG)
**Pros:**
- Full PNG quality (24-bit color + alpha)
- Lossless compression
- Good browser support
- Supports transparency

**Cons:**
- Larger file sizes than WebP/AVIF
- No audio support
- Not as widely supported as GIF

**Best for:** High-quality animations with transparency, PNG-level quality

**FFmpeg codec:** `apng`

```cpp
// Lossless APNG animation
recordingManager->SetVideoCodec("apng");
recordingManager->SetAudioEnabled(false);
recordingManager->StartRecording("clip.apng");
```

**Optimization tips:**
- Use `-plays 0` for infinite loop
- PNG compression is automatic
- Consider scaling down resolution to reduce file size
- Best for short clips (file size grows quickly)

### File Size Comparison

For a **10-second** ZX Spectrum recording (256×192, 25 fps):

| Format | Typical Size | Quality | Encoding Speed |
|--------|-------------|---------|----------------|
| **GIF** | 500 KB - 2 MB | Low (256 colors) | Fast |
| **WebP** | 200 KB - 800 KB | Good (24-bit) | Fast |
| **AVIF** | 100 KB - 400 KB | Excellent | Very Slow |
| **APNG** | 1 MB - 5 MB | Perfect (lossless) | Fast |
| **H.264** | 300 KB - 1 MB | Excellent | Fast |

**Note:** Actual sizes depend on content complexity, motion, and settings.

### When to Use Each Format

```
Use GIF when:
  ✓ Maximum compatibility needed (forums, old browsers)
  ✓ Simple content (menus, loading screens)
  ✓ Short clips (< 10 seconds)
  ✗ Avoid for: Complex graphics, long clips, high quality

Use WebP when:
  ✓ Modern browsers (Chrome, Firefox, Edge)
  ✓ Better quality than GIF needed
  ✓ Smaller file sizes than GIF
  ✗ Avoid for: Safari-only users, older browsers

Use AVIF when:
  ✓ Smallest possible file size needed
  ✓ High quality required
  ✓ Modern browsers (Chrome 85+, Firefox 93+)
  ✗ Avoid for: Fast encoding, older browsers

Use APNG when:
  ✓ Lossless quality required
  ✓ Transparency needed
  ✓ PNG-level quality
  ✗ Avoid for: Long clips, file size constraints

Use video (H.264) when:
  ✓ Audio needed
  ✓ Long recordings (> 30 seconds)
  ✓ Best quality/size ratio
  ✓ Streaming/YouTube
```

### Hybrid Approach: Video + Animated Thumbnail

Record both formats for best of both worlds:

```cpp
// Record full video with audio
recordingManager->SetVideoCodec("h264");
recordingManager->SetAudioCodec("aac");
recordingManager->StartRecording("full_gameplay.mp4");

// ... play game for 5 minutes ...

recordingManager->StopRecording();

// Now record a short GIF preview (first 10 seconds)
emulator->Reset();
emulator->LoadSnapshot("game_start.sna");

recordingManager->SetVideoCodec("webp");
recordingManager->SetVideoFrameRate(15.0f);     // Lower FPS for smaller size
recordingManager->SetAudioEnabled(false);
recordingManager->StartRecording("preview.webp");

// ... run for 10 seconds ...

recordingManager->StopRecording();
```

**Result:**
- `full_gameplay.mp4` - Full video with audio (5 minutes)
- `preview.webp` - Animated thumbnail for social media (10 seconds)

## Performance Optimization

### Encoding Speed

**Strategies:**
1. **Use hardware acceleration** (GPU encoding)
   - H.264 (NVENC, QSV, VideoToolbox)
   - H.265 (NVENC, QSV)
2. **Adjust codec presets**
   - `ultrafast` - Very fast, larger files
   - `medium` - Balanced (default)
   - `slow` - Slower, smaller files
3. **Use separate encoding thread**
   - Don't block emulation
   - Queue frames in ring buffer
4. **Batch packet writes**
   - Write every N packets instead of every packet

### Memory Management

**Buffer pooling:**
```cpp
class FramePool
{
public:
    AVFrame* Acquire();
    void Release(AVFrame* frame);
    
private:
    std::queue<AVFrame*> _pool;
    std::mutex _mutex;
};
```

**Benefits:**
- Avoid repeated allocations
- Reduce memory fragmentation
- Faster frame preparation

## Error Handling

### Common Errors

1. **Codec not found**
   - User's FFmpeg build doesn't include requested codec
   - Solution: Fall back to default codec (H.264/AAC)

2. **Insufficient disk space**
   - Writing fails mid-recording
   - Solution: Check available space before starting

3. **Encoder can't keep up**
   - Frames queued faster than encoded
   - Solution: Drop frames with warning, or reduce quality

4. **Format incompatibility**
   - Codec not supported by container
   - Solution: Auto-select compatible container

### Robust Initialization

```cpp
bool RecordingManager::StartRecording(/* ... */)
{
    // Validate configuration
    if (!ValidateConfiguration())
        return false;
    
    // Check disk space
    if (!CheckDiskSpace())
        return false;
    
    // Initialize with fallback
    if (!TryInitializeEncoder(_videoCodec, _audioCodec))
    {
        MLOGWARNING("Falling back to default codecs");
        if (!TryInitializeEncoder("h264", "aac"))
        {
            MLOGERROR("Failed to initialize encoders");
            return false;
        }
    }
    
    // ...
}
```

## References

- [Recording System Architecture](recording-system.md)
- [Audio Routing](audio-routing.md)
- FFmpeg documentation: https://ffmpeg.org/documentation.html
- libavcodec documentation: https://ffmpeg.org/doxygen/trunk/group__libavc.html
- libavformat documentation: https://ffmpeg.org/doxygen/trunk/group__libavf.html

