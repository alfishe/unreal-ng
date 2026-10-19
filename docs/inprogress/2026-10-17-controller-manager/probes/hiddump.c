// Dumps every input report of Microsoft 045E:<pid> HID endpoints (default 0B00 USB; 0B22 = BLE), and each report descriptor.
// Usage: hiddump [seconds] [pid-hex]
#include <IOKit/hid/IOHIDManager.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mach/mach_time.h>
static uint8_t last[8][128]; static CFIndex lastLen[8]; static int ndev=0; static IOHIDDeviceRef devs[8];
static double now(void){ static mach_timebase_info_data_t tb; if(!tb.denom) mach_timebase_info(&tb); return mach_absolute_time()*(double)tb.numer/tb.denom/1e6; }
static int idx(IOHIDDeviceRef d){ for(int i=0;i<ndev;i++) if(devs[i]==d) return i; return -1; }
static void cb(void* ctx, IOReturn r, void* sender, IOHIDReportType t, uint32_t id, uint8_t* rep, CFIndex len){
  int i=(int)(intptr_t)ctx; if(len==lastLen[i] && !memcmp(last[i],rep,len)) return;
  memcpy(last[i],rep,len); lastLen[i]=len;
  printf("%10.1f dev%d id=%02x len=%2ld :", now(), i, id, (long)len); for(CFIndex k=0;k<len;k++) printf(" %02x",rep[k]); printf("\n"); fflush(stdout);
}
static void added(void* c, IOReturn r, void* s, IOHIDDeviceRef d){
  if(ndev>=8||idx(d)>=0) return; int i=ndev; devs[ndev++]=d;
  CFStringRef p=IOHIDDeviceGetProperty(d,CFSTR(kIOHIDProductKey)); char name[128]="?"; if(p) CFStringGetCString(p,name,128,kCFStringEncodingUTF8);
  CFNumberRef up=IOHIDDeviceGetProperty(d,CFSTR(kIOHIDPrimaryUsagePageKey)), u=IOHIDDeviceGetProperty(d,CFSTR(kIOHIDPrimaryUsageKey)), mx=IOHIDDeviceGetProperty(d,CFSTR(kIOHIDMaxInputReportSizeKey));
  int vup=0,vu=0,vmx=0; if(up)CFNumberGetValue(up,kCFNumberIntType,&vup); if(u)CFNumberGetValue(u,kCFNumberIntType,&vu); if(mx)CFNumberGetValue(mx,kCFNumberIntType,&vmx);
  printf("added dev%d '%s' usagePage=%d usage=%d maxIn=%d\n",i,name,vup,vu,vmx);
  CFDataRef rd=IOHIDDeviceGetProperty(d,CFSTR(kIOHIDReportDescriptorKey));
  if(rd){ const uint8_t* b=CFDataGetBytePtr(rd); CFIndex n=CFDataGetLength(rd); printf("descriptor dev%d len=%ld :",i,(long)n); for(CFIndex k=0;k<n;k++) printf(" %02x",b[k]); printf("\n"); }
  fflush(stdout);
  uint8_t* buf=calloc(1,vmx>0?vmx:64); IOHIDDeviceRegisterInputReportCallback(d,buf,vmx>0?vmx:64,cb,(void*)(intptr_t)i);
}
int main(int argc,char**argv){ double secs=argc>1?atof(argv[1]):120;
  IOHIDManagerRef m=IOHIDManagerCreate(kCFAllocatorDefault,0);
  int vid=0x045E,pid=argc>2?(int)strtol(argv[2],0,16):0x0B00; CFNumberRef v=CFNumberCreate(0,kCFNumberIntType,&vid), p=CFNumberCreate(0,kCFNumberIntType,&pid);
  const void* k[]={CFSTR(kIOHIDVendorIDKey),CFSTR(kIOHIDProductIDKey)}; const void* vals[]={v,p};
  CFDictionaryRef md=CFDictionaryCreate(0,k,vals,2,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
  IOHIDManagerSetDeviceMatching(m,md); IOHIDManagerRegisterDeviceMatchingCallback(m,added,0);
  IOHIDManagerScheduleWithRunLoop(m,CFRunLoopGetCurrent(),kCFRunLoopDefaultMode);
  IOReturn r=IOHIDManagerOpen(m,kIOHIDOptionsTypeNone); printf("open=0x%x\n",r); fflush(stdout);
  CFRunLoopRunInMode(kCFRunLoopDefaultMode,secs,false); return 0; }
