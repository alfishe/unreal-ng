#include "macosplatform.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

void MacOSPlatform::disableAutomaticFullScreenMenuItem()
{
    // Documented AppKit user default: when NO, NSApplication does not add
    // "Enter Full Screen" to the View menu automatically
    [[NSUserDefaults standardUserDefaults] setBool:NO forKey:@"NSFullScreenMenuItemEverywhere"];
}

void MacOSPlatform::runAsBackgroundProcess()
{
    // Prohibited = a background-only app: out of the Dock and the app switcher, not watched for responsiveness
    [[NSApplication sharedApplication] setActivationPolicy:NSApplicationActivationPolicyProhibited];
}
