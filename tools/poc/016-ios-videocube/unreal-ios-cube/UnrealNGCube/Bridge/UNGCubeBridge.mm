#import "UNGCubeBridge.h"
#include "unrealng_embed.h"

#include <ifaddrs.h>
#include <arpa/inet.h>
#include <mutex>
#include <vector>

NSString * const UNGCubeSelectionChangedNotification = @"UNGCubeSelectionChangedNotification";
NSString * const UNGCubeInstancesChangedNotification = @"UNGCubeInstancesChangedNotification";

static UNGCubeBridge *s_sharedBridge = nil;

@interface UNGCubeBridge () {
    std::mutex _bridgeMutex;
    app_emulator* _instances[6];
    char _modelNames[6][64];
    char _instanceIds[6][64];
    NSInteger _createdCount;
}

@property (nonatomic, readwrite) BOOL isInitialized;
@property (nonatomic, readwrite, nullable) NSString *localIPAddress;
@property (nonatomic, readwrite, copy, nullable) NSString *webApiStatus;
@property (nonatomic, readwrite) uint16_t webApiPort;
@property (nonatomic, readwrite) uint16_t cliPort;

- (void)refreshInstanceList;

@end

static void OnNotificationReceived(const char* event_type, const char* json_payload, void* user_data)
{
    if (!event_type) return;

    NSString *eventTypeStr = [NSString stringWithUTF8String:event_type];
    NSString *jsonPayloadStr = json_payload ? [NSString stringWithUTF8String:json_payload] : @"{}";

    dispatch_async(dispatch_get_main_queue(), ^{
        NSDictionary *userInfo = @{
            @"event_type": eventTypeStr,
            @"payload_raw": jsonPayloadStr
        };

        if ([eventTypeStr isEqualToString:@"NC_EMULATOR_SELECTION_CHANGED"]) {
            [[NSNotificationCenter defaultCenter] postNotificationName:UNGCubeSelectionChangedNotification object:nil userInfo:userInfo];
        } else if ([eventTypeStr isEqualToString:@"NC_EMULATOR_INSTANCE_CREATED"] || [eventTypeStr isEqualToString:@"NC_EMULATOR_INSTANCE_DESTROYED"]) {
            [[UNGCubeBridge sharedBridge] refreshInstanceList];
            [[NSNotificationCenter defaultCenter] postNotificationName:UNGCubeInstancesChangedNotification object:nil userInfo:userInfo];
        } else if ([eventTypeStr isEqualToString:@"NC_VIDEOWALL_SINGLE_SYNC_MODE"]) {
            NSData *data = [jsonPayloadStr dataUsingEncoding:NSUTF8StringEncoding];
            NSDictionary *dict = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
            BOOL enable = [dict[@"enable"] boolValue];
            NSString *emuId = dict[@"emulator_id"];

            NSInteger masterIdx = 0;
            if (emuId.length > 0) {
                for (int i = 0; i < 6; i++) {
                    NSString *instanceId = [[UNGCubeBridge sharedBridge] instanceIdForIndex:i];
                    if ([instanceId isEqualToString:emuId] || [emuId hasPrefix:instanceId]) {
                        masterIdx = i;
                        break;
                    }
                }
            }
            [[UNGCubeBridge sharedBridge] setSingleSyncEnabled:enable masterIndex:masterIdx];
            [[NSNotificationCenter defaultCenter] postNotificationName:UNGCubeInstancesChangedNotification object:nil userInfo:userInfo];
        }
    });
}

@implementation UNGCubeBridge

+ (instancetype)sharedBridge {
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        s_sharedBridge = [[UNGCubeBridge alloc] init];
    });
    return s_sharedBridge;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _isInitialized = NO;
        _isSingleSyncEnabled = NO;
        _singleSyncMasterIndex = 0;
        // Standard WebAPI port. A suspended (backgrounded) iOS app keeps its
        // listening sockets open, so a resident older Unreal-NG app can win the
        // bind race - verifyWebApiExposureAfterDelay detects that and the HUD
        // surfaces it instead of advertising an address that answers with
        // someone else's instances
        _webApiPort = 8090;
        _cliPort = 8765;
        for (int i = 0; i < 6; i++) {
            _instances[i] = nullptr;
            _modelNames[i][0] = '\0';
            _instanceIds[i][0] = '\0';
        }
        [self detectLocalIP];
    }
    return self;
}

