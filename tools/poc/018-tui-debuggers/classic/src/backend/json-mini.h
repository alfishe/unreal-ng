// json-mini.h - minimal read-only JSON parser for the REST backend.
//
// Covers exactly the shapes the unreal-ng WebAPI emits (nested objects,
// arrays, strings, integers, bools, null) and nothing else: no streaming,
// no mutation, no document building (request bodies are built by hand with
// JsonEscape()). A parse failure returns a null-typed value and an error
// string; accessors are total functions that answer "no" instead of
// throwing, so a malformed reply degrades a panel instead of killing the UI.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace dbg {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    // Parses "text"; on failure returns a null value and sets "error".
    static Json Parse(const std::string& text, std::string* error);

    Json() = default;

    Type TypeOf() const { return type_; }
    bool IsNull() const { return type_ == Type::Null; }
    bool IsBool() const { return type_ == Type::Bool; }
    bool IsNumber() const { return type_ == Type::Number; }
    bool IsString() const { return type_ == Type::String; }
    bool IsArray() const { return type_ == Type::Array; }
    bool IsObject() const { return type_ == Type::Object; }

    bool AsBool(bool* out) const;
    bool AsString(std::string* out) const;
    bool AsInt(int64_t* out) const;
    bool AsDouble(double* out) const;

    // Object member lookup; nullptr when absent or not an object.
    const Json* Find(const std::string& key) const;
    // Typed member getters; false when absent or the wrong type.
    bool Get(const std::string& key, std::string* out) const;
    bool Get(const std::string& key, int64_t* out) const;
    bool Get(const std::string& key, bool* out) const;

    // Array access (also the member count for objects).
    size_t Size() const;
    const Json& At(size_t index) const;
    const std::vector<Json>& Items() const { return items_; }
    const std::vector<std::pair<std::string, Json>>& Members() const { return members_; }

private:
    class Parser;

    Type type_ = Type::Null;
    bool boolValue_ = false;
    double numberValue_ = 0.0;
    std::string stringValue_;
    std::vector<Json> items_;                            // Array
    std::vector<std::pair<std::string, Json>> members_;  // Object
};

// Escapes "text" into a JSON string literal body (no surrounding quotes).
std::string JsonEscape(const std::string& text);

// ---------------------------------------------------------------------------
// Implementation (header-only; the REST transport is the only consumer).
// ---------------------------------------------------------------------------

inline std::string JsonEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04X", static_cast<unsigned char>(c));
                out += buf;
            } else {
                out += c;
            }
        }
    }
    return out;
}

class Json::Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    bool Run(Json* out, std::string* error) {
        SkipWs();
        if (!Value(out)) return Fail(error, "unexpected end of input");
        SkipWs();
        if (pos_ != text_.size()) return Fail(error, "trailing characters after value");
        return true;
    }

