#pragma once

#include <cstdint>

namespace dzrp {

// DZRP version we implement
constexpr uint8_t VERSION_MAJOR = 2;
constexpr uint8_t VERSION_MINOR = 2;
constexpr uint8_t VERSION_PATCH = 0;

// Default port
constexpr uint16_t DEFAULT_PORT = 12000;

// Command IDs
enum class CommandId : uint8_t
{
    CMD_INIT = 1,
    CMD_CLOSE = 2,
    CMD_GET_REGISTERS = 3,
    CMD_SET_REGISTER = 4,
    CMD_WRITE_BANK = 5,
    CMD_CONTINUE = 6,
    CMD_PAUSE = 7,
    CMD_READ_MEM = 8,
    CMD_WRITE_MEM = 9,
    CMD_SET_SLOT = 10,
    CMD_GET_TBBLUE_REG = 11,
    CMD_SET_BORDER = 12,
    CMD_SET_BREAKPOINTS = 13,
    CMD_RESTORE_MEM = 14,
    CMD_LOOPBACK = 15,
    CMD_GET_SPRITES_PALETTE = 16,
    CMD_GET_SPRITES_CLIP_WINDOW_AND_CONTROL = 17,
    CMD_GET_SPRITES = 18,
    CMD_GET_SPRITE_PATTERNS = 19,
    CMD_READ_PORT = 20,
    CMD_WRITE_PORT = 21,
    CMD_EXEC_ASM = 22,
    CMD_INTERRUPT_ON_OFF = 23,
    CMD_GET_SUPPORTED_COMMANDS = 24,
    CMD_ADD_BREAKPOINT = 40,
    CMD_REMOVE_BREAKPOINT = 41,
    CMD_ADD_WATCHPOINT = 42,
    CMD_REMOVE_WATCHPOINT = 43,
    CMD_READ_STATE = 50,
    CMD_WRITE_STATE = 51,
};

// Notification IDs
enum class NotificationId : uint8_t
{
    NTF_PAUSE = 1,
};

// Break reasons for NTF_PAUSE
enum class BreakReason : uint8_t
{
    NONE = 0,
    MANUAL = 1,
    BREAKPOINT = 2,
    WATCHPOINT_READ = 3,
    WATCHPOINT_WRITE = 4,
    OTHER = 255,
};

// Machine types for CMD_INIT response
enum class MachineType : uint8_t
{
    UNKNOWN = 0,
    ZX16K = 1,
    ZX48K = 2,
    ZX128K = 3,
    ZXNEXT = 4,
};

// Register IDs for CMD_SET_REGISTER
enum class RegisterId : uint8_t
{
    PC = 0, SP = 1, AF = 2, BC = 3, DE = 4, HL = 5, IX = 6, IY = 7,
    AF2 = 8, BC2 = 9, DE2 = 10, HL2 = 11,
    IM = 13,
    F = 14, A = 15, C = 16, B = 17, E = 18, D = 19, L = 20, H = 21,
    IXL = 22, IXH = 23, IYL = 24, IYH = 25,
    F2 = 26, A2 = 27, C2 = 28, B2 = 29, E2 = 30, D2 = 31, L2 = 32, H2 = 33,
    R = 34, I = 35,
};

// Watchpoint access types
enum class WatchAccess : uint8_t
{
    READ = 0x01,
    WRITE = 0x02,
    READ_WRITE = 0x03,
};

// Sequence number limits
constexpr uint8_t SEQ_MIN = 1;
constexpr uint8_t SEQ_MAX = 15;
constexpr uint8_t SEQ_NOTIFICATION = 0;
constexpr uint8_t NAK_BIT = 0x80;

} // namespace dzrp
