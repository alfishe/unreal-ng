// The Surface Dial's input reports with timestamps and the touch bit decoded: its touch latency (2026-10-19).
// Usage: dialtouch [seconds]. Build: clang -o dialtouch dialtouch.c -framework IOKit -framework CoreFoundation
#include <IOKit/hid/IOHIDManager.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static uint8_t buf[64];
static double T0;
static double Now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void Rep(void*c,IOReturn r,void*s,IOHIDReportType ty,uint32_t id,uint8_t*rep,CFIndex n){
 printf("%8.3f id=%02x len=%ld:",Now()-T0,id,(long)n);for(CFIndex i=0;i<n;i++)printf(" %02x",rep[i]);
 if(n>=4&&rep[0]==1)printf("   push=%d touch=%d steps=%d",rep[1]&1,(rep[1]>>1)&1,(int16_t)(rep[2]|rep[3]<<8));printf("\n");fflush(stdout);}
static CFDictionaryRef M(int page,int usage){CFNumberRef p=CFNumberCreate(0,kCFNumberIntType,&page),u=CFNumberCreate(0,kCFNumberIntType,&usage);
const void*k[]={CFSTR(kIOHIDDeviceUsagePageKey),CFSTR(kIOHIDDeviceUsageKey)};const void*v[]={p,u};
return CFDictionaryCreate(0,k,v,2,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);}
static void Added(void*c,IOReturn r,void*s,IOHIDDeviceRef d){char n[128]="";CFStringRef p=IOHIDDeviceGetProperty(d,CFSTR(kIOHIDProductKey));
if(p)CFStringGetCString(p,n,sizeof n,kCFStringEncodingUTF8);printf("%8.3f added %s\n",Now()-T0,n);fflush(stdout);
if(strstr(n,"Dial"))IOHIDDeviceRegisterInputReportCallback(d,buf,sizeof buf,Rep,0);}
static void Removed(void*c,IOReturn r,void*s,IOHIDDeviceRef d){printf("%8.3f removed\n",Now()-T0);fflush(stdout);}
int main(int argc,char**argv){T0=Now();IOHIDManagerRef m=IOHIDManagerCreate(0,0);IOHIDManagerSetDeviceMatching(m,M(1,14));
IOHIDManagerRegisterDeviceMatchingCallback(m,Added,0);IOHIDManagerRegisterDeviceRemovalCallback(m,Removed,0);
IOHIDManagerScheduleWithRunLoop(m,CFRunLoopGetCurrent(),kCFRunLoopDefaultMode);IOHIDManagerOpen(m,0);
CFRunLoopRunInMode(kCFRunLoopDefaultMode,argc>1?atof(argv[1]):60,false);return 0;}
