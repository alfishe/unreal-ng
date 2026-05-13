#pragma once

#include <cstdint>

class WD1793;

/// Observer interface for WD1793 FDC events.
/// Analyzers implement this to receive FDC notifications.
class IWD1793Observer
{
public:
    virtual ~IWD1793Observer() = default;

    /// Called when FDC command is issued (port $1F write)
    /// @param command Command byte written to command register
    /// @param fdc Reference to FDC for state queries
    virtual void onFDCCommand(uint8_t command, const WD1793& fdc) { (void)command; (void)fdc; }

    /// Called on any FDC port access
    /// @param port Port number ($1F, $3F, $5F, $7F, $FF)
    /// @param value Value read or written
    /// @param isWrite True if write, false if read
    /// @param fdc Reference to FDC for state queries
    virtual void onFDCPortAccess(uint8_t port, uint8_t value, bool isWrite, const WD1793& fdc)
    {
        (void)port; (void)value; (void)isWrite; (void)fdc;
    }

    /// Called when command completes (INTRQ raised)
    /// @param status Status register value at completion
    /// @param fdc Reference to FDC for state queries
    virtual void onFDCCommandComplete(uint8_t status, const WD1793& fdc) { (void)status; (void)fdc; }
};
