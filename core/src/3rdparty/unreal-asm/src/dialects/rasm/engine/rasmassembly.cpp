#include "dialects/rasm/engine/rasmassembly.h"

#include <memory>

#include "dialects/rasm/engine/rasmprepro.h"

namespace unrealasm::rasm
{
RasmAssembly AssembleRasm(const std::string& mainName, const std::vector<uint8_t>& source, const FileSystem& fs,
                          const EngineOptions& options)
{
    RasmAssembly result;
    PreproOptions prepro = options.prepro;
    prepro.as80 = prepro.as80 || options.as80;
    prepro.noWarning = prepro.noWarning || options.noWarning;
    PreproResult pre = Preprocess(mainName, source, fs, prepro);
    result.diagnostics = pre.diagnostics;
    result.files = pre.files;
    if (pre.fatal || pre.words.empty())
        return result;

    // the engine is large (bank tables): on the heap
    auto engine = std::make_unique<Engine>(fs, options);
    Engine& e = *engine;
    e.wl = std::move(pre.words);
    e.filename = pre.files;
    e.hexbin = std::move(pre.hexbins);
    for (const auto& d : pre.diagnostics)
    {
        if (d.severity == Severity::Error)
            e.nberr++;
    }
    e.Assemble();

    result.diagnostics.insert(result.diagnostics.end(), e.diagnostics.begin(), e.diagnostics.end());
    if (!e.printLine.empty())
        result.diagnostics.push_back({Severity::Info, 0, 0, e.printLine});
    result.ok = !HasErrors(result.diagnostics);
    result.notConverted = e.notConverted;
    for (auto& f : e.outputFiles)
        result.outputs.push_back({f.name, std::move(f.bytes)});
    for (size_t i = 0; i < e.outputFiles.size(); i++)
    {
        result.outputs[i].bank = e.outputFiles[i].bank;
        result.outputs[i].offset = e.outputFiles[i].offset;
        result.outputs[i].size = e.outputFiles[i].size;
        result.outputs[i].format = e.outputFiles[i].format;
    }
    if (e.binaryOutput)
        result.hasBinary = e.PlainBinary(result.binary, result.binaryStart, &result.binaryBank);
    result.amsdosHeader = e.amsdos != 0;
    result.zxSnapshot = e.forcezx != 0;
    result.zxBankset0 = e.bankset[0] != 0;
    result.zxRun = e.zxsnapshot.run;
    result.zxStack = e.zxsnapshot.stack;

    auto crunched = [&e](int lz) {
        return lz >= 0 && lz < static_cast<int>(e.lzsection.size()) && e.lzsection[static_cast<size_t>(lz)].lzversion != 0;
    };
    // the runs of a crunched section become one run of its final bytes, where the first of them was
    std::vector<bool> done(e.lzsection.size(), false);
    for (const TraceItem& t : e.trace)
    {
        if (!crunched(t.lz))
        {
            result.trace.push_back(t);
            continue;
        }
        const LzSection& s = e.lzsection[static_cast<size_t>(t.lz)];
        if (done[static_cast<size_t>(t.lz)])
            continue;
        done[static_cast<size_t>(t.lz)] = true;
        TraceItem c = t;
        c.instruction = false;
        c.mnemonic.clear();
        c.operands.clear();
        c.encoded.clear();
        c.fixups.clear();
        c.values.clear();
        c.outputStart = s.memstart;
        c.outputEnd = s.memstart + (s.crunchedSize >= 0 ? s.crunchedSize : s.memend - s.memstart);
        result.trace.push_back(std::move(c));
    }
    auto keep = [&](int bank) {
        if (bank >= 0 && bank < static_cast<int>(e.mem.size()) && !e.mem[static_cast<size_t>(bank)].empty() && !result.memory.count(bank))
            result.memory[bank] = e.mem[static_cast<size_t>(bank)];
    };
    for (const TraceItem& t : result.trace)
        keep(t.bank);
    for (const auto& f : result.outputs)
        keep(f.bank);
    keep(result.binaryBank);
    for (const Label& l : e.label)
    {
        RasmLabel rl;
        rl.name = l.name;
        rl.value = l.ptr;
        rl.bank = l.ibank;
        rl.orgzone = l.iorgzone;
        rl.crunched = crunched(l.lz);
        rl.local = l.local;
        rl.file = l.fileidx;
        rl.line = l.fileline;
        result.labels.push_back(std::move(rl));
    }
    for (const auto& [name, d] : e.dicoTree)
        result.variables[name] = d.v;
    for (const Alias& a : e.alias)
        result.aliases[a.alias] = a.translation;
    return result;
}
}  // namespace unrealasm::rasm
