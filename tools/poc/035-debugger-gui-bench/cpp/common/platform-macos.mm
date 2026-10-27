// macOS part of the harness: keep the process out of App Nap and timer coalescing while it is measured, so 50 Hz
// pacing is not stretched by the OS (the window is in front during a run; this only removes the residual risk).

#include "harness.h"

#import <Foundation/Foundation.h>

namespace bench
{

void PlatformBeginBenchmark()
{
    static id<NSObject> activity = nil;  // held for the life of the process
    @autoreleasepool
    {
        activity = [[NSProcessInfo processInfo] beginActivityWithOptions:(NSActivityUserInitiated | NSActivityLatencyCritical)
                                                                  reason:@"POC 035 GUI benchmark"];
#if !__has_feature(objc_arc)
        [activity retain];
#endif
    }
}

}  // namespace bench
