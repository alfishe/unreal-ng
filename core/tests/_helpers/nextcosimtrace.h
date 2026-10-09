#pragma once
// Co-simulation trace sink for the ZX Spectrum Next tests: writes the common event format of
// tools/machines/next/cosim/trace-format.md - one line "<seq> <KIND> <addr> <value> <pc>" per NextREG write /
// read, port read / write, MMU slot change, interrupt acceptance, frame end and (optionally) PC sample - so a run of
// this emulator can be diffed against jnext / ZEsarUX / MAME (tools/machines/next/cosim/diff-traces.py).
//
// Test-only and read-only: it rides on hooks the CPU and the board already have (Z80::busTraceHook,
// Z80::m1TraceHook, NextBoard::SetWriteLog), so with no sink attached nothing in the emulator costs anything extra.
// The sink must outlive the run and is detached by its destructor.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "common/image/imagehelper.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/io/z80n/nextboard.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/ports/models/portdecoder_next.h"

class NextCosimTrace
{
public:
    enum Kind : uint32_t { NRW = 1, NRR = 2, POUT = 4, PIN = 8, MMU = 16, IRQ = 32, PCS = 64, FRM = 128 };

    /// Reads COSIM_KINDS (default all but PCS), COSIM_PCS=N (a PC sample every N instructions), COSIM_PCWIN=a:n,...
    /// (a sample for every instruction of frames [a, a+n), frames counted from 1), COSIM_PORT_SKIP=eb,e7 (low port
    /// bytes left out) - the same variables the reference emulators read
    NextCosimTrace(Emulator* emulator, const std::string& tracePath) : _emulator(emulator)
    {
        EmulatorContext* context = emulator->GetContext();
        _z80 = context->pCore->GetZ80();
        _memory = dynamic_cast<NextMemory*>(context->pMemory);
        _ports = dynamic_cast<PortDecoder_Next*>(context->pPortDecoder);
        _file = std::fopen(tracePath.c_str(), "w");
        if (!_file || !_memory || !_ports)
            return;
        std::setvbuf(_file, nullptr, _IOFBF, 1 << 20);
        _mask = NRW | NRR | POUT | PIN | MMU | IRQ | FRM;
        if (const char* k = std::getenv("COSIM_KINDS"))
        {
            _mask = 0;
            const std::string s(k);
            const char* names[] = {"NRW", "NRR", "POUT", "PIN", "MMU", "IRQ", "PCS", "FRM"};
            for (int i = 0; i < 8; i++)
                if (s.find(names[i]) != std::string::npos)
                    _mask |= 1u << i;
        }
        if (const char* p = std::getenv("COSIM_PCS"))
            _pcsEvery = static_cast<uint32_t>(std::atol(p));
        if (_pcsEvery)
            _mask |= PCS;
        if (const char* w = std::getenv("COSIM_PCWIN"))
        {
            const std::string s(w);
            size_t i = 0;
            while (i < s.size())
            {
                size_t c = s.find(':', i), e = s.find(',', i);
                if (e == std::string::npos)
                    e = s.size();
                if (c != std::string::npos && c < e)
                    _windows.push_back({std::strtoull(s.c_str() + i, nullptr, 10), std::strtoull(s.c_str() + c + 1, nullptr, 10)});
                i = e + 1;
            }
            if (!_windows.empty())
                _mask |= PCS;
        }
        if (const char* sk = std::getenv("COSIM_PORT_SKIP"))
        {
            const std::string s(sk);
            size_t i = 0;
            while (i < s.size())
            {
                size_t e = s.find(',', i);
                if (e == std::string::npos)
                    e = s.size();
                _portSkip[std::strtoul(s.c_str() + i, nullptr, 16) & 0xFF] = true;
                i = e + 1;
            }
        }
        _ports->Board().SetWriteLog(&_nrWrites, &_z80->m1_pc);
        _z80->busTraceHook = [this](char type, uint16_t addr, uint8_t value) { OnBus(type, addr, value); };
        _z80->m1TraceHook = [this](uint16_t pc) { OnInstruction(pc); };
        _attached = true;
    }

    ~NextCosimTrace()
    {
        if (_attached)
        {
            _z80->busTraceHook = nullptr;
            _z80->m1TraceHook = nullptr;
            _ports->Board().SetWriteLog(nullptr, nullptr);
        }
        if (_file)
            std::fclose(_file);
    }

    bool Active() const { return _attached; }

    /// Call after every RunFrame: the frame-end event and the PC window of the next frame
    void OnFrameEnd()
    {
        DrainNextRegWrites();
        _frame++;
        _inWindow = false;
        for (const auto& w : _windows)
            _inWindow |= _frame >= w.first && _frame < w.first + w.second;
        if (_mask & FRM)
            std::fprintf(_file, "%llu FRM %llu 00 %04X\n", static_cast<unsigned long long>(_seq++), static_cast<unsigned long long>(_frame), _z80->m1_pc);
    }

