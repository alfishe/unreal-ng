#import "UNEmulatorBridge.h"
#import "UNAudioOutput.h"

// ---------------------------------------------------------------------------
// C++ core - confined to this translation unit
// ---------------------------------------------------------------------------
#include "3rdparty/message-center/messagecenter.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/io/tape/tape.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/video/screen.h"

#include <memory>
#include <mutex>
#include <set>
#include <string>

// ---------------------------------------------------------------------------
// Keep UNZXKeys.h honest: the Swift-visible enum must mirror core's ZXKeysEnum.
// ---------------------------------------------------------------------------
static_assert((uint8_t)UNZXKeyNone == (uint8_t)ZXKEY_NONE, "ZX key enum drift");
static_assert((uint8_t)UNZXKeyCapsShift == (uint8_t)ZXKEY_CAPS_SHIFT, "ZX key enum drift");
static_assert((uint8_t)UNZXKeySymShift == (uint8_t)ZXKEY_SYM_SHIFT, "ZX key enum drift");
static_assert((uint8_t)UNZXKeyEnter == (uint8_t)ZXKEY_ENTER, "ZX key enum drift");
static_assert((uint8_t)UNZXKeySpace == (uint8_t)ZXKEY_SPACE, "ZX key enum drift");
static_assert((uint8_t)UNZXKeyDigit0 == (uint8_t)ZXKEY_0, "ZX key enum drift");
static_assert((uint8_t)UNZXKeyDigit9 == (uint8_t)ZXKEY_9, "ZX key enum drift");
static_assert((uint8_t)UNZXKeyLetterA == (uint8_t)ZXKEY_A, "ZX key enum drift");
static_assert((uint8_t)UNZXKeyLetterH == (uint8_t)ZXKEY_H, "ZX key enum drift");
static_assert((uint8_t)UNZXKeyLetterI == (uint8_t)ZXKEY_I, "ZX key enum drift");
static_assert((uint8_t)UNZXKeyLetterZ == (uint8_t)ZXKEY_Z, "ZX key enum drift");
static_assert((uint8_t)UNZXKeyExtCtrl == (uint8_t)ZXKEY_EXT_CTRL, "ZX key enum drift");
static_assert((uint8_t)UNZXKeyExtUp == (uint8_t)ZXKEY_EXT_UP, "ZX key enum drift");
static_assert((uint8_t)UNZXKeyExtDown == (uint8_t)ZXKEY_EXT_DOWN, "ZX key enum drift");
static_assert((uint8_t)UNZXKeyExtLeft == (uint8_t)ZXKEY_EXT_LEFT, "ZX key enum drift");
static_assert((uint8_t)UNZXKeyExtRight == (uint8_t)ZXKEY_EXT_RIGHT, "ZX key enum drift");
static_assert((uint8_t)UNZXKeyExtDelete == (uint8_t)ZXKEY_EXT_DELETE, "ZX key enum drift");
static_assert((uint8_t)UNZXKeyExtBreak == (uint8_t)ZXKEY_EXT_BREAK, "ZX key enum drift");
static_assert((uint8_t)UNZXKeyExtEdit == (uint8_t)ZXKEY_EXT_EDIT, "ZX key enum drift");
static_assert((uint8_t)UNZXKeyExtDot == (uint8_t)ZXKEY_EXT_DOT, "ZX key enum drift");
static_assert((uint8_t)UNZXKeyExtComma == (uint8_t)ZXKEY_EXT_COMMA, "ZX key enum drift");
static_assert((uint8_t)UNZXKeyExtPlus == (uint8_t)ZXKEY_EXT_PLUS, "ZX key enum drift");
static_assert((uint8_t)UNZXKeyExtMinus == (uint8_t)ZXKEY_EXT_MINUS, "ZX key enum drift");
static_assert((uint8_t)UNZXKeyExtMul == (uint8_t)ZXKEY_EXT_MULTIPLY, "ZX key enum drift");
static_assert((uint8_t)UNZXKeyExtDiv == (uint8_t)ZXKEY_EXT_DIVIDE, "ZX key enum drift");
static_assert((uint8_t)UNZXKeyExtEqual == (uint8_t)ZXKEY_EXT_EQUAL, "ZX key enum drift");
static_assert((uint8_t)UNZXKeyExtBar == (uint8_t)ZXKEY_EXT_BAR, "ZX key enum drift");
static_assert((uint8_t)UNZXKeyExtBkSlash == (uint8_t)ZXKEY_EXT_BACKSLASH, "ZX key enum drift");
static_assert((uint8_t)UNZXKeyExtCapsLock == (uint8_t)ZXKEY_EXT_CAPSLOCK, "ZX key enum drift");
static_assert((uint8_t)UNZXKeyExtDblQuote == (uint8_t)ZXKEY_EXT_DBLQUOTE, "ZX key enum drift");

