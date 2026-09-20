#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface UNGBridge : NSObject

@property (nonatomic, readonly) BOOL isInitialized;
@property (nonatomic, readonly, nullable) NSString *localIPAddress;
@property (nonatomic, readonly) uint16_t webApiPort;
@property (nonatomic, readonly) uint16_t cliPort;

+ (instancetype)sharedBridge;

- (BOOL)startSystemWithResourceRoot:(NSString *)resourceRoot
                          writableRoot:(NSString *)writableRoot
                             webApiPort:(uint16_t)webApiPort
                                cliPort:(uint16_t)cliPort;
- (void)shutdownSystem;

- (BOOL)createEmulatorWithModel:(NSString *)model symbolicId:(NSString *)symbolicId;
- (BOOL)startEmulator;
- (void)destroyEmulator;

- (BOOL)getFrameWidth:(uint16_t *)width height:(uint16_t *)height latchTimestampUs:(uint64_t *)latchTs;
- (BOOL)copyFrameToBuffer:(void *)buffer size:(size_t)size;
- (void)setPresentDelay:(uint8_t)frames;

- (BOOL)openAudioDevice;
- (BOOL)attachAudioPullWithSampleRate:(uint32_t)sampleRate;
- (size_t)pullAudioF32:(float *)buffer frames:(size_t)frames;
- (void)setAudioActive:(BOOL)active;

- (BOOL)pressKey:(NSString *)keyName;
- (BOOL)releaseKey:(NSString *)keyName;
- (BOOL)tapKey:(NSString *)keyName;
- (void)releaseAllKeys;

@end

NS_ASSUME_NONNULL_END
