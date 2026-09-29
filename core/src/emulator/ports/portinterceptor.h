#pragma once

#include <cstdint>

/// Optional pre-decode port hook on a Z80 (null on stock machines).
///
/// Every IN and OUT of every model passes Z80::in() / Z80::out(); an
/// interceptor installed there sees the access before the model's port
/// decoder and may consume it. Used by the ZX-Poly layer, whose platform
/// ports (#3D00 has A0 = 0, i.e. it aliases the ULA #FE port) must never
/// reach the stock decoder
/// (docs/inprogress/2026-09-27-zxpoly/model-agnostic-sync-layer.md §4.1).
///
/// Called at the access T-state, after I/O contention was applied.
class IPortInterceptor
{
public:
    virtual ~IPortInterceptor() = default;

    /// Return true to consume the read; `value` is then the bus value
    virtual bool InterceptIn(uint16_t port, uint8_t& value) = 0;

    /// Return true to consume the write (the model decoder does not see it)
    virtual bool InterceptOut(uint16_t port, uint8_t value) = 0;

    /// The final value of a read that was not consumed, after the model
    /// decoder, observer cards and the floating bus. fromFloatingBus: the
    /// value is the video byte on the bus (48K/128K/+3 undecoded ports); the
    /// interceptor may replace it (a ZX-Poly slave takes the master's)
    virtual void OnInResult(uint16_t port, uint8_t& value, bool fromFloatingBus)
    {
        (void)port;
        (void)value;
        (void)fromFloatingBus;
    }
};
