#include "symbols/codecs/native/nativecodec.h"

#include <algorithm>
#include <initializer_list>

#include "symbols/io/json.h"

namespace unrealasm::symbols::codecs
{
namespace
{
using json::Value;

constexpr std::string_view kFormat = "unreal-symbols";

Value Str(const std::string& s)
{
    Value v;
    v.type = Value::Type::String;
    v.string = s;
    return v;
}

Value Int(int64_t i)
{
    Value v;
    v.type = Value::Type::Integer;
    v.integer = i;
    return v;
}

Value Bool(bool b)
{
    Value v;
    v.type = Value::Type::Bool;
    v.boolean = b;
    return v;
}

Value Object()
{
    Value v;
    v.type = Value::Type::Object;
    return v;
}

void Add(Value& object, const char* name, Value v)
{
    object.object.emplace_back(name, std::move(v));
}

/// The members of `extra` (a JSON object text) appended to `object`
void AddExtra(Value& object, const std::string& extra)
{
    if (extra.empty())
        return;
    Value parsed;
    std::string error;
    size_t offset = 0;
    if (json::Parse(extra, parsed, error, offset) && parsed.type == Value::Type::Object)
        for (auto& member : parsed.object)
            object.object.push_back(std::move(member));
}

/// The members of `object` not named in `known`, as a JSON object text ("" when none)
std::string Extra(const Value& object, std::initializer_list<std::string_view> known)
{
    Value rest = Object();
    for (const auto& [name, value] : object.object)
        if (std::find(known.begin(), known.end(), name) == known.end())
            rest.object.emplace_back(name, value);
    return rest.object.empty() ? std::string() : json::Write(rest);
}

class Reader
{
public:
    explicit Reader(Diagnostics& diagnostics) : _diagnostics(diagnostics) {}

    void Error(const std::string& where, const std::string& message)
    {
        _diagnostics.push_back({Severity::Error, 0, 0, where + ": " + message});
    }

    void Warning(const std::string& where, const std::string& message)
    {
        _diagnostics.push_back({Severity::Warning, 0, 0, where + ": " + message});
    }

    bool String(const Value& object, const char* name, std::string& out, const std::string& where)
    {
        const Value* v = object.Get(name);
        if (!v)
            return true;
        if (v->type != Value::Type::String)
        {
            Error(where, std::string("\"") + name + "\" is not a string");
            return false;
        }
        out = v->string;
        return true;
    }

    bool Unsigned(const Value& object, const char* name, uint32_t& out, const std::string& where)
    {
        const Value* v = object.Get(name);
        if (!v)
            return true;
        if (v->type != Value::Type::Integer || v->integer < 0 || v->integer > 0xFFFFFFFFLL)
        {
            Error(where, std::string("\"") + name + "\" is not a number 0..4294967295");
            return false;
        }
        out = static_cast<uint32_t>(v->integer);
        return true;
    }

    bool Boolean(const Value& object, const char* name, bool& out, const std::string& where)
    {
        const Value* v = object.Get(name);
        if (!v)
            return true;
        if (v->type != Value::Type::Bool)
        {
            Error(where, std::string("\"") + name + "\" is not true / false");
            return false;
        }
        out = v->boolean;
        return true;
    }

