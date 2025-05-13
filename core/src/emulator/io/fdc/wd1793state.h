#pragma once

#include "emulator/io/fdc/fdc.h"
#include <cstdint>

struct WD93SignalsHost
{
    /// WRITE ENABLE
    /// @details Active state writes data from Data Access Lines into the selected register
    bool we_in;

    /// CHIP SELECT
    /// @details Active state selects the chip and enables host communication with FDC
    bool cs_in;

    /// READ ENABLE
    /// @details Active state triggers placing data from the selected register to Data Access Lines
    bool re_in;

    /// INTERRUPT REQUEST
    /// @details Set at any command completion and reset when STATUS REGISTER is read or the COMMAND REGISTER written to
    bool intrq_out;

    /// DATA REQUEST
    /// @details Indicates that the DATA REGISTER contains assembled data in Read operations, or the
    /// DATA REGISTER is empty in Write operations.
    /// This signal is reset when serviced by the host through reading or loading the DATA REGISTER
    /// in Read or Write operations, respectively.
    bool drq_out;

    /// WRITE FAULT
    /// @details Set when a write operation fails and reset when the write operation completes
    bool write_fault_out;
};

struct WD93SignalsFDD
{
    /// HEAD LOAD TIMING
    /// @details Head is engaged (true = active/asserted state)
    bool hlt_in;

    /// READY - gives permission to start Read or Write operation
    /// @details This input indicates disk readiness and is sampled for a logic high before Read or Write commands
    /// are performed.
    /// If Ready is low the Read or Write operation is not performed a n d an interrupt is generated.
    /// Type I operations are performed regardless of the state of Ready.
    /// The Ready input appears in inverted format as Status Register bit 7
    bool ready_in;

    /// TRACK 00
    /// @details Informs FDC that the Read/Write head positioned over Track 00 (usually triggered by hardware sensor on FDD side)
    bool tr00_in;

    /// INDEX PULSE
    /// @details informs FDC that the index hole is encountered on the diskette
    /// (opto-coupler on FDD detects index hole during diskette rotation)
    bool ip_in;

    /// WRITE PROTECT
    /// @details If signal is active - it terminates any Write command andsets the Write Protection bit in Status Register
    bool wp_in;

    /// DOUBLE_DENSITY
    /// @details If active - Double Density (MFM) is seleted. Inactive - Single Density (FM)
    bool dden_in;


    /// WRITE FAULT / VFO ENABLE
    bool vfoe_inout;


    /// HEAD LOAD
    /// @details Engage head to read/write media
    bool hld_out;

    /// WRITE GATE
    /// @details Activated before writing to a diskette
    bool wg_out;

    /// READ GATE
    /// @details Activated when field of zeroes or ones detected and is used for synchronization
    bool rg_out;

    /// TRACK GREATER THAN 43
    /// @details Read/Write head positioned between tracks 44-86, Output valid ONLY for Read and Write commands
    bool tg43_out;

    /// SSO - SIDE SELECT OUTPUT
    /// @details The logic level of the Side Select Output is directly
    /// controlled by the 'S' flag in Type Il or Ill commands.
    /// When S = 1, SSO is set to a logic 1. When S = 0.
    /// SSO is set to a logic 0. The Side Select Output is only
    /// updated at the beginning of a Type Il or Ill command.
    /// It is forced to a logic 0 upon a MASTER RESET.
    bool sso_out = false;

    /// DIRECTION
    /// @details Active when stepping in (to a disk center) and inactive when stepping out (to disk edge)
    bool direction_out = true;

    /// STEP
    /// @details Generates pulse / strobe for each FDD head step (in a direction defined by DIRECTION signal)
    bool step_out;

    /// WRITE DATA
    /// @details 250 ns (MFM) or 500 ns (FM) pulse per flux transition.
    bool wd_out;
};


struct WD93Signals
{
    WD93SignalsHost host;
    WD93SignalsFDD fdd;
};

/// WD1793 registers accessible via host system
struct WD93Registers
{
    uint8_t commandRegister;
    uint8_t trackRegister;
    uint8_t sectorRegister;
    uint8_t dataRegister;
};

struct WD93Counters
{
    // The host system has only one byte transfer time worth to read or write from Data Register
    /// Otherwise DATA LOST error will occur
    /// @details 32us for MFM @250Kbps, 64us for FM @125Kbps => 114 t-states for MFM and 228 t-states for FM
    /// @note Initialize with timeout value and decrease. Timeout will be detected when zero or negative
    int dataLostTimeoutCounter;

    /// Detects if the requested sector or header doesn't appear during disk rotation.
    /// Used in: Read Sector, Write Sector, Read Address, Format commands
    /// @details 5 disk revolutions => 1s or 3.5 million t-states
    /// @note Initialize with timeout value and decrease. Timeout will be detected when zero or negative
    int indexTimeoutCounter;

    /// Time until FDD motor stops
    /// @details When the last command completes, the system starts a countdown (e.g., 2–5 seconds)
    /// @note Initialize with timeout value and decrease. Timeout will be detected when zero or negative
    int fddMotorTimeoutCounter;
};

struct WD93Mode
{
    /// @brief True if MFM mode is selected, false if FM mode is selected
    /// @details WD1793 is capable to operate in two modes:
    /// - DD (Double Density) mode with MFM data encoding, 250 Kbps transfer speed
    /// - FM (Single Density) mode with FM data encoding, 125 Kbps transfer speed
    bool isInMFMMode;

};


/// Hardware state for WD1793 FDC
/// @brief Holds all output, internal state, registers and counters
/// @see WD93OutputSignals
/// @see WD93Registers
/// @see WD93Counters
struct WD93State
{
    WD93Signals signals;
    WD93Registers registers;
    WD93Counters counters;
};