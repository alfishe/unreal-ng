#include "debugger/analyzers/analysis/signatures/signatures.h"

namespace analysis::signatures
{

/// The 48K ROM: the routines programs call, and the character set. Patterns written by
/// tools/verification/analysis/make_rom48_signatures.py from data/rom/48.rom (JP / CALL / LD (nn) operands masked)
const std::vector<Signature>& Rom48()
{
    static const std::vector<Signature> catalog = {
        {"rom48.start", "rom48", "START", "F3 AF 11 FF FF C3 ?? ??", 0x0000, 0, RegionKind::Code,
         SignatureCheck::AtAddress, "ROM: reset"},
        {"rom48.error-1", "rom48", "ERROR_1", "2A ?? ?? 22 ?? ?? 18 43", 0x0008, 0, RegionKind::Code,
         SignatureCheck::AtAddress, "ROM: report an error (RST $08)"},
        {"rom48.print-a-1", "rom48", "PRINT_A_1", "C3 ?? ??", 0x0010, 0, RegionKind::Code, SignatureCheck::AtAddress,
         "ROM: print a character (RST $10)"},
        {"rom48.get-char", "rom48", "GET_CHAR", "2A ?? ?? 7E CD ?? ?? D0", 0x0018, 0, RegionKind::Code,
         SignatureCheck::AtAddress, "ROM: collect a character (RST $18)"},
        {"rom48.next-char", "rom48", "NEXT_CHAR", "CD ?? ?? 18 F7 FF FF FF", 0x0020, 0, RegionKind::Code,
         SignatureCheck::AtAddress, "ROM: collect the next character (RST $20)"},
        {"rom48.fp-calc", "rom48", "FP_CALC", "C3 ?? ??", 0x0028, 0, RegionKind::Code, SignatureCheck::AtAddress,
         "ROM: the calculator (RST $28)"},
        {"rom48.mask-int", "rom48", "MASK_INT", "F5 E5 2A ?? ?? 23 22 ?? ?? 7C B5 20 03", 0x0038, 0, RegionKind::Code,
         SignatureCheck::AtAddress, "ROM: the maskable interrupt (RST $38)"},
        {"rom48.reset-nmi", "rom48", "RESET_NMI", "F5 E5 2A ?? ?? 7C B5 20 01", 0x0066, 0, RegionKind::Code,
         SignatureCheck::AtAddress, "ROM: the NMI"},
        {"rom48.key-scan", "rom48", "KEY_SCAN", "2E 2F 11 FF FF 01 FE FE ED 78 2F E6 1F", 0x028E, 0, RegionKind::Code,
         SignatureCheck::AtAddress, "ROM: scan the keyboard"},
        {"rom48.keyboard", "rom48", "KEYBOARD", "CD ?? ?? C0 21 00 5C CB 7E 20 07 23", 0x02BF, 0, RegionKind::Code,
         SignatureCheck::AtAddress, "ROM: the keyboard interrupt routine"},
        {"rom48.beeper", "rom48", "BEEPER", "F3 7D CB 3D CB 3D 2F E6 03 4F 06 00", 0x03B5, 0, RegionKind::Code,
         SignatureCheck::AtAddress, "ROM: make a sound"},
        {"rom48.beep", "rom48", "BEEP", "EF 31 27 C0 03 34 EC ?? ?? 1F F5 04", 0x03F8, 0, RegionKind::Code,
         SignatureCheck::AtAddress, "ROM: BEEP"},
        {"rom48.sa-bytes", "rom48", "SA_BYTES", "21 3F 05 E5 21 80 1F CB 7F 28 03 21 98 0C", 0x04C2, 0,
         RegionKind::Code, SignatureCheck::AtAddress, "ROM: save bytes"},
        {"rom48.ld-bytes", "rom48", "LD_BYTES", "14 08 15 F3 3E 0F D3 FE 21 3F 05 E5", 0x0556, 0, RegionKind::Code,
         SignatureCheck::AtAddress, "ROM: load bytes"},
        {"rom48.print-out", "rom48", "PRINT_OUT", "CD ?? ?? FE 20 D2 ?? ?? FE 06 38 69", 0x09F4, 0, RegionKind::Code,
         SignatureCheck::AtAddress, "ROM: the print routine of the main channels"},
        {"rom48.pr-all", "rom48", "PR_ALL", "79 3D 3E 21 20 0E 05 4F FD CB 01 4E", 0x0B7F, 0, RegionKind::Code,
         SignatureCheck::AtAddress, "ROM: draw a character's 8 rows to the display"},
        {"rom48.po-msg", "rom48", "PO_MSG", "E5 26 00 E3 18 04 11 95 00 F5", 0x0C0A, 0, RegionKind::Code,
         SignatureCheck::AtAddress, "ROM: print a message"},
        {"rom48.cls", "rom48", "CLS", "CD ?? ?? 21 3C 5C CB AE CB C6 CD ?? ??", 0x0D6B, 0, RegionKind::Code,
         SignatureCheck::AtAddress, "ROM: clear the screen"},
        {"rom48.cl-all", "rom48", "CL_ALL", "21 00 00 22 ?? ?? FD CB 30 86 CD ?? ??", 0x0DAF, 0, RegionKind::Code,
         SignatureCheck::AtAddress, "ROM: clear the whole display"},
        {"rom48.cl-line", "rom48", "CL_LINE", "C5 CD ?? ?? 0E 08 C5 E5 78 E6 07 0F", 0x0E44, 0, RegionKind::Code,
         SignatureCheck::AtAddress, "ROM: clear lines"},
        {"rom48.cl-addr", "rom48", "CL_ADDR", "3E 18 90 57 0F 0F 0F E6 E0 6F 7A E6 18", 0x0E9B, 0, RegionKind::Code,
         SignatureCheck::AtAddress, "ROM: the display address of a line"},
        {"rom48.print-a-2", "rom48", "PRINT_A_2", "D9 E5 2A ?? ?? 5E 23 56", 0x15F2, 0, RegionKind::Code,
         SignatureCheck::AtAddress, "ROM: print a character"},
        {"rom48.chan-open", "rom48", "CHAN_OPEN", "87 C6 16 6F 26 5C 5E 23 56 7A B3 20 02", 0x1601, 0, RegionKind::Code,
         SignatureCheck::AtAddress, "ROM: open a channel"},
        {"rom48.pr-string", "rom48", "PR_STRING", "78 B1 0B C8 1A 13 D7 18 F7", 0x203C, 0, RegionKind::Code,
         SignatureCheck::AtAddress, "ROM: print a string"},
        {"rom48.stack-a", "rom48", "STACK_A", "4F 06 00 FD 21 3A 5C", 0x2D28, 0, RegionKind::Code,
         SignatureCheck::AtAddress, "ROM: stack A"},
        {"rom48.stack-bc", "rom48", "STACK_BC", "FD 21 3A 5C AF 5F 51 48 47 CD ?? ??", 0x2D2B, 0, RegionKind::Code,
         SignatureCheck::AtAddress, "ROM: stack BC"},
        {"rom48.print-fp", "rom48", "PRINT_FP", "EF 31 36 00 0B 31 37 00 0D 02 38 3E", 0x2DE3, 0, RegionKind::Code,
         SignatureCheck::AtAddress, "ROM: print a floating-point number"},
        {"rom48.charset", "rom48", "CHARSET", "00 00 00 00 00 00 00 00 00 10 10 10 10 00 10 00", 0x3D00, 768,
         RegionKind::Font, SignatureCheck::AtAddress, "ROM: the character set (96 x 8 x 8)"},
    };
    return catalog;
}

const std::vector<Signature>& Builtin()
{
    static const std::vector<Signature> all = [] {
        std::vector<Signature> out;
        const std::vector<Signature>& rom48 = Rom48();
        out.insert(out.end(), rom48.begin(), rom48.end());
        const std::vector<Signature>& players = Players();
        out.insert(out.end(), players.begin(), players.end());
        return out;
    }();
    return all;
}

}  // namespace analysis::signatures
