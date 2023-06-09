#include "vg93.h"

/// region <Constructors / destructors>

VG93::VG93(EmulatorContext* context) : PortDecoder(context)
{
    _chipAttachedToPortDecoder = false;
    _ejectPending = false;
}

/// endregion </Constructors / destructors>

/// region <Methods>

/// Update FDC internal state
void VG93::process()
{

}

/// Handle Beta128 interface system controller commands
/// @param value
void VG93::processBeta128(uint8_t value)
{
    // Set active drive, Bits[0,1] (0..3)
    _drive = value & 0b0000'0011;

    // Set side Bit[4] (0..1)
    _side = ~(value >> 4) & 0b0000'0001;

    // TODO: Select drive

    // Reset Bit[3] low
    bool reset = !(value & 0b0000'0100);

    if (reset)
    {
        _status &= ~WDS_NOTRDY;
        _rqs = INTRQ;
        //_seldrive->motor = 0;
        _indexCounter = 0;

        // Set initial state after reset
        _state = S_TYPE1_CMD;
        _cmd =
        _decodedCmd = WD_CMD_RESTORE;
    }
}

uint8_t VG93::readStatus()
{
    uint8_t result = _status;

    if(!(_cmd & 0x80))
    {
        // hld & hlt
        result = _status | (((_extStatus & SIG_HLD) && (_beta128 & 0b0000'1000)) ? WDS_HEADL : 0);
    }
    return result;
}

/// Initiate disk ejection sequence
void VG93::eject(uint8_t drive)
{
    _status |= WDS_WRITEP;
    _state = S_EJECT1;
    _ejectPending = true;

    // TODO: trigger floppy disk image unmount
}

/// endregion </Methods>

/// region <PortDevice interface methods>

uint8_t VG93::portDeviceInMethod(uint16_t port)
{
    uint8_t result = 0xFF;

    // Update FDC internal states
    process();

    // Handle FDC ports
    switch (port)
    {
        case PORT_1F:   // Return state
            _rqs &= ~INTRQ;
            result = readStatus();
            break;
        case PORT_3F:   // Return current track number
            result = _track;
            break;
        case PORT_5F:   // Return current sector number
            result = _sector;
            break;
        case PORT_7F:   // Return data byte and update internal state
            _status &= ~WDS_DRQ;
            _rqs &= ~DRQ;
            result = _data;
            break;
        case PORT_FF:   // Handle BETA128 system port (#FF)
            result = _rqs | (_beta128 & 0x3F);
            break;
        default:
            break;
    }

    return result;
}

void VG93::portDeviceOutMethod(uint16_t port, uint8_t value)
{
    // Update FDC internal states
    process();

    // Eject blocks command execution
    if (_ejectPending)
    {
        return;
    }

    // Handle FDC ports
    switch (port)
    {
        case PORT_1F:
            break;
        case PORT_3F:
            _track = value;
            break;
        case PORT_5F:
            _sector = value;
            break;
        case PORT_7F:
            _data = value;
            _rqs &= ~DRQ;
            _status &= ~WDS_DRQ;
            break;
        case PORT_FF:
            processBeta128(value);
            break;
        default:
            break;
    }
}

/// endregion </PortDevice interface methods>

/// region <Ports interaction>

bool VG93::attachToPorts(PortDecoder* decoder)
{
    bool result = false;

    if (decoder)
    {
        _portDecoder = decoder;

        PortDevice* device = this;
        result = decoder->RegisterPortHandler(0xBFFD, this);
        result &= decoder->RegisterPortHandler(0xFFFD, this);

        if (result)
        {
            _chipAttachedToPortDecoder = true;
        }
    }

    return result;
}

void VG93::detachFromPorts()
{
    if (_portDecoder && _chipAttachedToPortDecoder)
    {
        _portDecoder->UnregisterPortHandler(0xBFFD);
        _portDecoder->UnregisterPortHandler(0xFFFD);

        _chipAttachedToPortDecoder = false;
    }
}

/// endregion </Ports interaction>