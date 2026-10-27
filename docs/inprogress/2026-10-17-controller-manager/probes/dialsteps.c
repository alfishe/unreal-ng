// The detents path of the IOHID backend, live: reads the Surface Dial's feature report 1 as found, writes the reports
// SurfaceDial::FeatureReport builds for a few StepsOut (the found cutoff and retrigger kept), reads each back, then
// writes the found report back and reads it once more. Prints PASS or FAIL per step; exit 0 when all pass.
// The Dial sleeps: touch or turn it when the probe waits. Usage: dialsteps [wait-seconds]
// Build: clang -o dialsteps dialsteps.c -framework IOKit -framework CoreFoundation
#include <IOKit/hid/IOHIDManager.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static IOHIDDeviceRef dial;

static void Added(void* c, IOReturn r, void* s, IOHIDDeviceRef d) { (void)c; (void)r; (void)s; if (!dial) dial = d; }

static int Read(uint8_t out[8]) {
  uint8_t r[16] = {1}; CFIndex n = sizeof r;
  IOReturn x = IOHIDDeviceGetReport(dial, kIOHIDReportTypeFeature, 1, r, &n);
  if (x != kIOReturnSuccess || n < 8) { printf("  read: error 0x%x len %ld\n", x, (long)n); return 0; }
  memcpy(out, r, 8); return 1;
}
static void Print(const char* what, const uint8_t b[8]) {
  printf("  %-6s", what); for (int i = 0; i < 8; i++) printf(" %02x", b[i]); printf("\n");
}
// The bytes of SurfaceDial::FeatureReport(steps, base): steps, repeat 0, auto trigger 3 (clicks) or 1, base cutoff and
// retrigger
static void Steps(uint8_t out[8], int steps, int clicks, const uint8_t base[8]) {
  out[0] = 1; out[1] = steps & 0xFF; out[2] = steps >> 8; out[3] = 0; out[4] = clicks ? 3 : 1;
  out[5] = base[5] ? base[5] : 5; out[6] = base[6]; out[7] = base[7];
  if (!out[6] && !out[7]) out[6] = 80;
}
static int WriteAndCheck(const char* name, const uint8_t want[8]) {
  printf("%s\n", name); Print("write", want);
  IOReturn x = IOHIDDeviceSetReport(dial, kIOHIDReportTypeFeature, 1, want, 8);
  if (x != kIOReturnSuccess) { printf("  write: error 0x%x\nFAIL\n", x); return 0; }
  uint8_t back[8]; if (!Read(back)) { printf("FAIL\n"); return 0; }
  Print("read", back);
  int ok = !memcmp(back, want, 8); printf("%s\n", ok ? "PASS" : "FAIL"); return ok;
}

int main(int argc, char** argv) {
  double wait = argc > 1 ? atof(argv[1]) : 30;
  IOHIDManagerRef m = IOHIDManagerCreate(kCFAllocatorDefault, 0);
  int vid = 0x045E, pid = 0x091B;
  CFNumberRef v = CFNumberCreate(0, kCFNumberIntType, &vid), p = CFNumberCreate(0, kCFNumberIntType, &pid);
  const void* k[] = {CFSTR(kIOHIDVendorIDKey), CFSTR(kIOHIDProductIDKey)}; const void* vals[] = {v, p};
  CFDictionaryRef match = CFDictionaryCreate(0, k, vals, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  IOHIDManagerSetDeviceMatching(m, match);
  IOHIDManagerRegisterDeviceMatchingCallback(m, Added, 0);
  IOHIDManagerScheduleWithRunLoop(m, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
  IOHIDManagerOpen(m, kIOHIDOptionsTypeNone);
  printf("waiting up to %.0f s for the Surface Dial 045E:091B (touch or turn it)\n", wait); fflush(stdout);
  for (double t = 0; !dial && t < wait; t += 0.1) CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);
  if (!dial) { printf("no Surface Dial connected\n"); return 2; }

  uint8_t found[8]; printf("found\n"); if (!Read(found)) return 1; Print("read", found);
  uint8_t want[8]; int ok = 1;
  Steps(want, 36, 1, found); ok &= WriteAndCheck("steps 36, clicks on", want);
  Steps(want, 72, 1, found); ok &= WriteAndCheck("steps 72, clicks on", want);
  Steps(want, 36, 0, found); ok &= WriteAndCheck("steps 36, clicks off (free wheel)", want);
  ok &= WriteAndCheck("restore the found report", found);
  printf("%s\n", ok ? "ALL PASS" : "SOME FAILED");
  return ok ? 0 : 1;
}
