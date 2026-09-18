#pragma once

#include <string>
#include <vector>

/// region <IniFile>
//
// Minimal in-house INI reader/writer (replaces the 3rd-party SimpleIni header).
//
// Parsing is intentionally bug-compatible with the SimpleIni (char, case-insensitive)
// configuration this project shipped with, so every existing .ini file - including
// the heritage "unreal.ini" machine configs - loads identically:
//
//  - section and key lookup is ASCII case-insensitive; the first spelling seen
//    (load / SetValue order) is the one preserved on save
//  - "[name]" headers: the name ends at the first ']', anything after it on the
//    line is ignored. The heritage configs open with the line
//    "[*] unreal speccy configuration file", which yields a section named "*"
//    holding the file-header keys - Config::ParseConfig reads them via GetValue("*", ...)
//  - full-line comments start with ';' or '#'; inline comments are stripped
//    from values: a backward scan from end of line truncates at the first
//    ';', '#' or '//' found. This reproduces the local SimpleIni patch the
//    shipped parser carried (heritage configs rely on it: "intlen=128 ; t-states"
//    must parse as 128); values that legitimately contain '#' or '//' are
//    truncated too - kept for compatibility
//  - lines that are neither a valid header nor a key=value pair are skipped
//    silently; a header line without ']' does not change the current section
//  - keys encountered before the first header go to the unnamed root section ("")
//  - a UTF-8 BOM is accepted on load and never written
//  - numeric getters return the default when the key is absent, empty or not
//    fully numeric (any trailing characters); a "0x" prefix parses hexadecimal,
//    otherwise decimal
//  - a single line longer than kMaxLineLength (header, comment, or key=value)
//    is skipped entirely on load, without allocating memory proportional to
//    its length - a pathologically large or corrupted line cannot force an
//    unbounded allocation or blow up load time; every other line in the file
//    still parses normally
//
// Out of scope: multi-line values, per-key comments on save (both in-tree
// writers rewrite their files wholesale), wchar_t and code-page conversions.
//
/// endregion </IniFile>

class IniFile
{
public:
    /// A single line (header, comment, or key=value) longer than this is
    /// skipped on load rather than parsed - see the class comment. Real
    /// config lines in this project are at most a few hundred bytes; this
    /// leaves generous headroom while still bounding worst-case per-line
    /// cost against a hostile or corrupted file.
    static constexpr size_t kMaxLineLength = 64 * 1024;

    IniFile() = default;

    /// Parse a file. Returns false only when the file cannot be opened -
    /// parsing itself never fails (malformed lines are skipped).
    [[nodiscard]] bool LoadFile(const std::string& path);

    /// Serialize to a file. Returns false when the file cannot be written.
    [[nodiscard]] bool SaveFile(const std::string& path) const;

    /// Parse an in-memory document (tests, generated configurations).
    void LoadData(const std::string& data);

    /// Serialize to a string: "[section]\nkey = value" blocks separated by one
    /// blank line, LF line endings, no BOM.
    [[nodiscard]] std::string SaveData() const;

    /// Value lookup. Returns defaultValue (nullptr by default) when the key is
    /// absent. The returned pointer stays valid until this object is modified.
    [[nodiscard]] const char* GetValue(const char* section, const char* key, const char* defaultValue = nullptr) const;

    /// Numeric lookups with SimpleIni-compatible conversion rules (see class comment).
    [[nodiscard]] long GetLongValue(const char* section, const char* key, long defaultValue = 0) const;
    [[nodiscard]] double GetDoubleValue(const char* section, const char* key, double defaultValue = 0) const;

    /// All section names in first-seen order (the unnamed root section, when
    /// populated, appears as "").
    [[nodiscard]] std::vector<std::string> GetAllSections() const;

    /// Create or update a key (last write wins); creates the section on first use.
    void SetValue(const char* section, const char* key, const char* value);

    [[nodiscard]] bool IsEmpty() const;
    void Clear();

private:
    struct Entry
    {
        std::string key;
        std::string value;
    };

    struct Section
    {
        std::string name;
        std::vector<Entry> entries;
    };

    std::vector<Section> _sections;

    Section* FindSection(const char* name);
    const Section* FindSection(const char* name) const;
    Section& GetOrCreateSection(const char* name);
    void SetEntry(const std::string& section, const std::string& key, const std::string& value);
    void ParseLine(const std::string& line, std::string& currentSection);
};
