#pragma once
#include "stdafx.h"

#include "common/callbackcollection.h"
#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"

#define FLAG_DOSPORTS     0x01    // TR-DOS ports are accessible
#define FLAG_TRDOS        0x02    // DOSEN trigger
#define FLAG_SETDOSROM    0x04    // TR-DOS ROM become active at #3Dxx
#define FLAG_LEAVEDOSRAM  0x08    // DOS ROM will be closed at executing RAM
#define FLAG_LEAVEDOSADR  0x10    // DOS ROM will be closed at pc>#4000
#define FLAG_CACHEON      0x20    // Cache active
#define FLAG_Z80FBUS      0x40    // Unstable data bus
#define FLAG_PROFROM      0x80    // PROF-ROM active

enum PortFlagsEnum : uint8_t
{
	DOSPorts	= 0x01,
	TRDOSROM	= 0x02,
	SetDOSROM	= 0x04,
	LeaveDOSRAM	= 0x08,
	LeaveDOSAdr	= 0x10,
	CacheOn		= 0x20,
	Z80FaultBus	= 0x40,
	ProfROM		= 0x80
};

class ModuleLogger;

class Ports
{
    /// region <ModuleLogger definitions for Module/Submodule>
protected:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_IO;
    const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_GENERIC;
    ModuleLogger* _logger = nullptr;
    /// endregion </ModuleLogger definitions for Module/Submodule>

protected:
	EmulatorContext* _context = nullptr;
	CallbackCollection _resetHandlers;

	uint16_t _brk_port_in;
	uint16_t _brk_port_out;
	uint8_t _brk_port_val;
	uint16_t _brk_mem_rd;
	uint16_t _brk_mem_wr;
	uint8_t _brk_mem_val;

public:
	// Latched port values
	uint8_t _p7FFD, _pFE, _pEFF7, _pXXXX;
	uint8_t _pDFFD, _pFDFD, _p1FFD, _pFF77;
	uint8_t _p7EFD, _p78FD, _p7AFD, _p7CFD, _gmx_config, _gmx_magic_shift; // gmx
	uint8_t _p00, _p80FD; // quorum

public:
	Ports(EmulatorContext* context);
	virtual ~Ports();

	// Module support methods
	void RegisterModule();
	void UnregisterModule();
	void Reset();				// Global /RST signal handling for all modules registered

	// Ports specific methods
	void SetBanks();


	void Out(uint16_t port, uint8_t val);
	uint8_t In(uint16_t port);
};