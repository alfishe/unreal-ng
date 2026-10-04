// The host adapter's MAC on Windows: Npcap lists no link-layer address, the IP Helper API does (see
// hostadaptermac.h). Npcap names an adapter "\Device\NPF_{GUID}"; GetAdaptersAddresses gives the same GUID

#include "platform/hostadaptermac.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <iphlpapi.h>

#include <cstring>
#include <vector>

bool LinkAddressOf(const sockaddr*, uint8_t*)
{
    return false;
}

bool AdapterMacByName(const std::string& name, uint8_t mac[6])
{
    const size_t brace = name.find('{');
    if (brace == std::string::npos)
        return false;
    const std::string guid = name.substr(brace);
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buffer(size);
    ULONG status = GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
    if (status == ERROR_BUFFER_OVERFLOW)
    {
        buffer.resize(size);
        status = GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
    }
    if (status != NO_ERROR)
        return false;
    for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); a; a = a->Next)
    {
        if (a->AdapterName && guid == a->AdapterName && a->PhysicalAddressLength == 6)
        {
            std::memcpy(mac, a->PhysicalAddress, 6);
            return true;
        }
    }
    return false;
}