    /// Registers, MMU slots, NR #00-#FF (read without side effects) and the screen, in the format of the references
    void Dump(const std::string& dir)
    {
        DrainNextRegWrites();
        if (_file)
            std::fflush(_file);
        if (FILE* d = std::fopen((dir + "/state.txt").c_str(), "w"))
        {
            std::fprintf(d, "emulator unreal-ng\nframes %llu\n", static_cast<unsigned long long>(_frame));
            std::fprintf(d,
                         "regs PC=%04X SP=%04X AF=%04X BC=%04X DE=%04X HL=%04X AF2=%04X BC2=%04X DE2=%04X HL2=%04X IX=%04X IY=%04X "
                         "I=%02X R=%02X IM=%d IFF1=%d IFF2=%d HALT=%d\n",
                         _z80->pc, _z80->sp, _z80->af, _z80->bc, _z80->de, _z80->hl, _z80->alt.af, _z80->alt.bc, _z80->alt.de, _z80->alt.hl,
                         _z80->ix, _z80->iy, _z80->i, (_z80->r_low & 0x7F) | (_z80->r_hi & 0x80), _z80->im, _z80->iff1, _z80->iff2, _z80->halted ? 1 : 0);
            for (unsigned s = 0; s < 8; s++)
                std::fprintf(d, "mmu %u %02X\n", s, SlotPage(s));
            for (unsigned r = 0; r < 256; r++)
                std::fprintf(d, "nr %02X %02X\n", r, _ports->Board().Read(static_cast<uint8_t>(r)));
            std::fclose(d);
        }
        FramebufferDescriptor fb = _emulator->GetFramebuffer();
        if (fb.memoryBuffer)
            ImageHelper::SavePNG(dir + "/screen.png", fb.memoryBuffer, fb.memoryBufferSize, fb.width, fb.height);
    }

private:
    void Line(const char* kind, uint32_t addr, uint32_t value, int addrDigits, uint16_t pc)
    {
        std::fprintf(_file, "%llu %s %0*X %02X %04X\n", static_cast<unsigned long long>(_seq++), kind, addrDigits, addr, value & 0xFF, pc);
    }

    /// 8K RAM page of a slot, FF for ROM (the boot ROM, the personality ROMs) - NR #50-#57's own convention
    uint8_t SlotPage(unsigned slot) const
    {
        if (slot < 2 && _memory->BootRomEnabled())
            return 0xFF;
        return _memory->GetMmu(slot);
    }

    void DrainNextRegWrites()
    {
        for (; _drained < _nrWrites.size(); _drained++)
        {
            const NextRegWrite& w = _nrWrites[_drained];
            if (_mask & NRW)
                Line("NRW", w.reg, w.value, 2, w.pc);
        }
        // keep the vector small on long runs
        if (_drained > (1u << 16))
        {
            _nrWrites.clear();
            _drained = 0;
        }
    }

    void PollMmu(uint16_t pc)
    {
        for (unsigned s = 0; s < 8; s++)
        {
            const uint8_t p = SlotPage(s);
            if (!_mmuInit || _mmu[s] != p)
            {
                _mmu[s] = p;
                Line("MMU", s, p, 1, pc);
            }
        }
        _mmuInit = true;
    }

    void OnBus(char type, uint16_t addr, uint8_t value)
    {
        if (type != 'I' && type != 'O')
            return;
        DrainNextRegWrites();
        const uint16_t pc = _z80->m1_pc;
        if (addr == PortDecoder_Next::kPortRegSelect || addr == PortDecoder_Next::kPortRegData)
        {
            // reported as NRW (from the board's log, which also holds the NEXTREG instruction) and NRR
            if (type == 'I' && addr == PortDecoder_Next::kPortRegData && (_mask & NRR))
                Line("NRR", _ports->Board().SelectedRegister(), value, 2, pc);
            return;
        }
        if (_portSkip[addr & 0xFF])
            return;
        if (type == 'I' ? (_mask & PIN) : (_mask & POUT))
            Line(type == 'I' ? "PIN" : "POUT", addr, value, 4, pc);
    }

    void OnInstruction(uint16_t pc)
    {
        DrainNextRegWrites();  // before the MMU poll: the references log the NEXTREG write, then the slot change
        if (_mask & MMU)
            PollMmu(_prevPc);
        if (_mask & IRQ)
        {
            // an accepted maskable interrupt: IFF1 fell with no DI executed and the new instruction is the vector
            if (_prevIff1 && !_z80->iff1 && _prevOpcode != 0xF3)
                Line("IRQ", pc & 0xFF, 0, 2, _prevPc);
            _prevIff1 = _z80->iff1;
            _prevOpcode = _memory->PeekSlot(pc);
        }
        _prevPc = pc;
        if (_mask & PCS)
            if (_inWindow || (_pcsEvery && (++_pcsCount % _pcsEvery) == 0))
            {
                DrainNextRegWrites();
                Line("PCS", pc, 0, 4, pc);
            }
    }

    Emulator* _emulator;
    Z80* _z80 = nullptr;
    NextMemory* _memory = nullptr;
    PortDecoder_Next* _ports = nullptr;
    FILE* _file = nullptr;
    bool _attached = false;
    uint32_t _mask = 0;
    uint64_t _seq = 0;
    uint64_t _frame = 0;
    uint32_t _pcsEvery = 0;
    uint64_t _pcsCount = 0;
    bool _inWindow = false;
    std::vector<std::pair<uint64_t, uint64_t>> _windows;
    bool _portSkip[256] = {};
    std::vector<NextRegWrite> _nrWrites;
    size_t _drained = 0;
    uint8_t _mmu[8] = {};
    bool _mmuInit = false;
    uint16_t _prevPc = 0;
    bool _prevIff1 = false;
    uint8_t _prevOpcode = 0;
};
