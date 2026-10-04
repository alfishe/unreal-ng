// The host adapter's MAC on macOS: the AF_LINK address libpcap lists (see hostadaptermac.h)

#include "platform/hostadaptermac.h"

#include <cstring>
#include <net/if_dl.h>
#include <sys/socket.h>

bool LinkAddressOf(const sockaddr* address, uint8_t mac[6])
{
    if (!address || address->sa_family != AF_LINK)
        return false;
    const auto* link = reinterpret_cast<const sockaddr_dl*>(address);
    if (link->sdl_alen != 6)
        return false;
    std::memcpy(mac, LLADDR(link), 6);
    return true;
}

bool AdapterMacByName(const std::string&, uint8_t*)
{
    return false;
}
