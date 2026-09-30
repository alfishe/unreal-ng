// Any baud rate on Linux: termios2 with BOTHER (see serialcustombaud.h).
// <asm/termbits.h> clashes with <termios.h>, so this file uses only the former

#include "platform/serialcustombaud.h"

#include <asm/ioctls.h>
#include <asm/termbits.h>
#include <sys/ioctl.h>

bool SetCustomSerialBaud(int fd, uint32_t baud)
{
    struct termios2 tio {};
    if (::ioctl(fd, TCGETS2, &tio) != 0)
        return false;
    tio.c_cflag &= ~static_cast<tcflag_t>(CBAUD);
    tio.c_cflag |= BOTHER;
    tio.c_ispeed = baud;
    tio.c_ospeed = baud;
    return ::ioctl(fd, TCSETS2, &tio) == 0;
}
