#include "macosplatform.h"

#import <Foundation/Foundation.h>

void MacOSPlatform::disableAutomaticFullScreenMenuItem()
{
    // Documented AppKit user default: when NO, NSApplication does not add
    // "Enter Full Screen" to the View menu automatically
    [[NSUserDefaults standardUserDefaults] setBool:NO forKey:@"NSFullScreenMenuItemEverywhere"];
}
