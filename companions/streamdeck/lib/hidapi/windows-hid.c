/* hidapi's Windows backend, compiled through this file (hid.c includes hidapi_descriptor_reconstruct.c itself).
   The root CMakeLists.txt defines WIN32_LEAN_AND_MEAN for the whole tree, and hidapi needs the full windows.h
   (PROPERTYKEY, DEFINE_PROPERTYKEY, NTSTATUS come through the headers the lean variant leaves out). An -U on the
   command line would make cl warn (D9025). */
#undef WIN32_LEAN_AND_MEAN
#include "../../3rdparty/hidapi/windows/hid.c"
