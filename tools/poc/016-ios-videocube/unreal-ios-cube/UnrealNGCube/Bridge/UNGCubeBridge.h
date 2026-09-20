#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

FOUNDATION_EXPORT NSString * const UNGCubeSelectionChangedNotification;
FOUNDATION_EXPORT NSString * const UNGCubeInstancesChangedNotification;

@interface UNGCubeBridge : NSObject

+ (instancetype)sharedBridge NS_SWIFT_NAME(shared());

@property (nonatomic, readonly) BOOL isInitialized;
@property (nonatomic, readonly, nullable) NSString *localIPAddress;
@property (nonatomic, readonly) uint16_t webApiPort;
@property (nonatomic, readonly) uint16_t cliPort;
/// Result of the post-boot WebAPI self-check (nil = not yet checked).
/// "OK - ..." when the port serves this app's 6 instances, "CONFLICT - ..."
/// when another Unreal-NG app owns the port, "UNREACHABLE - ..." when nothing
/// bound it.
@property (nonatomic, readonly, nullable) NSString *webApiStatus;

@property (nonatomic, readwrite) BOOL isSingleSyncEnabled;
@property (nonatomic, readwrite) NSInteger singleSyncMasterIndex;
@property (nonatomic, readwrite) NSInteger selectedFaceIndex;

- (BOOL)startSixInstances;
- (NSInteger)instanceCount;
- (NSInteger)createdInstanceCount NS_SWIFT_NAME(createdInstanceCount);
- (void)setSingleSyncEnabled:(BOOL)enabled masterIndex:(NSInteger)masterIndex NS_SWIFT_NAME(setSingleSync(enabled:masterIndex:));
- (void)setSelectedFaceIndex:(NSInteger)index NS_SWIFT_NAME(setSelectedFaceIndex(_:));

- (BOOL)getFrameInfoForIndex:(NSInteger)index width:(uint16_t *)w height:(uint16_t *)h timestamp:(uint64_t *)ts NS_SWIFT_NAME(getFrameInfo(forIndex:width:height:timestamp:));
- (BOOL)copyFrameForIndex:(NSInteger)index toBuffer:(void *)dst size:(size_t)size NS_SWIFT_NAME(copyFrame(forIndex:toBuffer:size:));
/// Atomically copies the latest end-of-frame staged snapshot for a face.
/// Staging is refreshed by the VIDEO_FRAME_REFRESH notification exactly at
/// frame boundaries (never mid-frame), so the copied image is always a
/// complete, tear-free frame. `sequence` bumps on every staged frame - use
/// it to skip redundant copies. Returns NO until the first frame is staged.
- (BOOL)copyStagedFrameForIndex:(NSInteger)index toBuffer:(void *)dst capacity:(size_t)capacity width:(uint16_t *)outWidth height:(uint16_t *)outHeight sequence:(uint64_t *)outSequence NS_SWIFT_NAME(copyStagedFrame(forIndex:toBuffer:capacity:width:height:sequence:));
/// Current staging sequence for a face (0 = nothing staged yet). Lets the
/// renderer poll for new frames without copying anything.
- (uint64_t)stagedFrameSequenceForIndex:(NSInteger)index NS_SWIFT_NAME(stagedFrameSequence(forIndex:));
- (nullable NSString *)modelNameForIndex:(NSInteger)index NS_SWIFT_NAME(modelName(forIndex:));
- (nullable NSString *)instanceIdForIndex:(NSInteger)index NS_SWIFT_NAME(instanceId(forIndex:));

- (BOOL)pressKey:(NSString *)keyName onInstance:(NSInteger)index NS_SWIFT_NAME(pressKey(_:onInstance:));
- (BOOL)releaseKey:(NSString *)keyName onInstance:(NSInteger)index NS_SWIFT_NAME(releaseKey(_:onInstance:));

@end

NS_ASSUME_NONNULL_END
