#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <GameController/GameController.h>
static void dump(void){
  for (GCController* c in GCController.controllers){
    NSLog(@"vendor=%@ category=%@ productCategory=%@ class=%@ ext=%@", c.vendorName, c.productCategory, c.productCategory, NSStringFromClass([c.extendedGamepad class]), c.extendedGamepad?@"y":@"n");
    if([c.extendedGamepad isKindOfClass:[GCXboxGamepad class]]){ GCXboxGamepad*x=(GCXboxGamepad*)c.extendedGamepad;
      NSLog(@"paddles p1=%@ p2=%@ p3=%@ p4=%@ share=%@", x.paddleButton1?@"y":@"nil", x.paddleButton2?@"y":@"nil", x.paddleButton3?@"y":@"nil", x.paddleButton4?@"y":@"nil", x.buttonShare?@"y":@"nil");}
    NSLog(@"battery=%@ light=%@ haptics=%@ localities=%@", c.battery?[NSString stringWithFormat:@"%.2f st=%ld",c.battery.batteryLevel,(long)c.battery.batteryState]:@"nil", c.light?@"y":@"nil", c.haptics?@"y":@"nil", c.haptics?[c.haptics.supportedLocalities.allObjects componentsJoinedByString:@","]:@"-");
    GCPhysicalInputProfile*p=c.physicalInputProfile;
    for(NSString*k in [p.buttons.allKeys sortedArrayUsingSelector:@selector(compare:)]){ GCControllerButtonInput*b=p.buttons[k]; NSLog(@"  btn '%@' sf=%@ local=%@", k, b.sfSymbolsName, b.localizedName);}
    for(NSString*k in p.axes.allKeys) NSLog(@"  axis '%@'", k);
    for(NSString*k in p.dpads.allKeys) NSLog(@"  dpad '%@' sf=%@", k, p.dpads[k].sfSymbolsName);
  }}
int main(){ @autoreleasepool{ [NSApplication sharedApplication]; GCController.shouldMonitorBackgroundEvents=YES;
  [[NSNotificationCenter defaultCenter] addObserverForName:GCControllerDidConnectNotification object:nil queue:nil usingBlock:^(NSNotification*n){ NSLog(@"connect"); }];
  NSDate* end=[NSDate dateWithTimeIntervalSinceNow:3]; while([end timeIntervalSinceNow]>0)[[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
  dump(); } return 0;}
