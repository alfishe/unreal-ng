#pragma once

// Symbol files for disassemblers and debuggers (symbols/formats.md §3, phase S4):
//
//   ida-idc      an IDC script: set_name(0xHHHH, "name", SN_NOWARN); set_cmt(0xHHHH, "comment", 0);
//   ida-python   an IDAPython script: idc.set_name(0xHHHH, "name", idc.SN_NOWARN), idc.set_cmt(...)
//                (both read either form, MakeName / MakeNameEx / MakeComm and ida_name.set_name too)
//   ghidra       System.map lines "hhhh T name" for Ghidra's LinuxSystemMapImportScript (t / T make a function; a local
//                label is written with l, a plain label there)
//   mame         MAME debugger commands "comadd HHHH,name": MAME has comments, no labels

#include "unrealasm/symbols/codec.h"

namespace unrealasm::symbols::codecs
{
class IdaCodec : public ISymbolCodec
{
public:
    explicit IdaCodec(bool python);
    const CodecInfo& Info() const override { return _info; }
    int Detect(const Probe& probe) const override;
    SymbolDecodeResult Decode(std::span<const uint8_t> bytes) const override;
    SymbolEncodeResult Encode(const SymbolFile& file, const SymbolEncodeOptions& options) const override;

private:
    bool _python;
    CodecInfo _info;
};

class GhidraCodec : public ISymbolCodec
{
public:
    GhidraCodec();
    const CodecInfo& Info() const override { return _info; }
    int Detect(const Probe& probe) const override;
    SymbolDecodeResult Decode(std::span<const uint8_t> bytes) const override;
    SymbolEncodeResult Encode(const SymbolFile& file, const SymbolEncodeOptions& options) const override;

private:
    CodecInfo _info;
};

class MameCodec : public ISymbolCodec
{
public:
    MameCodec();
    const CodecInfo& Info() const override { return _info; }
    int Detect(const Probe& probe) const override;
    SymbolDecodeResult Decode(std::span<const uint8_t> bytes) const override;
    SymbolEncodeResult Encode(const SymbolFile& file, const SymbolEncodeOptions& options) const override;

private:
    CodecInfo _info;
};
}  // namespace unrealasm::symbols::codecs