@class UNEmulatorBridge;

/// Private hooks the C++ observer calls back into. Everything here is implemented
/// in the matching category below.
@interface UNEmulatorBridge (CoreCallbacks)
- (void)coreDidRenderFrame;
- (void)coreDidChangeResolution;
- (void)coreDidChangeState;
- (void)coreWantsMoreAudio;
@end

/// Internal to the main @implementation. It used to be declared in the category
/// above while being implemented in the main block, which is what the compiler
/// meant by "method definition for 'refreshFramebufferDescriptor' not found".
@interface UNEmulatorBridge ()
- (void)refreshFramebufferDescriptor;
@end

namespace
{
    /// MessageCenter observers are member-function pointers on an Observer subclass,
    /// so the bridge needs a small C++ shim that forwards into Objective-C.
    class CoreObserver : public Observer
    {
    public:
        __unsafe_unretained UNEmulatorBridge* owner = nil;
        std::string uuid;   // filter: notifications are global, emulators are not

        bool matches(const unreal::UUID& emulatorId) const
        {
            return uuid.empty() || static_cast<std::string>(emulatorId) == uuid;
        }

        void onVideoFrame(int id, Message* message)
        {
            (void)id;
            if (!message || !message->obj)
                return;

            EmulatorFramePayload* payload = dynamic_cast<EmulatorFramePayload*>(message->obj);
            if (!payload || !matches(payload->_emulatorId))
                return;

            [owner coreDidRenderFrame];
        }

        void onResolutionChanged(int id, Message* message)
        {
            (void)id;
            if (!message || !message->obj)
                return;

            VideoResolutionPayload* payload = dynamic_cast<VideoResolutionPayload*>(message->obj);
            if (!payload || !matches(payload->_emulatorId))
                return;

            [owner coreDidChangeResolution];
        }

        void onStateChanged(int id, Message* message)
        {
            (void)id;
            (void)message;
            [owner coreDidChangeState];
        }
    };

    /// Core audio pump: called on the emulator worker thread.
    void CoreAudioSink(void* obj, int16_t* samples, size_t numSamples)
    {
        UNAudioOutput* output = (__bridge UNAudioOutput*)obj;
        [output enqueueSamples:samples count:numSamples];
    }
}

@implementation UNEmulatorBridge
{
    EmulatorManager* _manager;
    std::shared_ptr<Emulator> _emulator;
    std::string _emulatorId;    // manager-facing id
    std::string _uuid;          // used for message filtering / key routing

    CoreObserver _observer;
    BOOL _subscribed;

    std::mutex _framebufferMutex;
    uint8_t* _framebuffer;
    size_t _framebufferSize;
    int _width;
    int _height;

    std::set<uint8_t> _pressedKeys;
    std::mutex _keysMutex;

    UNAudioOutput* _audio;
}

- (instancetype)init
{
    self = [super init];
    if (self)
    {
        _manager = EmulatorManager::GetInstance();
        _observer.owner = self;
        _width = 352;
        _height = 288;
        _audio = [[UNAudioOutput alloc] init];

        __weak UNEmulatorBridge* weakSelf = self;
        _audio.needsMoreDataHandler = ^{
            [weakSelf coreWantsMoreAudio];
        };
    }
    return self;
}

- (void)dealloc
{
    [self stop];
    _observer.owner = nil;
}

