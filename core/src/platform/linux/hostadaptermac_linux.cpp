// The host adapter's MAC on Linux: the AF_PACKET address libpcap lists (see hostadaptermac.h)

#include "platform/hostadaptermac.h"

#include <cstring>
#include <netpacket/packet.h>
#include <sys/socket.h>

bool LinkAddressOf(const sockaddr* address, uint8_t mac[6])
{
    if (!address || address->sa_family != AF_PACKET)
        return false;
    const auto* link = reinterpret_cast<const sockaddr_ll*>(address);
    if (link->sll_halen != 6)
        return false;
    std::memcpy(mac, link->sll_addr, 6);
    return true;
}

bool AdapterMacByName(const std::string&, uint8_t*)
{
    return false;
}
