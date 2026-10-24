#include "im2daisychain.h"

#include <algorithm>

#include "emulator/cpu/z80.h"

void Im2DaisyChain::Add(IIm2DaisyDevice* device)
{
    if (device == nullptr || std::find(_devices.begin(), _devices.end(), device) != _devices.end())
        return;
    _devices.push_back(device);
    Update();
}

void Im2DaisyChain::Remove(IIm2DaisyDevice* device)
{
    _devices.erase(std::remove(_devices.begin(), _devices.end(), device), _devices.end());
    Update();
}

bool Im2DaisyChain::IsIntAsserted(uint32_t t)
{
    bool asserted = false;
    for (IIm2DaisyDevice* device : _devices)
    {
        const uint8_t state = device->DaisyState(t);
        if (state & IIm2DaisyDevice::kDaisyInt)
        {
            asserted = true;
            break;
        }
        if (state & IIm2DaisyDevice::kDaisyIeo)
            break;
    }
    if (!asserted)
        Update();  // a device whose timed events are done disarms
    return asserted;
}

bool Im2DaisyChain::Acknowledge(uint32_t t, uint8_t& vector)
{
    bool drove = false;
    for (IIm2DaisyDevice* device : _devices)
    {
        const uint8_t state = device->DaisyState(t);
        if (state & IIm2DaisyDevice::kDaisyInt)
        {
            vector = device->DaisyAcknowledge();
            drove = true;
            break;
        }
        if (state & IIm2DaisyDevice::kDaisyIeo)
            break;
    }
    Update();
    return drove;
}

void Im2DaisyChain::Reti(uint32_t t)
{
    for (IIm2DaisyDevice* device : _devices)
    {
        if (device->DaisyState(t) & IIm2DaisyDevice::kDaisyIeo)
        {
            device->DaisyReti();
            break;
        }
    }
    Update();
}

void Im2DaisyChain::Update()
{
    const bool armed =
        std::any_of(_devices.begin(), _devices.end(), [](const IIm2DaisyDevice* device) { return device->DaisyArmed(); });
    _cpu.SetDeviceIntLine(Z80::kDeviceIntIm2Chain, armed);
}