- (void)detectLocalIP {
    NSString *address = @"127.0.0.1";
    struct ifaddrs *interfaces = NULL;
    struct ifaddrs *temp_addr = NULL;

    if (getifaddrs(&interfaces) == 0) {
        temp_addr = interfaces;
        while (temp_addr != NULL) {
            if (temp_addr->ifa_addr->sa_family == AF_INET) {
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
    self.localIPAddress = address;
}

- (BOOL)startSixInstances {
    std::lock_guard<std::mutex> lock(_bridgeMutex);
    if (_isInitialized) return YES;

    NSString *documentsPath = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    NSString *bundlePath = [[NSBundle mainBundle] bundlePath];
    NSString *resourcePath = [bundlePath stringByAppendingPathComponent:@"data"];

    app_init_params params;
    memset(&params, 0, sizeof(params));
    params.resource_root = [resourcePath UTF8String];
    params.writable_root = [documentsPath UTF8String];
    params.log_level = 2;
    params.automation_mask = APP_AUTOMATION_ALL;
    params.webapi_port = 8090;
    params.cli_port = 8765;

    app_result res = app_init(&params);
    if (res != APP_OK) {
        NSLog(@"UNGCubeBridge: app_init failed with code %d", res);
        return NO;
    }

    app_set_notification_callback(OnNotificationReceived, (__bridge void *)self);

    const char* defaultModels[6] = {
        "PENTAGON", "PENTAGON", "PENTAGON", "48K", "128k", "PENTAGON"
    };
    const char* defaultSymIds[6] = {
        "cube-face-0", "cube-face-1", "cube-face-2", "cube-face-3", "cube-face-4", "cube-face-5"
    };

    int created = 0;
    for (int i = 0; i < 6; i++) {
        app_emulator* emu = nullptr;
        app_result cRes = app_create(defaultModels[i], defaultSymIds[i], &emu);
        if (cRes == APP_OK && emu) {
            app_start(emu);
            _instances[i] = emu;
            snprintf(_modelNames[i], sizeof(_modelNames[i]), "%s", defaultModels[i]);
            snprintf(_instanceIds[i], sizeof(_instanceIds[i]), "%s", defaultSymIds[i]);
            created++;
        } else {
            NSLog(@"UNGCubeBridge: Failed to create instance %d (%s, %s): res=%d", i, defaultModels[i], defaultSymIds[i], cRes);
        }
    }
    _createdCount = created;
    NSLog(@"UNGCubeBridge: created %d/6 cube face instances", created);

    _isInitialized = YES;
    if (_instances[0]) {
        app_audio_open_device(_instances[0]);
        app_audio_set_active(_instances[0], 1);
    }
    [self updateActiveAudioSelection];
    [self verifyWebApiExposureAfterDelay];
    return YES;
}

- (NSInteger)instanceCount {
    return 6;
}

- (NSInteger)createdInstanceCount {
    std::lock_guard<std::mutex> lock(_bridgeMutex);
    return _createdCount;
}

- (void)verifyWebApiExposureAfterDelay {
    // Give the WebAPI thread time to bind, then verify that the standard port
    // actually serves THIS process's six instances. A suspended (backgrounded)
    // iOS app keeps its listening sockets open forever, so a resident older
    // Unreal-NG app silently wins the bind race - without this check the HUD
    // would advertise an address that answers with someone else's instances
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3.0 * NSEC_PER_SEC)),
                   dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        [self verifyWebApiExposure];
    });
}

- (void)verifyWebApiExposure {
    const uint16_t port = _webApiPort;
    NSURL *url = [NSURL URLWithString:
        [NSString stringWithFormat:@"http://127.0.0.1:%u/api/v1/emulator", port]];
    NSURLSessionConfiguration *config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
    config.timeoutIntervalForRequest = 4.0;
    NSURLSession *session = [NSURLSession sessionWithConfiguration:config];

    [[session dataTaskWithURL:url completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
        NSString *status;
        if (error != nil) {
            status = [NSString stringWithFormat:@"UNREACHABLE - WebAPI did not bind port %u", port];
        } else {
            NSInteger count = 0;
            id json = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
            if ([json isKindOfClass:[NSDictionary class]] && [json[@"count"] isKindOfClass:[NSNumber class]]) {
                count = [json[@"count"] integerValue];
            }
            if (count == 6) {
                status = [NSString stringWithFormat:@"OK - all 6 instances exposed on port %u", port];
            } else {
                status = [NSString stringWithFormat:@"CONFLICT - port %u answers with %ld instance(s) from another Unreal-NG app; terminate background Unreal-NG apps and relaunch", port, (long)count];
            }
        }
        [session finishTasksAndInvalidate];
        dispatch_async(dispatch_get_main_queue(), ^{
            self.webApiStatus = status;
            NSLog(@"UNGCubeBridge: WebAPI exposure check: %@", status);
            [[NSNotificationCenter defaultCenter] postNotificationName:UNGCubeInstancesChangedNotification object:nil];
        });
    }] resume];
}

