// POC 030: Surface Dial (045E:091B) haptic reports over IOHID, every field explicit; reads feature report 1 back.
//   dialhaptic get
//   dialhaptic feature <steps 0-3600> <auto-trigger 1-7> <cutoff 1-10> <retrigger-ms 1-2000> [repeat 0-255]
//   dialhaptic play <repeat 0-255> <waveform 1-7> <retrigger-ms 1-2000>
// Waveform ordinals (feature report 2): 1 none, 2 stop, 3 click, 4 buzz continuous, 5 buzz continuous (fw 29d51170).
// Build: clang -O1 -o dialhaptic dialhaptic.c -framework IOKit -framework CoreFoundation
#include <IOKit/hid/IOHIDManager.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void dump(IOHIDDeviceRef d) {
  uint8_t g[16] = {0}; CFIndex len = sizeof g;
  IOReturn r = IOHIDDeviceGetReport(d, kIOHIDReportTypeFeature, 1, g, &len);
  printf("feature 1 read -> 0x%x:", r); for (CFIndex i = 0; i < len; i++) printf(" %02x", g[i]); printf("\n");
}
static int arg(int argc, char** argv, int i, int def, int lo, int hi) {
  int v = argc > i ? atoi(argv[i]) : def;
  if (v < lo || v > hi) { fprintf(stderr, "argument %d = %d outside %d..%d\n", i, v, lo, hi); exit(2); }
  return v;
}

int main(int argc, char** argv) {
  IOHIDManagerRef m = IOHIDManagerCreate(kCFAllocatorDefault, 0);
  int vid = 0x045E, pid = 0x091B;
  CFNumberRef v = CFNumberCreate(0, kCFNumberIntType, &vid), p = CFNumberCreate(0, kCFNumberIntType, &pid);
  const void* k[] = {CFSTR(kIOHIDVendorIDKey), CFSTR(kIOHIDProductIDKey)}; const void* vals[] = {v, p};
  IOHIDManagerSetDeviceMatching(m, CFDictionaryCreate(0, k, vals, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
  IOHIDManagerOpen(m, kIOHIDOptionsTypeNone);
  CFSetRef set = IOHIDManagerCopyDevices(m);
  if (!set || !CFSetGetCount(set)) { printf("no dial (asleep? touch it)\n"); return 1; }
  CFIndex n = CFSetGetCount(set);
  const void** all = calloc((size_t)n, sizeof *all);
  CFSetGetValues(set, all);
  IOHIDDeviceRef d = (IOHIDDeviceRef)all[0];   // more than one Dial: the first one
  if (n > 1) printf("%ld dials, using the first\n", (long)n);
  IOHIDDeviceOpen(d, kIOHIDOptionsTypeNone);
  const char* cmd = argc > 1 ? argv[1] : "get";
  IOReturn r = kIOReturnSuccess;
  if (!strcmp(cmd, "feature")) {
    int steps = arg(argc, argv, 2, 36, 0, 3600), at = arg(argc, argv, 3, 3, 1, 7), cut = arg(argc, argv, 4, 5, 1, 10),
        rt = arg(argc, argv, 5, 80, 1, 2000), rep = arg(argc, argv, 6, 0, 0, 255);
    uint8_t b[8] = {1, (uint8_t)steps, (uint8_t)(steps >> 8), (uint8_t)rep, (uint8_t)at, (uint8_t)cut, (uint8_t)rt, (uint8_t)(rt >> 8)};
    r = IOHIDDeviceSetReport(d, kIOHIDReportTypeFeature, 1, b, sizeof b);
    printf("feature 1 write:"); for (size_t i = 0; i < sizeof b; i++) printf(" %02x", b[i]); printf(" -> 0x%x\n", r);
  } else if (!strcmp(cmd, "play")) {
    int rep = arg(argc, argv, 2, 0, 0, 255), wf = arg(argc, argv, 3, 3, 1, 7), rt = arg(argc, argv, 4, 80, 1, 2000);
    uint8_t b[5] = {1, (uint8_t)rep, (uint8_t)wf, (uint8_t)rt, (uint8_t)(rt >> 8)};
    r = IOHIDDeviceSetReport(d, kIOHIDReportTypeOutput, 1, b, sizeof b);
    printf("output 1 write:"); for (size_t i = 0; i < sizeof b; i++) printf(" %02x", b[i]); printf(" -> 0x%x\n", r);
  }
  dump(d);
  return r != kIOReturnSuccess;
}
