// Any baud rate on macOS: the IOSSIOSPEED ioctl (see serialcustombaud.h)

#include "platform/serialcustombaud.h"

#include <IOKit/serial/ioss.h>
#include <sys/ioctl.h>

bool SetCustomSerialBaud(int fd, uint32_t baud)
{
    speed_t speed = static_cast<speed_t>(baud);
    return ::ioctl(fd, IOSSIOSPEED, &speed) == 0;
}