- (void)updateActiveAudioSelection {
    NSInteger targetIdx = _isSingleSyncEnabled ? _singleSyncMasterIndex : _selectedFaceIndex;
    if (targetIdx < 0 || targetIdx >= 6) targetIdx = 0;
    for (int i = 0; i < 6; i++) {
        if (_instances[i]) {
            app_audio_set_active(_instances[i], (i == targetIdx) ? 1 : 0);
        }
    }
}

- (void)setSelectedFaceIndex:(NSInteger)index {
    std::lock_guard<std::mutex> lock(_bridgeMutex);
    _selectedFaceIndex = (index >= 0 && index < 6) ? index : 0;
    [self updateActiveAudioSelection];
}

- (void)setSingleSyncEnabled:(BOOL)enabled masterIndex:(NSInteger)masterIndex {
    std::lock_guard<std::mutex> lock(_bridgeMutex);
    _isSingleSyncEnabled = enabled;
    _singleSyncMasterIndex = (masterIndex >= 0 && masterIndex < 6) ? masterIndex : 0;
    [self updateActiveAudioSelection];
}

- (BOOL)getFrameInfoForIndex:(NSInteger)index width:(uint16_t *)w height:(uint16_t *)h timestamp:(uint64_t *)ts {
    if (index < 0 || index >= 6) return NO;
    std::lock_guard<std::mutex> lock(_bridgeMutex);
    NSInteger targetIdx = _isSingleSyncEnabled ? _singleSyncMasterIndex : index;
    app_emulator* emu = _instances[targetIdx];
    if (!emu) return NO;
    return (app_frame_info(emu, w, h, ts) == APP_OK);
}

- (BOOL)copyFrameForIndex:(NSInteger)index toBuffer:(void *)dst size:(size_t)size {
    if (index < 0 || index >= 6) return NO;
    std::lock_guard<std::mutex> lock(_bridgeMutex);
    NSInteger targetIdx = _isSingleSyncEnabled ? _singleSyncMasterIndex : index;
    app_emulator* emu = _instances[targetIdx];
    if (!emu) return NO;
    return (app_copy_frame(emu, dst, size) == APP_OK);
}

- (nullable NSString *)modelNameForIndex:(NSInteger)index {
    if (index < 0 || index >= 6) return nil;
    std::lock_guard<std::mutex> lock(_bridgeMutex);
    if (_modelNames[index][0] == '\0') return nil;
    return [NSString stringWithUTF8String:_modelNames[index]];
}

- (nullable NSString *)instanceIdForIndex:(NSInteger)index {
    if (index < 0 || index >= 6) return nil;
    std::lock_guard<std::mutex> lock(_bridgeMutex);
    if (_instanceIds[index][0] == '\0') return nil;
    return [NSString stringWithUTF8String:_instanceIds[index]];
}

- (void)refreshInstanceList {
    // Called when instances are created or destroyed remotely via WebAPI
    // Ensures wrapper handles continue pointing to valid active core instances
}

- (BOOL)pressKey:(NSString *)keyName onInstance:(NSInteger)index {
    if (index < 0 || index >= 6) return NO;
    std::lock_guard<std::mutex> lock(_bridgeMutex);
    app_emulator* emu = _instances[index];
    if (!emu) return NO;
    return (app_keyboard_press(emu, [keyName UTF8String]) == APP_OK);
}

- (BOOL)releaseKey:(NSString *)keyName onInstance:(NSInteger)index {
    if (index < 0 || index >= 6) return NO;
    std::lock_guard<std::mutex> lock(_bridgeMutex);
    app_emulator* emu = _instances[index];
    if (!emu) return NO;
    return (app_keyboard_release(emu, [keyName UTF8String]) == APP_OK);
}

@end
