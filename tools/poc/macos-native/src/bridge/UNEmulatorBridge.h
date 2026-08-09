// UNEmulatorBridge.h - Objective-C facade over the unreal-ng C++ core.
//
// HARD RULE: this header is imported by Swift through the bridging header, so it
// must contain plain Objective-C only. Every line of C++ contact lives in
// UNEmulatorBridge.mm.

#pragma once

#import <Foundation/Foundation.h>

#import "UNZXKeys.h"

NS_ASSUME_NONNULL_BEGIN

/// Which subsystems are doing something right now. Read once per frame from Swift;
/// every member is a plain flag read on the core, no work is done to produce it.
typedef NS_OPTIONS(NSUInteger, UNActivityFlags) {
    UNActivityFlagsNone  = 0,
    UNActivityFlagsTape  = 1 << 0,
    UNActivityFlagsDisk  = 1 << 1,
    UNActivityFlagsSound = 1 << 2,
};

@protocol UNEmulatorBridgeDelegate <NSObject>
/// A new video frame is ready in the framebuffer. Always delivered on the main thread.
- (void)emulatorBridgeDidRenderFrame;
/// Video mode changed; the framebuffer was re-described. Main thread.
- (void)emulatorBridgeDidChangeResolution:(int)width height:(int)height;
/// Run/pause/stop state changed. Main thread.
- (void)emulatorBridgeDidChangeState;
@end

@interface UNEmulatorBridge : NSObject

@property (nonatomic, weak, nullable) id<UNEmulatorBridgeDelegate> delegate;

@property (nonatomic, readonly) BOOL isRunning;
@property (nonatomic, readonly) BOOL isPaused;
@property (nonatomic, readonly) int framebufferWidth;
@property (nonatomic, readonly) int framebufferHeight;

/// Live tape / disk / sound activity. Cheap enough to poll from the frame tick:
/// it dereferences three core pointers and reads flags the core already maintains.
/// Returns UNActivityFlagsNone when the emulator is not running.
@property (nonatomic, readonly) UNActivityFlags activityFlags;

/// Creates the emulator instance, wires audio + notifications and starts it.
- (BOOL)start;
- (void)stop;
- (void)reset;
- (void)pause;
- (void)resume;

/// Loads a snapshot / tape / disk image, dispatching on the file extension.
/// Starts the emulator first if it is not running yet.
- (BOOL)loadFile:(NSString*)path NS_SWIFT_NAME(loadFile(_:));

/// File extensions this build accepts (lowercase, no dot).
+ (NSArray<NSString*>*)supportedFileExtensions;

#pragma mark - Keyboard

- (void)pressKey:(UNZXKey)key NS_SWIFT_NAME(pressKey(_:));
- (void)releaseKey:(UNZXKey)key NS_SWIFT_NAME(releaseKey(_:));
/// Releases every key currently held down in the ZX matrix.
/// Must be called on focus loss and before any modal/fullscreen transition:
/// a modifier whose key-up is swallowed by the transition otherwise stays latched
/// in the matrix and the keyboard appears dead until the machine is reset.
- (void)releaseAllKeys;

#pragma mark - Framebuffer

/// Gives the caller scoped access to the core framebuffer (RGBA8, tightly packed).
/// Returns NO (block not invoked) when no framebuffer is available.
- (BOOL)accessFramebuffer:(void (NS_NOESCAPE ^)(const void* bytes,
                                                int width,
                                                int height,
                                                size_t byteSize))block NS_SWIFT_NAME(accessFramebuffer(_:));

@end

NS_ASSUME_NONNULL_END
