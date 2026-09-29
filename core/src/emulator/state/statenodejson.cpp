#include "stdafx.h"

#include "statenodejson.h"

#include <cmath>
#include <cstdio>

namespace
{
    void WriteString(std::string& out, const std::string& text)
    {
        out.push_back('"');
        for (unsigned char c : text)
        {
            switch (c)
            {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20)
                    {
                        char escaped[8];
                        std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
                        out += escaped;
                    }
                    else
                    {
                        out.push_back(static_cast<char>(c));
                    }
            }
        }
        out.push_back('"');
    }

    void Write(std::string& out, const StateNode& node)
    {
        switch (node.kind)
        {
            case StateNode::Kind::Null: out += "null"; break;
            case StateNode::Kind::Bool: out += node.b ? "true" : "false"; break;
            case StateNode::Kind::Int: out += std::to_string(node.i); break;
            case StateNode::Kind::Double:
                if (std::isfinite(node.d))
                {
                    char number[32];
                    std::snprintf(number, sizeof(number), "%.17g", node.d);
                    out += number;
                }
                else
                {
                    out += "null";  // JSON has no NaN / infinity
                }
                break;
            case StateNode::Kind::String: WriteString(out, node.s); break;
            case StateNode::Kind::Array:
                out.push_back('[');
                for (size_t i = 0; i < node.items.size(); i++)
                {
                    if (i)
                        out.push_back(',');
                    Write(out, node.items[i]);
                }
                out.push_back(']');
                break;
            case StateNode::Kind::Object:
                out.push_back('{');
                for (size_t i = 0; i < node.members.size(); i++)
                {
                    if (i)
                        out.push_back(',');
                    WriteString(out, node.members[i].first);
                    out.push_back(':');
                    Write(out, node.members[i].second);
                }
                out.push_back('}');
                break;
        }
    }
}  // namespace

std::string StateNodeToJsonText(const StateNode& node)
{
    std::string out;
    Write(out, node);
    return out;
}
