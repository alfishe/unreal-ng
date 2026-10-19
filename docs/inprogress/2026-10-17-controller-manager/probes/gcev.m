// Logs GameController.framework button events (physicalInputProfile, every element) for N seconds, with the
// capabilities dump of gc.m at start and the battery every 10 s. Usage: gcev [seconds]
#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <GameController/GameController.h>
#include <mach/mach_time.h>
static double now(void){ static mach_timebase_info_data_t tb; if(!tb.denom) mach_timebase_info(&tb); return mach_absolute_time()*(double)tb.numer/tb.denom/1e6; }
static void hook(GCController* c){
  NSLog(@"controller vendor=%@ category=%@ ext=%@", c.vendorName, c.productCategory, NSStringFromClass([c.extendedGamepad class]));
  if([c.extendedGamepad isKindOfClass:[GCXboxGamepad class]]){ GCXboxGamepad*x=(GCXboxGamepad*)c.extendedGamepad;
    NSLog(@"paddles p1=%@ p2=%@ p3=%@ p4=%@ share=%@", x.paddleButton1?@"y":@"nil", x.paddleButton2?@"y":@"nil", x.paddleButton3?@"y":@"nil", x.paddleButton4?@"y":@"nil", x.buttonShare?@"y":@"nil");}
  NSLog(@"battery=%@ light=%@ haptics=%@ localities=%@", c.battery?[NSString stringWithFormat:@"%.2f st=%ld",c.battery.batteryLevel,(long)c.battery.batteryState]:@"nil", c.light?@"y":@"nil", c.haptics?@"y":@"nil", c.haptics?[c.haptics.supportedLocalities.allObjects componentsJoinedByString:@","]:@"-");
  GCPhysicalInputProfile*p=c.physicalInputProfile;
  for(NSString*k in [p.buttons.allKeys sortedArrayUsingSelector:@selector(compare:)]){
    GCControllerButtonInput*b=p.buttons[k]; NSLog(@"  btn '%@' sf=%@ local=%@", k, b.sfSymbolsName, b.localizedName);
    NSString* name=[k copy];
    b.pressedChangedHandler=^(GCControllerButtonInput* bb, float v, BOOL pressed){ printf("%10.1f %s %s\n", now(), name.UTF8String, pressed?"down":"up"); fflush(stdout); };
  }
}
int main(int argc,char**argv){ @autoreleasepool{ double secs=argc>1?atof(argv[1]):120;
  [NSApplication sharedApplication]; GCController.shouldMonitorBackgroundEvents=YES;
  [[NSNotificationCenter defaultCenter] addObserverForName:GCControllerDidConnectNotification object:nil queue:nil usingBlock:^(NSNotification*n){ hook(n.object); }];
  NSDate* end=[NSDate dateWithTimeIntervalSinceNow:secs]; double lastBat=0;
  while([end timeIntervalSinceNow]>0){ [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
    if(now()-lastBat>10000){ lastBat=now(); for(GCController*c in GCController.controllers) if(c.battery) { printf("%10.1f battery %.2f state=%ld\n", now(), c.battery.batteryLevel, (long)c.battery.batteryState); fflush(stdout);} } }
 } return 0;}
