#include "platform/macos/gccompanion.h"

#import <CoreHaptics/CoreHaptics.h>
#import <Foundation/Foundation.h>
#import <GameController/GameController.h>

#include <chrono>
#include <map>
#include <mutex>
#include <vector>

#if !__has_feature(objc_arc)
#error "gccompanion.mm is built with -fobjc-arc"
#endif

namespace controllers
{

namespace
{
uint64_t SteadyNowMs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

/// Which levels a motor plays: one engine per locality the pad has
enum class Source : uint8_t
{
    Low,           ///< the left handle
    High,          ///< the right handle
    Handles,       ///< both handles in one engine (no separate localities): the louder of the two
    LeftTrigger,
    RightTrigger,
};

float LevelFor(Source source, const RumbleLevels& levels)
{
    switch (source)
    {
        case Source::Low: return levels.low;
        case Source::High: return levels.high;
        case Source::Handles: return levels.low > levels.high ? levels.low : levels.high;
        case Source::LeftTrigger: return levels.leftTrigger;
        case Source::RightTrigger: return levels.rightTrigger;
    }
    return 0;
}

/// One CoreHaptics engine on one locality, a continuous event of full intensity scaled by the dynamic intensity
struct Motor
{
    Source source = Source::Low;
    GCHapticsLocality locality = nil;
    CHHapticEngine* engine = nil;
    id<CHHapticPatternPlayer> player = nil;
    bool playing = false;
    bool failed = false;  ///< the engine did not start: not tried again for this connection
    float level = 0;
};

struct Entry
{
    GCController* controller = nil;
    std::string key;
    std::vector<Motor> motors;
};

void Release(Motor& motor)
{
    if (motor.player != nil && motor.playing)
        [motor.player stopAtTime:0 error:nil];
    motor.player = nil;
    if (motor.engine != nil)
        [motor.engine stopWithCompletionHandler:nil];
    motor.engine = nil;
    motor.playing = false;
}

/// The engine and its player, on first use (an engine holds audio resources while it runs)
bool Prepare(GCController* controller, Motor& motor)
{
    if (motor.failed)
        return false;
    if (motor.player != nil)
        return true;
    NSError* error = nil;
    motor.engine = [controller.haptics createEngineWithLocality:motor.locality];
    if (motor.engine == nil || ![motor.engine startAndReturnError:&error])
    {
        motor.engine = nil;
        motor.failed = true;
        return false;
    }
    motor.engine.playsHapticsOnly = YES;
    CHHapticEventParameter* intensity =
        [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity value:1.0f];
    CHHapticEvent* event = [[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous
                                                         parameters:@[ intensity ]
                                                       relativeTime:0
                                                           duration:GCHapticDurationInfinite];
    CHHapticPattern* pattern = [[CHHapticPattern alloc] initWithEvents:@[ event ] parameters:@[] error:&error];
    if (pattern != nil)
        motor.player = [motor.engine createPlayerWithPattern:pattern error:&error];
    if (motor.player == nil)
    {
        [motor.engine stopWithCompletionHandler:nil];
        motor.engine = nil;
        motor.failed = true;
        return false;
    }
    return true;
}

void Apply(GCController* controller, Motor& motor, float level)
{
    level = level < 0 ? 0 : level > 1 ? 1 : level;
    if (level == motor.level && (motor.playing || level == 0))
        return;
    motor.level = level;
    if (level == 0)
    {
        if (motor.playing)
            [motor.player stopAtTime:0 error:nil];
        motor.playing = false;
        return;
    }
    if (!Prepare(controller, motor))
        return;
    NSError* error = nil;
    CHHapticDynamicParameter* parameter =
        [[CHHapticDynamicParameter alloc] initWithParameterID:CHHapticDynamicParameterIDHapticIntensityControl
                                                        value:level
                                                 relativeTime:0];
    if (!motor.playing)
        motor.playing = [motor.player startAtTime:0 error:&error];
    if (motor.playing && ![motor.player sendParameters:@[ parameter ] atTime:0 error:&error])
    {
        // The engine was reset under us: next time from scratch
        Release(motor);
        motor.level = 0;
    }
}

/// The motors a pad's localities give: separate handles when it has them, else one engine for both
std::vector<Motor> MotorsOf(GCController* controller)
{
    std::vector<Motor> motors;
    GCDeviceHaptics* haptics = controller.haptics;
    if (haptics == nil)
        return motors;
    NSSet<GCHapticsLocality>* localities = haptics.supportedLocalities;
    auto add = [&motors](Source source, GCHapticsLocality locality) {
        Motor motor;
        motor.source = source;
        motor.locality = locality;
        motors.push_back(motor);
    };
    if ([localities containsObject:GCHapticsLocalityLeftHandle] && [localities containsObject:GCHapticsLocalityRightHandle])
    {
        add(Source::Low, GCHapticsLocalityLeftHandle);
        add(Source::High, GCHapticsLocalityRightHandle);
    }
    else if ([localities containsObject:GCHapticsLocalityHandles])
        add(Source::Handles, GCHapticsLocalityHandles);
    else if ([localities containsObject:GCHapticsLocalityDefault])
        add(Source::Handles, GCHapticsLocalityDefault);
    if ([localities containsObject:GCHapticsLocalityLeftTrigger])
        add(Source::LeftTrigger, GCHapticsLocalityLeftTrigger);
    if ([localities containsObject:GCHapticsLocalityRightTrigger])
        add(Source::RightTrigger, GCHapticsLocalityRightTrigger);
    return motors;
}

uint8_t ActuatorsOf(const std::vector<Motor>& motors)
{
    uint8_t bits = 0;
    for (const Motor& motor : motors)
        switch (motor.source)
        {
            case Source::Low: bits |= kRumbleLow; break;
            case Source::High: bits |= kRumbleHigh; break;
            case Source::Handles: bits |= kRumbleLow | kRumbleHigh; break;
            case Source::LeftTrigger: bits |= kRumbleLeftTrigger; break;
            case Source::RightTrigger: bits |= kRumbleRightTrigger; break;
        }
    return bits;
}

/// The buttons a press is matched by (EndpointMerge): the ones every pad has on both endpoints
NSArray<GCControllerButtonInput*>* MatchButtons(GCExtendedGamepad* gamepad)
{
    NSMutableArray<GCControllerButtonInput*>* buttons = [NSMutableArray array];
    for (GCControllerButtonInput* button : {gamepad.buttonA, gamepad.buttonB, gamepad.buttonX, gamepad.buttonY,
                                            gamepad.leftShoulder, gamepad.rightShoulder})
        if (button != nil)
            [buttons addObject:button];
    return buttons;
}

std::string StringOf(NSString* text)
{
    return text != nil ? std::string([text UTF8String]) : std::string();
}
}  // namespace

struct GcCompanion::State
{
    dispatch_queue_t queue = nil;
    NSMutableArray* observers = nil;
    Callbacks callbacks;
    std::map<void*, Entry> pads;  ///< on the queue only; keyed by the controller
    std::mutex mutex;
    std::map<std::string, uint8_t> actuators;  ///< under mutex: the pads with haptics, for SetRumble without waiting
    int next = 1;
    bool running = false;  ///< on the queue only
};

GcCompanion::GcCompanion() = default;

GcCompanion::~GcCompanion()
{
    Stop();
}

bool GcCompanion::Start(Callbacks callbacks, std::string& reason)
{
    if (_state)
        return true;
    if (NSClassFromString(@"GCController") == nil)
    {
        reason = "GameController.framework is not available";
        return false;
    }
    auto state = std::make_shared<State>();
    state->queue = dispatch_queue_create("controllers-gc", DISPATCH_QUEUE_SERIAL);
    state->observers = [NSMutableArray array];
    state->callbacks = std::move(callbacks);

    // The work on the queue: a block keeps the state alive while it waits
    auto connect = [](const std::shared_ptr<State>& s, GCController* controller) {
        if (!s->running || controller == nil || controller.extendedGamepad == nil)
            return;  // a remote or a micro pad: no buttons to match, no handles
        void* handle = (__bridge void*)controller;
        if (s->pads.count(handle) != 0)
            return;
        Entry& entry = s->pads[handle];
        entry.controller = controller;
        entry.key = "gc#" + std::to_string(s->next++);
        entry.motors = MotorsOf(controller);
        Pad pad;
        pad.key = entry.key;
        pad.vendorName = StringOf(controller.vendorName);
        pad.actuators = ActuatorsOf(entry.motors);
        // The presses reach the queue directly, not through the main thread: their times stay close to IOHID's
        controller.handlerQueue = s->queue;
        GCExtendedGamepad* gamepad = controller.extendedGamepad;
        const std::string key = entry.key;
        std::weak_ptr<State> weak = s;
        GCControllerButtonValueChangedHandler handler = ^(GCControllerButtonInput*, float, BOOL pressed) {
          const std::shared_ptr<State> live = weak.lock();
          if (pressed && live && live->running && live->callbacks.pressed)
              live->callbacks.pressed(key, SteadyNowMs());
        };
        for (GCControllerButtonInput* button in MatchButtons(gamepad))
            button.pressedChangedHandler = handler;
        if (pad.actuators != 0)
        {
            std::lock_guard<std::mutex> lock(s->mutex);
            s->actuators[pad.key] = pad.actuators;
        }
        if (s->callbacks.added)
            s->callbacks.added(pad);
    };
    auto disconnect = [](const std::shared_ptr<State>& s, GCController* controller) {
        const auto found = s->pads.find((__bridge void*)controller);
        if (!s->running || found == s->pads.end())
            return;
        for (Motor& motor : found->second.motors)
            Release(motor);
        const std::string key = found->second.key;
        s->pads.erase(found);
        {
            std::lock_guard<std::mutex> lock(s->mutex);
            s->actuators.erase(key);
        }
        if (s->callbacks.removed)
            s->callbacks.removed(key);
    };

    dispatch_sync(state->queue, ^{
      state->running = true;
    });
    std::weak_ptr<State> weak = state;
    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
    [state->observers addObject:[center addObserverForName:GCControllerDidConnectNotification
                                                    object:nil
                                                     queue:nil
                                                usingBlock:^(NSNotification* note) {
                                                  GCController* controller = note.object;
                                                  if (const std::shared_ptr<State> s = weak.lock())
                                                      dispatch_async(s->queue, ^{
                                                        connect(s, controller);
                                                      });
                                                }]];
    [state->observers addObject:[center addObserverForName:GCControllerDidDisconnectNotification
                                                    object:nil
                                                     queue:nil
                                                usingBlock:^(NSNotification* note) {
                                                  GCController* controller = note.object;
                                                  if (const std::shared_ptr<State> s = weak.lock())
                                                      dispatch_async(s->queue, ^{
                                                        disconnect(s, controller);
                                                      });
                                                }]];
    // The pads already connected
    dispatch_async(state->queue, ^{
      for (GCController* controller in GCController.controllers)
          connect(state, controller);
    });
    _state = state;
    return true;
}

void GcCompanion::Stop()
{
    if (!_state)
        return;
    std::shared_ptr<State> state = std::move(_state);
    for (id observer in state->observers)
        [[NSNotificationCenter defaultCenter] removeObserver:observer];
    [state->observers removeAllObjects];
    dispatch_sync(state->queue, ^{
      state->running = false;
      for (auto& [handle, entry] : state->pads)
      {
          for (Motor& motor : entry.motors)
              Release(motor);
          for (GCControllerButtonInput* button in MatchButtons(entry.controller.extendedGamepad))
              button.pressedChangedHandler = nil;
      }
      state->pads.clear();
      state->callbacks = {};
      std::lock_guard<std::mutex> lock(state->mutex);
      state->actuators.clear();
    });
}

bool GcCompanion::SetRumble(const std::string& key, const RumbleLevels& levels)
{
    std::shared_ptr<State> state = _state;
    if (!state)
        return false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->actuators.count(key) == 0)
            return false;
    }
    const RumbleLevels copy = levels;
    dispatch_async(state->queue, ^{
      for (auto& [handle, entry] : state->pads)
          if (entry.key == key)
              for (Motor& motor : entry.motors)
                  Apply(entry.controller, motor, LevelFor(motor.source, copy));
    });
    return true;
}

}  // namespace controllers
