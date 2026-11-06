// POC 030, A5: does the Surface Dial queue output reports? burst <n> <waveform> [stop-after-ms]: n plays back to back,
// each write timed; then a stop (waveform 2). Build: clang -O1 -o dialqueue dialqueue.c -framework IOKit -framework CoreFoundation
#include <IOKit/hid/IOHIDManager.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
static double now(void) { static mach_timebase_info_data_t tb; if (!tb.denom) mach_timebase_info(&tb); return mach_absolute_time() * (double)tb.numer / tb.denom / 1e6; }
static IOReturn play(IOHIDDeviceRef d, int wave) { uint8_t r[5] = {1, 1, (uint8_t)wave, 80, 0}; return IOHIDDeviceSetReport(d, kIOHIDReportTypeOutput, 1, r, 5); }
int main(int argc, char** argv) {
  int n = argc > 1 ? atoi(argv[1]) : 1, wave = argc > 2 ? atoi(argv[2]) : 4, stopMs = argc > 3 ? atoi(argv[3]) : -1;
  IOHIDManagerRef m = IOHIDManagerCreate(kCFAllocatorDefault, 0);
  int vid = 0x045E, pid = 0x091B;
  CFNumberRef v = CFNumberCreate(0, kCFNumberIntType, &vid), p = CFNumberCreate(0, kCFNumberIntType, &pid);
  const void* k[] = {CFSTR(kIOHIDVendorIDKey), CFSTR(kIOHIDProductIDKey)}; const void* vals[] = {v, p};
  IOHIDManagerSetDeviceMatching(m, CFDictionaryCreate(0, k, vals, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
  IOHIDManagerOpen(m, kIOHIDOptionsTypeNone);
  CFSetRef set = IOHIDManagerCopyDevices(m);
  if (!set || CFSetGetCount(set) == 0) { fprintf(stderr, "no dial\n"); return 1; }
  IOHIDDeviceRef d; CFSetGetValues(set, (const void**)&d);
  double t0 = now();
  for (int i = 0; i < n; i++) { double a = now(); IOReturn r = play(d, wave); printf("play %d at %.1f ms took %.1f ms -> 0x%x\n", i, a - t0, now() - a, r); }
  if (stopMs >= 0) { usleep(stopMs * 1000); double a = now(); IOReturn r = play(d, 2); printf("stop at %.1f ms took %.1f -> 0x%x\n", a - t0, now() - a, r); }
  return 0;
}