+ (NSArray<NSString*>*)supportedFileExtensions
{
    return @[ @"sna", @"z80", @"tap", @"tzx", @"trd", @"scl", @"fdi", @"td0", @"udi" ];
}

#pragma mark - Lifecycle

- (BOOL)start
{
    if (_emulator)
        return YES;

    _emulator = _manager->CreateEmulator("macos-native-poc", LoggerLevel::LogWarning);
    if (!_emulator)
    {
        NSLog(@"[UNEmulatorBridge] Failed to create emulator instance");
        return NO;
    }

    _emulatorId = _emulator->GetId();
    _uuid = static_cast<std::string>(_emulator->GetUUID());
    _observer.uuid = _uuid;
    _emulator->DebugOff();

    [self refreshFramebufferDescriptor];
    [self subscribe];

    // Wire audio before starting: the core throttles frame production on the
    // NC_AUDIO_BUFFER_HALF_FULL signal our output device posts.
    [_audio start];
    _emulator->SetAudioCallback((__bridge void*)_audio, &CoreAudioSink);

    if (!_manager->StartEmulatorAsync(_emulatorId))
    {
        NSLog(@"[UNEmulatorBridge] StartEmulatorAsync failed");
    }

    [self.delegate emulatorBridgeDidChangeState];
    [self.delegate emulatorBridgeDidChangeResolution:_width height:_height];

    return YES;
}

- (void)stop
{
    if (!_emulator)
        return;

    [self releaseAllKeys];
    [self unsubscribe];

    _emulator->ClearAudioCallback();
    [_audio stop];

    _manager->StopEmulator(_emulatorId);
    _manager->RemoveEmulator(_emulatorId);

    {
        std::lock_guard<std::mutex> lock(_framebufferMutex);
        _framebuffer = nullptr;
        _framebufferSize = 0;
    }

    _emulator.reset();
    _emulatorId.clear();
    _uuid.clear();
    _observer.uuid.clear();

    [self.delegate emulatorBridgeDidChangeState];
}

- (void)reset
{
    if (!_emulator)
        return;

    [self releaseAllKeys];
    _emulator->Reset();
    [self.delegate emulatorBridgeDidChangeState];
}

- (void)pause
{
    if (_emulator && _emulator->IsRunning() && !_emulator->IsPaused())
    {
        [self releaseAllKeys];
        _manager->PauseEmulator(_emulatorId);
        [self.delegate emulatorBridgeDidChangeState];
    }
}

- (void)resume
{
    if (_emulator && _emulator->IsPaused())
    {
        _manager->ResumeEmulator(_emulatorId);
        [self.delegate emulatorBridgeDidChangeState];
    }
}

- (BOOL)isRunning
{
    return _emulator && _emulator->IsRunning();
}

- (BOOL)isPaused
{
    return _emulator && _emulator->IsPaused();
}

- (int)framebufferWidth
{
    return _width;
}

- (int)framebufferHeight
{
    return _height;
}

#pragma mark - Activity

- (UNActivityFlags)activityFlags
{
    if (!_emulator || !_emulator->IsRunning() || _emulator->IsPaused())
        return UNActivityFlagsNone;

    EmulatorContext* ctx = _emulator->GetContext();
    if (!ctx)
        return UNActivityFlagsNone;

    UNActivityFlags flags = UNActivityFlagsNone;

    // Tape: _tapeStarted, exposed via Tape::isStarted(). The tape auto-starts on the
    // first port read and auto-stops at end of data, so this tracks real streaming.
    if (ctx->pTape && ctx->pTape->isStarted())
        flags |= UNActivityFlagsTape;

    // Disk: the selected drive's motor. The core stops it on a 15-revolution timeout,
    // so the flag decays on its own and the LED behaves like a real drive light.
    if (ctx->pBetaDisk)
    {
        FDD* drive = ctx->pBetaDisk->getDrive();
        if (drive && drive->getMotor())
            flags |= UNActivityFlagsDisk;
    }

    // Sound: per-device peak, refreshed by SoundManager::handleFrameEnd(). MasterMix
    // never gets a peak (it has no source buffer), so aggregate the real devices.
    if (SoundManager* sound = ctx->pSoundManager)
    {
        if (!sound->isMuted())
        {
            for (const AudioDeviceInfo& device : sound->devices())
            {
                if (device.type != AudioSourceType::MasterMix && device.activeRecently)
                {
                    flags |= UNActivityFlagsSound;
                    break;
                }
            }
        }
    }

    return flags;
}

