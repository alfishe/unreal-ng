#pragma once

#include "emulator/ports/portinterceptor.h"

#include <cstddef>

class ZXPolyGroup;

/// The ZX-Poly platform port layer of one group member: #3D00, the module
/// registers #x0FF-#x3FF and the IO-mapped window. Forwards to the group,
/// which owns the platform state shared by the four modules
class ZXPolyPortInterceptor final : public IPortInterceptor
{
public:
    ZXPolyPortInterceptor(ZXPolyGroup& group, size_t module) : _group(group), _module(module) {}

    bool InterceptIn(uint16_t port, uint8_t& value) override;
    bool InterceptOut(uint16_t port, uint8_t value) override;

private:
    ZXPolyGroup& _group;
    size_t _module;
};
