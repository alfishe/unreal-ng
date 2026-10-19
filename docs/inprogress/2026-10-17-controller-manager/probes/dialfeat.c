// Read-only: polls the Surface Dial's feature reports (GetReport) every 200 ms and prints the changes beside its input
// reports. Usage: dialfeat [seconds]. Build: clang -o dialfeat dialfeat.c -framework IOKit -framework CoreFoundation
#include <IOKit/hid/IOHIDManager.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static uint8_t buf[80];
static double T0;
static IOHIDDeviceRef dial;
static const int ids[] = {0x01, 0x02, 0x2A, 0x2B, 0x2D, 0x2E, 0x2F};
static uint8_t last[7][128];
static CFIndex lastLen[7];
static double Now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void Rep(void*c,IOReturn r,void*s,IOHIDReportType ty,uint32_t id,uint8_t*rep,CFIndex n){
 printf("%8.3f IN  id=%02x:",Now()-T0,id);for(CFIndex i=0;i<n&&i<24;i++)printf(" %02x",rep[i]);
 if(id==1)printf("   touch=%d",(rep[1]>>1)&1);printf("\n");fflush(stdout);}
static void Poll(CFRunLoopTimerRef t,void*c){if(!dial)return;
 for(int k=0;k<7;k++){uint8_t r[128];CFIndex n=sizeof r;r[0]=ids[k];
  IOReturn x=IOHIDDeviceGetReport(dial,kIOHIDReportTypeFeature,ids[k],r,&n);
  if(x!=kIOReturnSuccess){if(lastLen[k]!=-1){printf("%8.3f FEAT id=%02x error %x\n",Now()-T0,ids[k],x);lastLen[k]=-1;}continue;}
  if(n==lastLen[k]&&!memcmp(r,last[k],n))continue;
  printf("%8.3f FEAT id=%02x len=%ld:",Now()-T0,ids[k],(long)n);for(CFIndex i=0;i<n;i++)printf(" %02x",r[i]);printf("\n");fflush(stdout);
  memcpy(last[k],r,n);lastLen[k]=n;}}
static CFDictionaryRef M(int page,int usage){CFNumberRef p=CFNumberCreate(0,kCFNumberIntType,&page),u=CFNumberCreate(0,kCFNumberIntType,&usage);
const void*k[]={CFSTR(kIOHIDDeviceUsagePageKey),CFSTR(kIOHIDDeviceUsageKey)};const void*v[]={p,u};
return CFDictionaryCreate(0,k,v,2,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);}
static void Added(void*c,IOReturn r,void*s,IOHIDDeviceRef d){char n[128]="";CFStringRef p=IOHIDDeviceGetProperty(d,CFSTR(kIOHIDProductKey));
if(p)CFStringGetCString(p,n,sizeof n,kCFStringEncodingUTF8);printf("%8.3f added %s\n",Now()-T0,n);fflush(stdout);
if(strstr(n,"Dial")){dial=d;IOHIDDeviceRegisterInputReportCallback(d,buf,sizeof buf,Rep,0);}}
int main(int argc,char**argv){T0=Now();IOHIDManagerRef m=IOHIDManagerCreate(0,0);IOHIDManagerSetDeviceMatching(m,M(1,14));
IOHIDManagerRegisterDeviceMatchingCallback(m,Added,0);IOHIDManagerScheduleWithRunLoop(m,CFRunLoopGetCurrent(),kCFRunLoopDefaultMode);
IOHIDManagerOpen(m,0);CFRunLoopTimerRef t=CFRunLoopTimerCreate(0,CFAbsoluteTimeGetCurrent()+0.3,0.2,0,0,Poll,0);
CFRunLoopAddTimer(CFRunLoopGetCurrent(),t,kCFRunLoopDefaultMode);
CFRunLoopRunInMode(kCFRunLoopDefaultMode,argc>1?atof(argv[1]):45,false);return 0;}
