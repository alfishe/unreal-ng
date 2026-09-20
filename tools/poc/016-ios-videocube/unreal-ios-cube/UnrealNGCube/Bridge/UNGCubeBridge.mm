#import "UNGCubeBridge.h"
#include "unrealng_embed.h"

#include <ifaddrs.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mutex>
#include <vector>

NSString * const UNGCubeSelectionChangedNotification = @"UNGCubeSelectionChangedNotification";
NSString * const UNGCubeInstancesChangedNotification = @"UNGCubeInstancesChangedNotification";

static UNGCubeBridge *s_sharedBridge = nil;

// Dedicated-mode instance table, shared by boot, single-sync collapse and respawn
static const char* kCubeModels[6] = {
    "PENTAGON", "PENTAGON", "PENTAGON", "48K", "128k", "PENTAGON"
};
static const char* kCubeSymIds[6] = {
    "cube-face-0", "cube-face-1", "cube-face-2", "cube-face-3", "cube-face-4", "cube-face-5"
};

// Creates, starts and identifies a cube-face instance. Called with NO
// _bridgeMutex held: spawn posts to the MessageCenter whose worker also
// resolves frame events through that lock
static app_emulator* SpawnCubeInstance(int index, char *model, char *sym, char *uuid)
{
    app_emulator *emu = nullptr;
    app_result cRes = app_create(kCubeModels[index], kCubeSymIds[index], &emu);
    if (cRes != APP_OK || !emu) {
        NSLog(@"UNGCubeBridge: Failed to create instance %d (%s, %s): res=%d", index, kCubeModels[index], kCubeSymIds[index], (int)cRes);
        return nullptr;
    }
    app_start(emu);
    snprintf(model, 64, "%s", kCubeModels[index]);
    snprintf(sym, 64, "%s", kCubeSymIds[index]);
    uuid[0] = '\0';
    char uid[64];
    if (app_emulator_id(emu, uid, sizeof(uid)) == APP_OK) {
        snprintf(uuid, 64, "%s", uid);
    }
    return emu;
}

@interface UNGCubeBridge () {
    std::mutex _bridgeMutex;
    app_emulator* _instances[6];
    char _modelNames[6][64];
    char _instanceIds[6][64];
    char _instanceUuids[6][64];   // manager UUIDs, for payload -> slot mapping
    NSInteger _createdCount;
    int _frameCopiesInFlight;     // staged copies running outside the lock (bridge-guarded);
                                  // topology must wait for 0 before freeing a pinned wrapper
    dispatch_queue_t _topologyQueue;  // serializes single-sync <-> dedicated switches

    // End-of-frame staged snapshots (tear-free handoff to the renderer):
    // refreshed by the VIDEO_FRAME_REFRESH notification the core posts right
    // after latching a completed frame, never mid-frame
    std::mutex _stageMutex;        // guards everything below; leaf lock (never taken before _bridgeMutex)
    uint8_t* _faceStage[6];        // latest complete RGBA8 frame per face
    size_t _faceStageSize[6];      // allocated bytes per staging buffer
    uint16_t _faceStageW[6];
    uint16_t _faceStageH[6];
    uint64_t _faceStageSeq[6];     // bumped on every staged frame
}

@property (nonatomic, readwrite) BOOL isInitialized;
@property (nonatomic, readwrite, nullable) NSString *localIPAddress;
@property (nonatomic, readwrite, copy, nullable) NSString *webApiStatus;
@property (nonatomic, readwrite) uint16_t webApiPort;
@property (nonatomic, readwrite) uint16_t cliPort;

- (void)refreshInstanceList;
- (nullable NSString *)instanceUuidForIndex:(NSInteger)index;
- (void)onFrameRefreshForUuid:(NSString *)uuid;
- (void)collapseToSingleEmulatorAtMaster:(NSInteger)master previousAudioTarget:(NSInteger)previousTarget;
- (void)respawnDedicatedInstances;
- (void)destroyDetachedInstances:(app_emulator **)victims;

@end