private:
    // Utf8-encodes one code point (surrogate pairs consumed on failure return false).
    static void AppendUtf8(std::string* out, uint32_t cp) {
        if (cp <= 0x7F) {
            *out += static_cast<char>(cp);
        } else if (cp <= 0x7FF) {
            *out += static_cast<char>(0xC0 | (cp >> 6));
            *out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            *out += static_cast<char>(0xE0 | (cp >> 12));
            *out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            *out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    void SkipWs() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
            else break;
        }
    }

    bool Fail(std::string* error, const char* what) {
        if (error != nullptr) {
            *error = std::string(what) + " at offset " + std::to_string(pos_);
        }
        return false;
    }

    bool Value(Json* out) {
        if (pos_ >= text_.size()) return false;
        const char c = text_[pos_];
        if (c == '{') return Object(out);
        if (c == '[') return Array(out);
        if (c == '"') {
            out->type_ = Type::String;
            return String(&out->stringValue_);
        }
        if (c == 't' || c == 'f') return Bool(out);
        if (c == 'n') return NullWord(out);
        return Number(out);
    }

    bool Bool(Json* out) {
        if (text_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            out->type_ = Type::Bool;
            out->boolValue_ = true;
            return true;
        }
        if (text_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            out->type_ = Type::Bool;
            out->boolValue_ = false;
            return true;
        }
        return false;
    }

    bool NullWord(Json* out) {
        if (text_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            out->type_ = Type::Null;
            return true;
        }
        return false;
    }

    bool Number(Json* out) {
        const size_t start = pos_;
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            const bool numChar = (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' ||
                                 c == 'e' || c == 'E';
            if (!numChar) break;
            ++pos_;
        }
        if (pos_ == start) return false;
        const std::string token = text_.substr(start, pos_ - start);
        char* end = nullptr;
        out->numberValue_ = std::strtod(token.c_str(), &end);
        if (end == nullptr || *end != '\0') {
            pos_ = start;
            return false;
        }
        out->type_ = Type::Number;
        return true;
    }

    bool Hex4(uint32_t* out) {
        if (pos_ + 4 > text_.size()) return false;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[static_cast<size_t>(pos_ + i)];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
            else return false;
        }
        pos_ += 4;
        *out = v;
        return true;
    }

    bool String(std::string* out) {
        if (pos_ >= text_.size() || text_[pos_] != '"') return false;
        ++pos_;
        out->clear();
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') return true;
            if (c != '\\') {
                *out += c;
                continue;
            }
            if (pos_ >= text_.size()) return false;
            const char esc = text_[pos_++];
            switch (esc) {
            case '"': *out += '"'; break;
            case '\\': *out += '\\'; break;
            case '/': *out += '/'; break;
            case 'b': *out += '\b'; break;
            case 'f': *out += '\f'; break;
            case 'n': *out += '\n'; break;
            case 'r': *out += '\r'; break;
            case 't': *out += '\t'; break;
            case 'u': {
                uint32_t cp = 0;
                if (!Hex4(&cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 1 < text_.size() &&
                    text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                    const size_t save = pos_;
                    pos_ += 2;
                    uint32_t lo = 0;
                    if (!Hex4(&lo) || lo < 0xDC00 || lo > 0xDFFF) {
                        pos_ = save;  // lone surrogate: keep as replacement
                        AppendUtf8(out, 0xFFFD);
                        break;
                    }
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                AppendUtf8(out, cp >= 0xD800 && cp <= 0xDFFF ? 0xFFFD : cp);
                break;
            }
            default: return false;
            }
        }
        return false;  // unterminated
    }

    bool Array(Json* out) {
        ++pos_;  // '['
        out->type_ = Type::Array;
        out->items_.clear();
        SkipWs();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (true) {
            SkipWs();
            Json item;
            if (!Value(&item)) return false;
            out->items_.push_back(std::move(item));
            SkipWs();
            if (pos_ >= text_.size()) return false;
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == ']') {
                ++pos_;
                return true;
            }
            return false;
        }
    }

    bool Object(Json* out) {
        ++pos_;  // '{'
        out->type_ = Type::Object;
        out->members_.clear();
        SkipWs();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (true) {
            SkipWs();
            std::string key;
            if (!String(&key)) return false;
            SkipWs();
            if (pos_ >= text_.size() || text_[pos_] != ':') return false;
            ++pos_;
            SkipWs();
            Json value;
            if (!Value(&value)) return false;
            out->members_.emplace_back(std::move(key), std::move(value));
            SkipWs();
            if (pos_ >= text_.size()) return false;
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == '}') {
                ++pos_;
                return true;
            }
            return false;
        }
    }

    const std::string& text_;
    size_t pos_ = 0;
};

inline Json Json::Parse(const std::string& text, std::string* error) {
    Parser parser(text);
    Json out;
    if (parser.Run(&out, error)) return out;
    if (error != nullptr && error->empty()) *error = "invalid JSON";
    return Json();
}

inline bool Json::AsBool(bool* out) const {
    if (type_ != Type::Bool) return false;
    *out = boolValue_;
    return true;
}

inline bool Json::AsString(std::string* out) const {
    if (type_ != Type::String) return false;
    *out = stringValue_;
    return true;
}

inline bool Json::AsInt(int64_t* out) const {
    if (type_ != Type::Number) return false;
    *out = static_cast<int64_t>(numberValue_);
    return true;
}

inline bool Json::AsDouble(double* out) const {
    if (type_ != Type::Number) return false;
    *out = numberValue_;
    return true;
}

inline const Json* Json::Find(const std::string& key) const {
    if (type_ != Type::Object) return nullptr;
    for (const auto& member : members_) {
        if (member.first == key) return &member.second;
    }
    return nullptr;
}

inline bool Json::Get(const std::string& key, std::string* out) const {
    const Json* value = Find(key);
    return value != nullptr && value->AsString(out);
}

inline bool Json::Get(const std::string& key, int64_t* out) const {
    const Json* value = Find(key);
    return value != nullptr && value->AsInt(out);
}

inline bool Json::Get(const std::string& key, bool* out) const {
    const Json* value = Find(key);
    return value != nullptr && value->AsBool(out);
}

inline size_t Json::Size() const {
    return type_ == Type::Array ? items_.size()
                                : type_ == Type::Object ? members_.size() : 0;
}

inline const Json& Json::At(size_t index) const {
    static const Json kNull;
    if (type_ == Type::Array && index < items_.size()) return items_[index];
    return kNull;
}

}  // namespace dbg
