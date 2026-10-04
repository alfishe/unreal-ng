#pragma once

/// @file hostadaptermac.h
/// @brief The hardware (MAC) address of a host network adapter, for the network bridge's MAC translation on Wi-Fi
/// (network SN6b): frames leave with the host adapter's own address. One implementation per OS: macOS reads the
/// AF_LINK address libpcap lists, Linux the AF_PACKET one, Windows asks the IP Helper API by the adapter's GUID.

#include <cstdint>
#include <string>

struct sockaddr;

/// A link-layer address in a libpcap address list (macOS AF_LINK, Linux AF_PACKET); false for any other family
bool LinkAddressOf(const sockaddr* address, uint8_t mac[6]);

/// The adapter's address by its packet-library name (Windows: "\\Device\\NPF_{GUID}"); false where the OS gives it
/// through LinkAddressOf instead, or when not found
bool AdapterMacByName(const std::string& name, uint8_t mac[6]);
