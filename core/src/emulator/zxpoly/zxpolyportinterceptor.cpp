#include "stdafx.h"

#include "zxpolyportinterceptor.h"

#include "emulator/zxpoly/zxpolygroup.h"

bool ZXPolyPortInterceptor::InterceptIn(uint16_t port, uint8_t& value)
{
    return _group.OnPortIn(_module, port, value);
}

bool ZXPolyPortInterceptor::InterceptOut(uint16_t port, uint8_t value)
{
    return _group.OnPortOut(_module, port, value);
}

void ZXPolyPortInterceptor::OnInResult(uint16_t port, uint8_t& value, bool fromFloatingBus)
{
    _group.OnPortInResult(_module, port, value, fromFloatingBus);
}