static void OnNotificationReceived(const char* event_type, const char* json_payload, void* user_data)
{
    if (!event_type) return;

    NSString *eventTypeStr = [NSString stringWithUTF8String:event_type];
    NSString *jsonPayloadStr = json_payload ? [NSString stringWithUTF8String:json_payload] : @"{}";

    // Hottest path (up to 6 instances x 50 Hz = 300 events/s): runs inline on
    // the MessageCenter worker thread. Never dispatch_async to main from here
    // - the flood would starve the UI. The staging copy itself is atomic and
    // tear-free: the core latches the completed frame before posting
    if ([eventTypeStr isEqualToString:@"VIDEO_FRAME_REFRESH"]) {
        NSData *data = [jsonPayloadStr dataUsingEncoding:NSUTF8StringEncoding];
        NSDictionary *dict = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
        NSString *emuId = dict[@"emulator_id"];
        if (emuId.length > 0) {
            [[UNGCubeBridge sharedBridge] onFrameRefreshForUuid:emuId];
        }
        return;
    }

    dispatch_async(dispatch_get_main_queue(), ^{
        NSDictionary *userInfo = @{
            @"event_type": eventTypeStr,
            @"payload_raw": jsonPayloadStr
        };

        // Topic strings arrive without the NC_ prefix (that is only the
        // C++ constant name; platform.h values are e.g. "EMULATOR_SELECTION_CHANGED")
        if ([eventTypeStr isEqualToString:@"EMULATOR_SELECTION_CHANGED"]) {
            [[NSNotificationCenter defaultCenter] postNotificationName:UNGCubeSelectionChangedNotification object:nil userInfo:userInfo];
        } else if ([eventTypeStr isEqualToString:@"EMULATOR_INSTANCE_CREATED"] || [eventTypeStr isEqualToString:@"EMULATOR_INSTANCE_DESTROYED"]) {
            [[UNGCubeBridge sharedBridge] refreshInstanceList];
            [[NSNotificationCenter defaultCenter] postNotificationName:UNGCubeInstancesChangedNotification object:nil userInfo:userInfo];
        } else if ([eventTypeStr isEqualToString:@"VIDEOWALL_SINGLE_SYNC_MODE"]) {
            NSData *data = [jsonPayloadStr dataUsingEncoding:NSUTF8StringEncoding];
            NSDictionary *dict = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
            BOOL enable = [dict[@"enable"] boolValue];
            NSString *emuId = dict[@"emulator_id"];

            NSInteger masterIdx = 0;
            if (emuId.length > 0) {
                // The payload carries the manager UUID; symbolic ids are kept
                // as a fallback for payloads constructed from symbolic names
                for (int i = 0; i < 6; i++) {
                    NSString *uuid = [[UNGCubeBridge sharedBridge] instanceUuidForIndex:i];
                    NSString *instanceId = [[UNGCubeBridge sharedBridge] instanceIdForIndex:i];
                    if ([uuid isEqualToString:emuId] || [instanceId isEqualToString:emuId] || [emuId hasPrefix:instanceId]) {
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
        _topologyQueue = dispatch_queue_create("ungcube.topology", DISPATCH_QUEUE_SERIAL);
        // Standard WebAPI port. A suspended (backgrounded) iOS app keeps its
        // listening sockets open, so a resident older Unreal-NG app can win the
        // bind race - verifyWebApiExposureAfterDelay detects that and the HUD
        // surfaces it instead of advertising an address that answers with
        // someone else's instances
        _webApiPort = 8090;
        _cliPort = 8765;
        _frameCopiesInFlight = 0;
        for (int i = 0; i < 6; i++) {
            _instances[i] = nullptr;
            _modelNames[i][0] = '\0';
            _instanceIds[i][0] = '\0';
            _instanceUuids[i][0] = '\0';
            _faceStage[i] = nullptr;
            _faceStageSize[i] = 0;
            _faceStageW[i] = 0;
            _faceStageH[i] = 0;
            _faceStageSeq[i] = 0;
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
    {
        std::lock_guard<std::mutex> lock(_bridgeMutex);
        if (_isInitialized) return YES;
    }

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

    {
        std::lock_guard<std::mutex> lock(_bridgeMutex);
        _isInitialized = YES;
    }

    // Creates the 6 dedicated instances; the same path runs on every
    // single-sync OFF switch so instances always come back individually
    [self respawnDedicatedInstances];
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
    // Expected live topology: 6 dedicated instances, or exactly 1 while
    // single-sync is enabled (its 6 faces are frame replicas, not instances)
    const NSInteger expected = _isSingleSyncEnabled ? 1 : 6;
    // Give the WebAPI thread time to bind, then verify that the standard port
    // actually serves THIS process's instances. A suspended (backgrounded)
    // iOS app keeps its listening sockets open forever, so a resident older
    // Unreal-NG app silently wins the bind race - without this check the HUD
    // would advertise an address that answers with someone else's instances
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3.0 * NSEC_PER_SEC)),
                   dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        [self verifyWebApiExposureExpected:expected];
    });
}

- (void)verifyWebApiExposureExpected:(NSInteger)expected {
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
            if (count == expected) {
                status = [NSString stringWithFormat:@"OK - %ld instance(s) exposed on port %u", (long)count, port];
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
    const NSInteger idx = (masterIndex >= 0 && masterIndex < 6) ? masterIndex : 0;
    BOOL topologyChange = NO;
    NSInteger previousAudioTarget = 0;
    {
        std::lock_guard<std::mutex> lock(_bridgeMutex);
        // Captured before the flip: whichever instance currently owns the
        // audio device gets destroyed by the collapse and the master needs
        // the device re-opened
        previousAudioTarget = _isSingleSyncEnabled ? _singleSyncMasterIndex : _selectedFaceIndex;
        topologyChange = (_isSingleSyncEnabled != enabled) ||
                         (enabled && _singleSyncMasterIndex != idx);
        _isSingleSyncEnabled = enabled;
        _singleSyncMasterIndex = idx;
        [self updateActiveAudioSelection];
    }
    if (!topologyChange) return;

    // Apply the requested instance topology off the main thread. ON keeps a
    // single emulator alive and all 6 faces replicate it (frame routing above);
    // OFF always re-spawns the 6 dedicated instances from scratch
    if (enabled) {
        dispatch_async(_topologyQueue, ^{
            [self collapseToSingleEmulatorAtMaster:idx previousAudioTarget:previousAudioTarget];
        });
    } else {
        dispatch_async(_topologyQueue, ^{
            [self respawnDedicatedInstances];
        });
    }
}

- (void)collapseToSingleEmulatorAtMaster:(NSInteger)master previousAudioTarget:(NSInteger)previousTarget {
    // Phase 1 - detach victims under the lock. Staging skips null slots from
    // this moment on, so no new frame copy can pin a doomed wrapper
    app_emulator *victims[6] = {};
    int destroyed = 0;
    {
        std::lock_guard<std::mutex> lock(_bridgeMutex);
        for (int i = 0; i < 6; i++) {
            if (i == master || !_instances[i]) continue;
            victims[i] = _instances[i];
            _instances[i] = nullptr;
            _instanceUuids[i][0] = '\0';   // stale refresh events skip this slot
            destroyed++;
        }
    }
    [self destroyDetachedInstances:victims];

    // Phase 2 - fresh master if the slot was empty (master switch while
    // collapsed). Spawned with no bridge lock held, then attached
    char model[64] = "";
    char sym[64] = "";
    char uuid[64] = "";
    app_emulator *fresh = nullptr;
    {
        std::lock_guard<std::mutex> lock(_bridgeMutex);
        if (!_instances[master]) {
            fresh = SpawnCubeInstance((int)master, model, sym, uuid);
        }
    }

    BOOL masterIsFresh = NO;
    {
        std::lock_guard<std::mutex> lock(_bridgeMutex);
        if (fresh && !_instances[master]) {
            _instances[master] = fresh;
            snprintf(_modelNames[master], sizeof(_modelNames[master]), "%s", model);
            snprintf(_instanceIds[master], sizeof(_instanceIds[master]), "%s", sym);
            snprintf(_instanceUuids[master], sizeof(_instanceUuids[master]), "%s", uuid);
            masterIsFresh = YES;
            fresh = nullptr;
        }
        _createdCount = _instances[master] ? 1 : 0;
        // The instance that owned the audio device was destroyed above (or the
        // master was freshly spawned) - hand the device to the new master
        if (_instances[master] && (masterIsFresh || master != previousTarget)) {
            app_audio_open_device(_instances[master]);
        }
        [self updateActiveAudioSelection];
        NSLog(@"UNGCubeBridge: single-sync ON - destroyed %d instance(s); master face %ld replicates to all 6 faces", destroyed, (long)master);
    }
    if (fresh) app_destroy(fresh);   // unreachable: serial topology queue
    dispatch_async(dispatch_get_main_queue(), ^{
        [[NSNotificationCenter defaultCenter] postNotificationName:UNGCubeInstancesChangedNotification object:nil];
    });
    [self verifyWebApiExposureAfterDelay];
}

- (void)respawnDedicatedInstances {
    // Phase 1 - tear down whatever is alive: OFF always starts from a clean
    // slate. Detach under the lock, free with no lock held (see destroyDetached)
    app_emulator *victims[6] = {};
    {
        std::lock_guard<std::mutex> lock(_bridgeMutex);
        for (int i = 0; i < 6; i++) {
            if (!_instances[i]) continue;
            victims[i] = _instances[i];
            _instances[i] = nullptr;
            _instanceUuids[i][0] = '\0';
        }
    }
    [self destroyDetachedInstances:victims];

    // Phase 2 - spawn the fresh set with no bridge lock held
    app_emulator *fresh[6] = {};
    char models[6][64];
    char syms[6][64];
    char uuids[6][64];
    int created = 0;
    for (int i = 0; i < 6; i++) {
        fresh[i] = SpawnCubeInstance(i, models[i], syms[i], uuids[i]);
        if (fresh[i]) created++;
    }

    // Phase 3 - attach + bookkeeping under the lock
    {
        std::lock_guard<std::mutex> lock(_bridgeMutex);
        for (int i = 0; i < 6; i++) {
            if (!fresh[i]) continue;
            _instances[i] = fresh[i];
            snprintf(_modelNames[i], sizeof(_modelNames[i]), "%s", models[i]);
            snprintf(_instanceIds[i], sizeof(_instanceIds[i]), "%s", syms[i]);
            snprintf(_instanceUuids[i], sizeof(_instanceUuids[i]), "%s", uuids[i]);
        }
        _createdCount = created;
        _isSingleSyncEnabled = NO;  // flip only after the fresh set is running
        NSInteger targetIdx = _selectedFaceIndex;
        if (targetIdx < 0 || targetIdx >= 6) targetIdx = 0;
        if (_instances[targetIdx]) {
            app_audio_open_device(_instances[targetIdx]);
        }
        [self updateActiveAudioSelection];
        NSLog(@"UNGCubeBridge: single-sync OFF - respawned %d/6 dedicated instances", created);
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        [[NSNotificationCenter defaultCenter] postNotificationName:UNGCubeInstancesChangedNotification object:nil];
    });
    [self verifyWebApiExposureAfterDelay];
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

- (nullable NSString *)instanceUuidForIndex:(NSInteger)index {
    if (index < 0 || index >= 6) return nil;
    std::lock_guard<std::mutex> lock(_bridgeMutex);
    if (_instanceUuids[index][0] == '\0') return nil;
    return [NSString stringWithUTF8String:_instanceUuids[index]];
}

- (void)onFrameRefreshForUuid:(NSString *)uuid {
    const char *uid = [uuid UTF8String];
    if (!uid || uid[0] == '\0') return;

    // Resolve + pin under _bridgeMutex, then copy WITHOUT it: holding this
    // lock across app_copy_frame/app_frame_info deadlocks a concurrent
    // topology switch, because app_destroy drains the MessageCenter worker
    // this very method runs on (worker waits for the lock destroy holds).
    // _frameCopiesInFlight keeps the pinned wrapper alive until the copy lands
    app_emulator *target = nullptr;
    NSInteger slot = -1;
    BOOL replicate = NO;
    {
        std::lock_guard<std::mutex> lock(_bridgeMutex);
        for (int i = 0; i < 6; i++) {
            if (_instanceUuids[i][0] != '\0' && strcmp(_instanceUuids[i], uid) == 0) {
                slot = i;
                break;
            }
        }
        // Stale event: the instance was destroyed after the notification posted
        if (slot < 0 || !_instances[slot]) return;
        target = _instances[slot];
        replicate = _isSingleSyncEnabled;
        _frameCopiesInFlight++;
    }

    // No early returns below: the in-flight pin must always be released.
    // _stageMutex is a leaf lock also taken by copyStagedFrameForIndex
    {
        std::lock_guard<std::mutex> stageLock(_stageMutex);

        // Pull the just-latched presentation snapshot - a complete frame,
        // taken at the frame boundary, never a mid-frame render state
        uint16_t w = _faceStageW[slot];
        uint16_t h = _faceStageH[slot];
        if (app_frame_info(target, &w, &h, nullptr) == APP_OK) {
            const size_t size = (size_t)w * (size_t)h * 4;
            if (size > 0) {
                if (_faceStageSize[slot] < size) {
                    uint8_t *grown = (uint8_t *)realloc(_faceStage[slot], size);
                    if (grown) {
                        _faceStage[slot] = grown;
                        _faceStageSize[slot] = size;
                    }
                }
                if (_faceStageSize[slot] >= size &&
                    app_copy_frame(target, _faceStage[slot], _faceStageSize[slot]) == APP_OK) {
                    _faceStageW[slot] = w;
                    _faceStageH[slot] = h;
                    _faceStageSeq[slot]++;

                    // Single-sync: the master frame is the content of all 6 faces
                    if (replicate) {
                        for (int i = 0; i < 6; i++) {
                            if (i == slot) continue;
                            if (_faceStageSize[i] < size) {
                                uint8_t *grown2 = (uint8_t *)realloc(_faceStage[i], size);
                                if (!grown2) continue;
                                _faceStage[i] = grown2;
                                _faceStageSize[i] = size;
                            }
                            memcpy(_faceStage[i], _faceStage[slot], size);
                            _faceStageW[i] = w;
                            _faceStageH[i] = h;
                            _faceStageSeq[i]++;
                        }
                    }
                }
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(_bridgeMutex);
        _frameCopiesInFlight--;
    }
}

// Frees detached wrappers with NO bridge lock held. EmulatorManager::
// RemoveEmulator drains the MessageCenter queue before freeing a context,
// and that queue's worker is the same thread running onFrameRefreshForUuid:
// destroying while holding _bridgeMutex deadlocks (worker waits for the
// lock, destroy waits for the worker). Waiting for _frameCopiesInFlight to
// reach zero first guarantees no copy is mid-flight when a wrapper is freed
- (void)destroyDetachedInstances:(app_emulator **)victims {
    bool any = false;
    for (int i = 0; i < 6; i++) {
        if (victims[i]) {
            any = true;
            break;
        }
    }
    if (!any) return;

    while (true) {
        {
            std::lock_guard<std::mutex> lock(_bridgeMutex);
            if (_frameCopiesInFlight <= 0) break;
        }
        usleep(100);
    }
    for (int i = 0; i < 6; i++) {
        if (victims[i]) {
            app_destroy(victims[i]);
            victims[i] = nullptr;
        }
    }
}

- (BOOL)copyStagedFrameForIndex:(NSInteger)index toBuffer:(void *)dst capacity:(size_t)capacity width:(uint16_t *)outWidth height:(uint16_t *)outHeight sequence:(uint64_t *)outSequence {
    if (index < 0 || index >= 6 || !dst) return NO;
    std::lock_guard<std::mutex> lock(_stageMutex);
    if (!_faceStage[index] || _faceStageSeq[index] == 0) return NO;
    const size_t size = (size_t)_faceStageW[index] * (size_t)_faceStageH[index] * 4;
    if (size == 0 || capacity < size) return NO;
    memcpy(dst, _faceStage[index], size);
    if (outWidth) *outWidth = _faceStageW[index];
    if (outHeight) *outHeight = _faceStageH[index];
    if (outSequence) *outSequence = _faceStageSeq[index];
    return YES;
}

- (uint64_t)stagedFrameSequenceForIndex:(NSInteger)index {
    if (index < 0 || index >= 6) return 0;
    std::lock_guard<std::mutex> lock(_stageMutex);
    return _faceStageSeq[index];
}

- (void)refreshInstanceList {
    // Called when instances are created or destroyed remotely via WebAPI
    // Ensures wrapper handles continue pointing to valid active core instances
}

- (BOOL)pressKey:(NSString *)keyName onInstance:(NSInteger)index {
    if (index < 0 || index >= 6) return NO;
    std::lock_guard<std::mutex> lock(_bridgeMutex);
    NSInteger targetIdx = _isSingleSyncEnabled ? _singleSyncMasterIndex : index;
    app_emulator* emu = _instances[targetIdx];
    if (!emu) return NO;
    return (app_keyboard_press(emu, [keyName UTF8String]) == APP_OK);
}

- (BOOL)releaseKey:(NSString *)keyName onInstance:(NSInteger)index {
    if (index < 0 || index >= 6) return NO;
    std::lock_guard<std::mutex> lock(_bridgeMutex);
    NSInteger targetIdx = _isSingleSyncEnabled ? _singleSyncMasterIndex : index;
    app_emulator* emu = _instances[targetIdx];
    if (!emu) return NO;
    return (app_keyboard_release(emu, [keyName UTF8String]) == APP_OK);
}

@end