#pragma mark - Media loading

- (BOOL)saveSnapshot:(NSString*)path
{
    if (path.length == 0)
        return NO;

    // Unlike loadFile, this must NOT start the machine: saving the state of a
    // machine that has not run yet is meaningless.
    if (!_emulator)
    {
        NSLog(@"[UNEmulatorBridge] No emulator running, cannot save snapshot");
        return NO;
    }

    const std::string filePath = path.fileSystemRepresentation;
    return _emulator->SaveSnapshot(filePath) ? YES : NO;
}

- (BOOL)loadFile:(NSString*)path
{
    if (path.length == 0)
        return NO;

    if (![NSFileManager.defaultManager fileExistsAtPath:path])
    {
        NSLog(@"[UNEmulatorBridge] File not found: %@", path);
        return NO;
    }

    if (!_emulator && ![self start])
        return NO;

    const std::string filePath = path.fileSystemRepresentation;
    NSString* ext = path.pathExtension.lowercaseString;

    if ([ext isEqualToString:@"sna"] || [ext isEqualToString:@"z80"])
        return _emulator->LoadSnapshot(filePath) ? YES : NO;

    if ([ext isEqualToString:@"tap"] || [ext isEqualToString:@"tzx"])
        return _emulator->LoadTape(filePath) ? YES : NO;

    if ([ext isEqualToString:@"trd"] || [ext isEqualToString:@"scl"] ||
        [ext isEqualToString:@"fdi"] || [ext isEqualToString:@"td0"] ||
        [ext isEqualToString:@"udi"])
        return _emulator->LoadDisk(filePath) ? YES : NO;

    NSLog(@"[UNEmulatorBridge] Unsupported file type: %@", ext);
    return NO;
}

#pragma mark - Keyboard

- (void)postKey:(uint8_t)zxKey pressed:(BOOL)pressed
{
    if (!_emulator || zxKey == (uint8_t)ZXKEY_NONE)
        return;

    KeyboardEvent* event = new KeyboardEvent(zxKey,
                                             pressed ? KEY_PRESSED : KEY_RELEASED,
                                             _uuid);
    MessageCenter::DefaultMessageCenter().Post(pressed ? MC_KEY_PRESSED : MC_KEY_RELEASED, event);
}

- (void)pressKey:(UNZXKey)key
{
    const uint8_t code = (uint8_t)key;
    if (code == (uint8_t)ZXKEY_NONE)
        return;

    {
        std::lock_guard<std::mutex> lock(_keysMutex);
        if (!_pressedKeys.insert(code).second)
            return;    // already held - swallow auto-repeat
    }

    [self postKey:code pressed:YES];
}

- (void)releaseKey:(UNZXKey)key
{
    const uint8_t code = (uint8_t)key;
    if (code == (uint8_t)ZXKEY_NONE)
        return;

    {
        std::lock_guard<std::mutex> lock(_keysMutex);
        if (_pressedKeys.erase(code) == 0)
            return;
    }

    [self postKey:code pressed:NO];
}

- (void)releaseAllKeys
{
    std::set<uint8_t> held;
    {
        std::lock_guard<std::mutex> lock(_keysMutex);
        held.swap(_pressedKeys);
    }

    for (uint8_t code : held)
    {
        [self postKey:code pressed:NO];
    }
}

#pragma mark - Framebuffer