    bool ReadSymbol(const Value& v, Symbol& s, const std::string& where)
    {
        if (v.type != Value::Type::Object)
        {
            Error(where, "not an object");
            return false;
        }
        bool ok = String(v, "name", s.name, where);
        if (ok && s.name.empty())
        {
            Error(where, "no name");
            return false;
        }
        std::string space = "cpu:main";
        ok = ok && String(v, "space", space, where);
        if (ok && !AddressSpace::Parse(space, s.location.space))
        {
            Error(where, "unknown space \"" + space + "\"");
            return false;
        }
        ok = ok && Unsigned(v, "offset", s.location.offset, where);
        std::string kind;
        ok = ok && String(v, "kind", kind, where);
        if (ok && !kind.empty() && !ParseKind(kind, s.kind))
            Warning(where, "unknown kind \"" + kind + "\" (read as unknown)");
        ok = ok && Unsigned(v, "size", s.size, where);
        uint32_t window = 0xFFFFFFFF;
        ok = ok && Unsigned(v, "window", window, where);
        if (ok && window != 0xFFFFFFFF)
        {
            if (window > 3)
            {
                Error(where, "\"window\" is not 0..3");
                return false;
            }
            s.window = static_cast<int>(window);
        }
        if (const Value* scope = v.Get("scope"); ok && scope)
        {
            ok = scope->type == Value::Type::Object && String(*scope, "parent", s.parent, where);
            if (!ok)
                Error(where, "\"scope\" is not an object with a \"parent\" string");
        }
        ok = ok && String(v, "module", s.module, where) && String(v, "section", s.section, where) && Boolean(v, "exported", s.exported, where);
        if (const Value* traits = v.Get("traits"); ok && traits)
        {
            ok = traits->type == Value::Type::Array;
            for (const Value& t : traits->array)
            {
                ok = ok && t.type == Value::Type::String;
                if (ok)
                    s.traits.push_back(t.string);
            }
            if (!ok)
                Error(where, "\"traits\" is not a list of strings");
        }
        if (const Value* source = v.Get("source"); ok && source)
        {
            if (source->type != Value::Type::Object)
                Error(where, "\"source\" is not an object");
            ok = source->type == Value::Type::Object && String(*source, "file", s.source.file, where) &&
                 Unsigned(*source, "line", s.source.line, where) && Unsigned(*source, "column", s.source.column, where);
        }
        ok = ok && String(v, "comment", s.comment, where);
        if (const Value* aliases = v.Get("aliases"); ok && aliases)
        {
            ok = aliases->type == Value::Type::Array;
            for (const Value& a : aliases->array)
            {
                ok = ok && a.type == Value::Type::String;
                if (ok)
                    s.aliases.push_back(a.string);
            }
            if (!ok)
                Error(where, "\"aliases\" is not a list of strings");
        }
        ok = ok && Boolean(v, "enabled", s.enabled, where);
        if (const Value* provenance = v.Get("provenance"); ok && provenance)
        {
            if (provenance->type != Value::Type::Object)
                Error(where, "\"provenance\" is not an object");
            ok = provenance->type == Value::Type::Object && String(*provenance, "importer", s.provenance.importer, where) &&
                 String(*provenance, "raw", s.provenance.raw, where) && Unsigned(*provenance, "line", s.provenance.line, where) &&
                 String(*provenance, "type", s.provenance.type, where);
        }
        if (ok)
            s.extra = Extra(v, {"name", "space", "offset", "kind", "size", "window", "scope", "module", "section", "exported", "traits",
                                "source", "comment", "aliases", "enabled", "provenance"});
        return ok;
    }

