#import "UNGBridge.h"
#import "unrealng_embed.h"

#include <ifaddrs.h>
#include <arpa/inet.h>

@implementation UNGBridge {
    app_emulator *_currentEmulator;
    BOOL _initialized;
    uint16_t _webApiPort;
    uint16_t _cliPort;
}

+ (instancetype)sharedBridge {
    static UNGBridge *instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[UNGBridge alloc] init];
    });
    return instance;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _currentEmulator = NULL;
        _initialized = NO;
        _webApiPort = 8090;
        _cliPort = 8765;
    }
    return self;
}

- (BOOL)isInitialized {
    return _initialized;
}

- (uint16_t)webApiPort {
    return _webApiPort;
}

- (uint16_t)cliPort {
    return _cliPort;
}

- (NSString *)localIPAddress {
    NSString *address = @"127.0.0.1";
    struct ifaddrs *interfaces = NULL;
    struct ifaddrs *temp_addr = NULL;

    if (getifaddrs(&interfaces) == 0) {
        temp_addr = interfaces;
        while (temp_addr != NULL) {
            if (temp_addr->ifa_addr && temp_addr->ifa_addr->sa_family == AF_INET) {
                NSString *name = [NSString stringWithUTF8String:temp_addr->ifa_name];
                if ([name isEqualToString:@"en0"] || [name isEqualToString:@"en1"]) {
                    address = [NSString stringWithUTF8String:inet_ntoa(((struct sockaddr_in *)temp_addr->ifa_addr)->sin_addr)];
                    break;
                }
            }
            temp_addr = temp_addr->ifa_next;
        }
    }
    freeifaddrs(interfaces);
    return address;
}

- (BOOL)startSystemWithResourceRoot:(NSString *)resourceRoot
                          writableRoot:(NSString *)writableRoot
                             webApiPort:(uint16_t)webApiPort
                                cliPort:(uint16_t)cliPort {
    if (_initialized) return YES;

    _webApiPort = webApiPort > 0 ? webApiPort : 8090;
    _cliPort = cliPort > 0 ? cliPort : 8765;

    app_init_params params;
    memset(&params, 0, sizeof(params));
    params.resource_root = [resourceRoot UTF8String];
    params.writable_root = [writableRoot UTF8String];
    params.log_level = 2;
    params.automation_mask = APP_AUTOMATION_ALL;
    params.webapi_port = _webApiPort;
    params.cli_port = _cliPort;

    app_result res = app_init(&params);
    if (res == APP_OK) {
        _initialized = YES;
        return YES;
    }
    return NO;
}

- (void)shutdownSystem {
    if (_currentEmulator) {
        [self destroyEmulator];
    }
    if (_initialized) {
        app_shutdown();
        _initialized = NO;
    }
}

- (BOOL)createEmulatorWithModel:(NSString *)model symbolicId:(NSString *)symbolicId {
    if (!_initialized) return NO;
    if (_currentEmulator) {
        [self destroyEmulator];
    }

    const char *modelStr = model ? [model UTF8String] : "PENTAGON";
    const char *symIdStr = symbolicId ? [symbolicId UTF8String] : "ios-01";

    app_result res = app_create(modelStr, symIdStr, &_currentEmulator);
    return (res == APP_OK && _currentEmulator != NULL);
}

- (BOOL)startEmulator {
    if (!_currentEmulator) return NO;
    app_result res = app_start(_currentEmulator);
    return (res == APP_OK);
}

- (void)destroyEmulator {
    if (_currentEmulator) {
        app_destroy(_currentEmulator);
        _currentEmulator = NULL;
    }
}

- (BOOL)getFrameWidth:(uint16_t *)width height:(uint16_t *)height latchTimestampUs:(uint64_t *)latchTs {
    if (!_currentEmulator) return NO;
    app_result res = app_frame_info(_currentEmulator, width, height, latchTs);
    return (res == APP_OK);
}

- (BOOL)copyFrameToBuffer:(void *)buffer size:(size_t)size {
    if (!_currentEmulator) return NO;
    app_result res = app_copy_frame(_currentEmulator, buffer, size);
    return (res == APP_OK);
}

- (void)setPresentDelay:(uint8_t)frames {
    if (_currentEmulator) {
        app_set_present_delay(_currentEmulator, frames);
    }
}

- (BOOL)openAudioDevice {
    if (!_currentEmulator) return NO;
    app_result res = app_audio_open_device(_currentEmulator);
    return (res == APP_OK);
}

- (BOOL)attachAudioPullWithSampleRate:(uint32_t)sampleRate {
    if (!_currentEmulator) return NO;
    app_result res = app_audio_attach_pull(_currentEmulator, sampleRate);
    return (res == APP_OK);
}

- (size_t)pullAudioF32:(float *)buffer frames:(size_t)frames {
    if (!_currentEmulator || !buffer || frames == 0) return 0;
    return app_audio_pull_f32(_currentEmulator, buffer, frames);
}

- (void)setAudioActive:(BOOL)active {
    if (_currentEmulator) {
        app_audio_set_active(_currentEmulator, active ? 1 : 0);
    }
}

- (BOOL)pressKey:(NSString *)keyName {
    if (!_currentEmulator || !keyName) return NO;
    app_result res = app_keyboard_press(_currentEmulator, [keyName UTF8String]);
    return (res == APP_OK);
}

- (BOOL)releaseKey:(NSString *)keyName {
    if (!_currentEmulator || !keyName) return NO;
    app_result res = app_keyboard_release(_currentEmulator, [keyName UTF8String]);
    return (res == APP_OK);
}

- (BOOL)tapKey:(NSString *)keyName {
    if (!_currentEmulator || !keyName) return NO;
    app_result res = app_keyboard_tap(_currentEmulator, [keyName UTF8String], 2);
    return (res == APP_OK);
}

- (void)releaseAllKeys {
    if (_currentEmulator) {
        app_keyboard_release_all(_currentEmulator);
    }
}

@end
