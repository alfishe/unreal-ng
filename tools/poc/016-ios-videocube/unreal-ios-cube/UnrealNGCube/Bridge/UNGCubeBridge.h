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
- (nullable NSString *)modelNameForIndex:(NSInteger)index NS_SWIFT_NAME(modelName(forIndex:));
- (nullable NSString *)instanceIdForIndex:(NSInteger)index NS_SWIFT_NAME(instanceId(forIndex:));

- (BOOL)pressKey:(NSString *)keyName onInstance:(NSInteger)index NS_SWIFT_NAME(pressKey(_:onInstance:));
- (BOOL)releaseKey:(NSString *)keyName onInstance:(NSInteger)index NS_SWIFT_NAME(releaseKey(_:onInstance:));

@end

NS_ASSUME_NONNULL_END
