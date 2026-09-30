#pragma once

/// @file ataregisters.h
/// @brief ATA register numbers, status / error / control bits and command
/// codes shared by the disk core and every board adapter.
/// Design: docs/inprogress/2026-09-21-profi/2026-09-25-ide-hdd-design.md §1, §6.

#include <cstdint>

namespace ata
{
    /// Task-file registers (CS0), numbered as on the cable (A2..A0)
    enum Register : uint8_t
    {
        Data = 0,          ///< 16-bit data port
        ErrorFeatures = 1, ///< read: error, write: features
        SectorCount = 2,
        SectorNumber = 3,  ///< LBA 7:0
        CylinderLow = 4,   ///< LBA 15:8; ATAPI byte count low
        CylinderHigh = 5,  ///< LBA 23:16; ATAPI byte count high
        DeviceHead = 6,    ///< bit 4: device (0 master, 1 slave); bit 6: LBA; bits 3:0 head / LBA 27:24
        StatusCommand = 7, ///< read: status (clears INTRQ), write: command
        /// CS1 register 6: read alternate status (no side effects), write device control
        Control = 8
    };

    namespace Status
    {
        constexpr uint8_t BSY = 0x80;
        constexpr uint8_t DRDY = 0x40;
        constexpr uint8_t DF = 0x20;
        constexpr uint8_t DSC = 0x10;  ///< seek complete (ATA-3); ATAPI: SERV
        constexpr uint8_t DRQ = 0x08;
        constexpr uint8_t CORR = 0x04;
        constexpr uint8_t IDX = 0x02;
        constexpr uint8_t ERR = 0x01;
    }  // namespace Status

    namespace Error
    {
        constexpr uint8_t ICRC = 0x80;
        constexpr uint8_t UNC = 0x40;
        constexpr uint8_t MC = 0x20;
        constexpr uint8_t IDNF = 0x10;
        constexpr uint8_t MCR = 0x08;
        constexpr uint8_t ABRT = 0x04;
        constexpr uint8_t TK0NF = 0x02;
        constexpr uint8_t AMNF = 0x01;
    }  // namespace Error

    namespace DeviceControl
    {
        constexpr uint8_t nIEN = 0x02;  ///< interrupt disable
        constexpr uint8_t SRST = 0x04;  ///< software reset
        constexpr uint8_t HOB = 0x80;   ///< read the previous (high order) values of the LBA48 registers
    }  // namespace DeviceControl

    namespace DeviceBits
    {
        constexpr uint8_t DEV = 0x10;
        constexpr uint8_t LBA = 0x40;
        constexpr uint8_t Obsolete = 0xA0;  ///< bits 7 and 5, set by old drivers, ignored
    }  // namespace DeviceBits

    namespace Command
    {
        constexpr uint8_t DeviceReset = 0x08;  ///< ATAPI only
        constexpr uint8_t RecalibrateFirst = 0x10;
        constexpr uint8_t RecalibrateLast = 0x1F;
        constexpr uint8_t ReadSectors = 0x20;
        constexpr uint8_t ReadSectorsNoRetry = 0x21;
        constexpr uint8_t ReadSectorsExt = 0x24;
        constexpr uint8_t ReadMultipleExt = 0x29;
        constexpr uint8_t WriteSectors = 0x30;
        constexpr uint8_t WriteSectorsNoRetry = 0x31;
        constexpr uint8_t WriteSectorsExt = 0x34;
        constexpr uint8_t WriteMultipleExt = 0x39;
        constexpr uint8_t ReadVerify = 0x40;
        constexpr uint8_t ReadVerifyNoRetry = 0x41;
        constexpr uint8_t ReadVerifyExt = 0x42;
        constexpr uint8_t FormatTrack = 0x50;
        constexpr uint8_t SeekFirst = 0x70;
        constexpr uint8_t SeekLast = 0x7F;
        constexpr uint8_t ExecuteDiagnostic = 0x90;
        constexpr uint8_t InitializeDeviceParameters = 0x91;
        constexpr uint8_t Packet = 0xA0;          ///< ATAPI
        constexpr uint8_t IdentifyPacket = 0xA1;  ///< ATAPI
        constexpr uint8_t ReadMultiple = 0xC4;
        constexpr uint8_t WriteMultiple = 0xC5;
        constexpr uint8_t SetMultipleMode = 0xC6;
        constexpr uint8_t StandbyImmediate = 0xE0;
        constexpr uint8_t IdleImmediate = 0xE1;
        constexpr uint8_t Standby = 0xE2;
        constexpr uint8_t Idle = 0xE3;
        constexpr uint8_t CheckPowerMode = 0xE5;
        constexpr uint8_t Sleep = 0xE6;
        constexpr uint8_t FlushCache = 0xE7;
        constexpr uint8_t FlushCacheExt = 0xEA;
        constexpr uint8_t Identify = 0xEC;
        constexpr uint8_t SetFeatures = 0xEF;
    }  // namespace Command

    /// Signatures in the task file after a reset or EXECUTE DEVICE DIAGNOSTIC
    constexpr uint8_t kAtapiSignatureLow = 0x14;
    constexpr uint8_t kAtapiSignatureHigh = 0xEB;
}  // namespace ata