- (void)refreshFramebufferDescriptor
{
    if (!_emulator)
        return;

    EmulatorContext* ctx = _emulator->GetContext();
    if (!ctx || !ctx->pScreen)
    {
        NSLog(@"[UNEmulatorBridge] No screen in emulator context");
        return;
    }

    FramebufferDescriptor desc = ctx->pScreen->GetFramebufferDescriptor();

    std::lock_guard<std::mutex> lock(_framebufferMutex);
    _framebuffer = desc.memoryBuffer;
    _framebufferSize = desc.memoryBufferSize;
    _width = desc.width;
    _height = desc.height;

    // Once per resolution change only - never per frame. This is the single source of
    // truth the window sizing and the renderer both scale against.
    NSLog(@"[UNEmulatorBridge] framebuffer %dx%d (%zu bytes)",
          desc.width, desc.height, desc.memoryBufferSize);
}

- (BOOL)accessFramebuffer:(void (NS_NOESCAPE ^)(const void*, int, int, size_t))block
{
    std::lock_guard<std::mutex> lock(_framebufferMutex);

    if (_framebuffer == nullptr || _width <= 0 || _height <= 0)
        return NO;

    block(_framebuffer, _width, _height, _framebufferSize);
    return YES;
}

#pragma mark - MessageCenter wiring

- (void)subscribe
{
    if (_subscribed)
        return;

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();
    Observer* obs = static_cast<Observer*>(&_observer);

    mc.AddObserver(NC_VIDEO_FRAME_REFRESH, obs,
                   static_cast<ObserverCallbackMethod>(&CoreObserver::onVideoFrame));
    mc.AddObserver(NC_VIDEO_RESOLUTION_CHANGED, obs,
                   static_cast<ObserverCallbackMethod>(&CoreObserver::onResolutionChanged));
    mc.AddObserver(NC_EMULATOR_STATE_CHANGE, obs,
                   static_cast<ObserverCallbackMethod>(&CoreObserver::onStateChanged));

    _subscribed = YES;
}

- (void)unsubscribe
{
    if (!_subscribed)
        return;

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();
    Observer* obs = static_cast<Observer*>(&_observer);

    mc.RemoveObserver(NC_VIDEO_FRAME_REFRESH, obs,
                      static_cast<ObserverCallbackMethod>(&CoreObserver::onVideoFrame));
    mc.RemoveObserver(NC_VIDEO_RESOLUTION_CHANGED, obs,
                      static_cast<ObserverCallbackMethod>(&CoreObserver::onResolutionChanged));
    mc.RemoveObserver(NC_EMULATOR_STATE_CHANGE, obs,
                      static_cast<ObserverCallbackMethod>(&CoreObserver::onStateChanged));

    _subscribed = NO;
}

@end

@implementation UNEmulatorBridge (CoreCallbacks)

// All three arrive on emulator worker threads - hop to the main thread before
// touching the delegate (SwiftUI / AppKit are main-thread only).

- (void)coreDidRenderFrame
{
    __weak UNEmulatorBridge* weakSelf = self;
    dispatch_async(dispatch_get_main_queue(), ^{
        UNEmulatorBridge* strongSelf = weakSelf;
        [strongSelf.delegate emulatorBridgeDidRenderFrame];
    });
}

- (void)coreDidChangeResolution
{
    __weak UNEmulatorBridge* weakSelf = self;
    dispatch_async(dispatch_get_main_queue(), ^{
        UNEmulatorBridge* strongSelf = weakSelf;
        if (!strongSelf)
            return;

        [strongSelf refreshFramebufferDescriptor];
        [strongSelf.delegate emulatorBridgeDidChangeResolution:strongSelf.framebufferWidth
                                                        height:strongSelf.framebufferHeight];
    });
}

- (void)coreDidChangeState
{
    __weak UNEmulatorBridge* weakSelf = self;
    dispatch_async(dispatch_get_main_queue(), ^{
        UNEmulatorBridge* strongSelf = weakSelf;
        [strongSelf.delegate emulatorBridgeDidChangeState];
    });
}

- (void)coreWantsMoreAudio
{
    // Posted from the CoreAudio render thread - MessageCenter::Post is the cheap,
    // non-blocking path the core's MainLoop waits on.
    MessageCenter::DefaultMessageCenter().Post(NC_AUDIO_BUFFER_HALF_FULL);
}

@end