    bool ReadSet(const Value& v, SymbolSet& set, size_t number)
    {
        const std::string where = "set " + std::to_string(number + 1);
        if (v.type != Value::Type::Object)
        {
            Error(where, "not an object");
            return false;
        }
        bool ok = String(v, "id", set.id, where) && String(v, "title", set.title, where);
        if (const Value* origin = v.Get("origin"); ok && origin)
        {
            if (origin->type != Value::Type::Object)
                Error(where, "\"origin\" is not an object");
            ok = origin->type == Value::Type::Object && String(*origin, "kind", set.origin.kind, where) &&
                 String(*origin, "where", set.origin.where, where) && String(*origin, "sha256", set.origin.sha256, where);
        }
        if (const Value* priority = v.Get("priority"); ok && priority)
        {
            ok = priority->type == Value::Type::Integer && priority->integer >= -1000000 && priority->integer <= 1000000;
            if (ok)
                set.priority = static_cast<int>(priority->integer);
            else
                Error(where, "\"priority\" is not a number -1000000..1000000");
        }
        ok = ok && Boolean(v, "enabled", set.enabled, where);
        std::string caseRule;
        ok = ok && String(v, "case", caseRule, where);
        if (caseRule == "fold")
            set.caseRule = CaseRule::Fold;
        else if (!caseRule.empty() && caseRule != "exact")
            Warning(where, "unknown case rule \"" + caseRule + "\" (read as exact)");
        if (!ok)
            return false;
        if (set.id.empty())
        {
            set.id = "set" + std::to_string(number + 1);
            Warning(where, "no id (named " + set.id + ")");
        }
        if (const Value* symbols = v.Get("symbols"))
        {
            if (symbols->type != Value::Type::Array)
            {
                Error(where, "\"symbols\" is not a list");
                return false;
            }
            for (size_t i = 0; i < symbols->array.size(); ++i)
            {
                Symbol s;
                if (ReadSymbol(symbols->array[i], s, set.id + " symbol " + std::to_string(i + 1)))
                    set.symbols.push_back(std::move(s));
            }
        }
        set.extra = Extra(v, {"id", "title", "origin", "priority", "enabled", "case", "symbols"});
        return true;
    }

private:
    Diagnostics& _diagnostics;
};

Value SymbolValue(const Symbol& s)
{
    Value v = Object();
    Add(v, "name", Str(s.name));
    Add(v, "space", Str(s.location.space.Format()));
    Add(v, "offset", Int(s.location.offset));
    if (s.kind != SymbolKind::Unknown)
        Add(v, "kind", Str(std::string(KindName(s.kind))));
    if (s.size)
        Add(v, "size", Int(s.size));
    if (s.window >= 0)
        Add(v, "window", Int(s.window));
    if (!s.parent.empty())
    {
        Value scope = Object();
        Add(scope, "parent", Str(s.parent));
        Add(v, "scope", std::move(scope));
    }
    if (!s.module.empty())
        Add(v, "module", Str(s.module));
    if (!s.section.empty())
        Add(v, "section", Str(s.section));
    if (s.exported)
        Add(v, "exported", Bool(true));
    if (!s.traits.empty())
    {
        Value traits;
        traits.type = Value::Type::Array;
        for (const std::string& t : s.traits)
            traits.array.push_back(Str(t));
        Add(v, "traits", std::move(traits));
    }
    if (!s.source.file.empty() || s.source.line || s.source.column)
    {
        Value source = Object();
        if (!s.source.file.empty())
            Add(source, "file", Str(s.source.file));
        if (s.source.line)
            Add(source, "line", Int(s.source.line));
        if (s.source.column)
            Add(source, "column", Int(s.source.column));
        Add(v, "source", std::move(source));
    }
    if (!s.comment.empty())
        Add(v, "comment", Str(s.comment));
    if (!s.aliases.empty())
    {
        Value aliases;
        aliases.type = Value::Type::Array;
        for (const std::string& a : s.aliases)
            aliases.array.push_back(Str(a));
        Add(v, "aliases", std::move(aliases));
    }
    if (!s.enabled)
        Add(v, "enabled", Bool(false));
    if (!s.provenance.importer.empty() || !s.provenance.raw.empty() || s.provenance.line || !s.provenance.type.empty())
    {
        Value p = Object();
        if (!s.provenance.importer.empty())
            Add(p, "importer", Str(s.provenance.importer));
        if (!s.provenance.raw.empty())
            Add(p, "raw", Str(s.provenance.raw));
        if (s.provenance.line)
            Add(p, "line", Int(s.provenance.line));
        if (!s.provenance.type.empty())
            Add(p, "type", Str(s.provenance.type));
        Add(v, "provenance", std::move(p));
    }
    AddExtra(v, s.extra);
    return v;
}

/// "name": value lines of an object, each indented, without the braces
void Members(const Value& object, const std::string& indent, const std::string& nl, std::string& out, bool more)
{
    for (size_t i = 0; i < object.object.size(); ++i)
        out += indent + json::Quote(object.object[i].first) + ": " + json::Write(object.object[i].second) +
               (i + 1 < object.object.size() || more ? "," : "") + nl;
}
}  // namespace

NativeCodec::NativeCodec() : _info{"native", "unreal-ng symbol file (*.usym.json)", Family::Native, {"json"}} {}

int NativeCodec::Detect(const Probe& probe) const
{
    const std::string_view text(reinterpret_cast<const char*>(probe.bytes.data()), probe.bytes.size());
    size_t p = text.substr(0, 3) == "\xEF\xBB\xBF" ? 3 : 0;
    while (p < text.size() && (text[p] == ' ' || text[p] == '\t' || text[p] == '\r' || text[p] == '\n'))
        ++p;
    if (p >= text.size() || text[p] != '{')
        return 0;
    const size_t format = text.find("\"format\"");
    if (format != std::string_view::npos && text.find("\"unreal-symbols\"", format) != std::string_view::npos)
        return 100;
    return probe.extension == "json" ? 30 : 10;
}

SymbolDecodeResult NativeCodec::Decode(std::span<const uint8_t> bytes) const
{
    SymbolDecodeResult result;
    std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (text.substr(0, 3) == "\xEF\xBB\xBF")
        text.remove_prefix(3);
    Value root;
    std::string error;
    size_t offset = 0;
    if (!json::Parse(text, root, error, offset))
    {
        result.diagnostics.push_back({Severity::Error, 0, offset, "not JSON: " + error});
        return result;
    }
    const Value* format = root.Get("format");
    if (root.type != Value::Type::Object || !format || format->type != Value::Type::String || format->string != kFormat)
    {
        result.diagnostics.push_back({Severity::Error, 0, 0, "not an unreal-symbols file (\"format\" missing or different)"});
        return result;
    }
    if (const Value* version = root.Get("version"); version && (version->type != Value::Type::Integer || version->integer > kVersion))
        result.diagnostics.push_back({Severity::Warning, 0, 0, "a newer file version; fields this version does not know are kept"});
    Reader reader(result.diagnostics);
    if (const Value* sets = root.Get("sets"))
    {
        if (sets->type != Value::Type::Array)
        {
            result.diagnostics.push_back({Severity::Error, 0, 0, "\"sets\" is not a list"});
            return result;
        }
        for (size_t i = 0; i < sets->array.size(); ++i)
        {
            SymbolSet set;
            if (reader.ReadSet(sets->array[i], set, i))
                result.file.sets.push_back(std::move(set));
        }
    }
    result.file.extra = Extra(root, {"format", "version", "generator", "sets"});
    result.ok = !HasErrors(result.diagnostics);
    return result;
}

SymbolEncodeResult NativeCodec::Encode(const SymbolFile& file, const SymbolEncodeOptions& options) const
{
    SymbolEncodeResult result;
    const std::string& nl = options.lineEnd;
    std::string out = "{" + nl;
    Value head = Object();
    Add(head, "format", Str(std::string(kFormat)));
    Add(head, "version", Int(kVersion));
    Add(head, "generator", Str("unreal-asm"));
    AddExtra(head, file.extra);
    Members(head, "  ", nl, out, true);
    out += "  \"sets\": [" + nl;
    for (size_t i = 0; i < file.sets.size(); ++i)
    {
        const SymbolSet& set = file.sets[i];
        Value fields = Object();
        Add(fields, "id", Str(set.id));
        if (!set.title.empty())
            Add(fields, "title", Str(set.title));
        if (!set.origin.kind.empty() || !set.origin.where.empty() || !set.origin.sha256.empty())
        {
            Value origin = Object();
            if (!set.origin.kind.empty())
                Add(origin, "kind", Str(set.origin.kind));
            if (!set.origin.where.empty())
                Add(origin, "where", Str(set.origin.where));
            if (!set.origin.sha256.empty())
                Add(origin, "sha256", Str(set.origin.sha256));
            Add(fields, "origin", std::move(origin));
        }
        Add(fields, "priority", Int(set.priority));
        Add(fields, "enabled", Bool(set.enabled));
        if (set.caseRule == CaseRule::Fold)
            Add(fields, "case", Str("fold"));
        AddExtra(fields, set.extra);
        out += "    {" + nl;
        Members(fields, "      ", nl, out, true);
        out += "      \"symbols\": [" + nl;
        for (size_t k = 0; k < set.symbols.size(); ++k)
            out += "        " + json::Write(SymbolValue(set.symbols[k])) + (k + 1 < set.symbols.size() ? "," : "") + nl;
        result.written += set.symbols.size();
        out += "      ]" + nl;
        out += std::string("    }") + (i + 1 < file.sets.size() ? "," : "") + nl;
    }
    out += "  ]" + nl + "}" + nl;
    result.bytes.assign(out.begin(), out.end());
    result.ok = true;
    return result;
}
}  // namespace unrealasm::symbols::codecs
