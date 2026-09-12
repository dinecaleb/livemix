#include "Json.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace livemix::json
{

namespace
{
    const Value& nullValue()
    {
        static const Value v;
        return v;
    }

    const std::string& emptyKey()
    {
        static const std::string s;
        return s;
    }

    void writeString (std::string& out, const std::string& s)
    {
        out += '"';
        for (const char c : s)
        {
            switch (c)
            {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:
                    if (static_cast<unsigned char> (c) < 0x20)
                    {
                        char buf[8];
                        std::snprintf (buf, sizeof (buf), "\\u%04x", int (static_cast<unsigned char> (c)));
                        out += buf;
                    }
                    else out += c;
            }
        }
        out += '"';
    }

    void writeNumber (std::string& out, double d)
    {
        if (! std::isfinite (d)) { out += "null"; return; }
        // Whole numbers print as integers: a schema version or a strip index reads better
        // as 1 than 1.0, and the round trip is exact either way.
        if (d == std::floor (d) && std::fabs (d) < 1.0e15)
        {
            char buf[32];
            std::snprintf (buf, sizeof (buf), "%lld", static_cast<long long> (d));
            out += buf;
            return;
        }
        char buf[40];
        std::snprintf (buf, sizeof (buf), "%.6g", d);
        out += buf;
    }

    struct Parser
    {
        const std::string& s;
        size_t i = 0;
        std::string error;

        explicit Parser (const std::string& input) : s (input) {}

        void skip()
        {
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
        }

        bool fail (const char* what)
        {
            if (error.empty()) error = std::string (what) + " at offset " + std::to_string (i);
            return false;
        }

        bool literal (const char* word)
        {
            const size_t n = std::char_traits<char>::length (word);
            if (s.compare (i, n, word) != 0) return fail ("unexpected token");
            i += n;
            return true;
        }

        bool string (std::string& out)
        {
            if (i >= s.size() || s[i] != '"') return fail ("expected a string");
            ++i;
            while (i < s.size())
            {
                const char c = s[i++];
                if (c == '"') return true;
                if (c != '\\') { out += c; continue; }
                if (i >= s.size()) return fail ("truncated escape");
                const char e = s[i++];
                switch (e)
                {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u':
                    {
                        if (i + 4 > s.size()) return fail ("truncated \\u escape");
                        const unsigned code = unsigned (std::strtoul (s.substr (i, 4).c_str(), nullptr, 16));
                        i += 4;
                        // UTF-8 for the basic plane; surrogate pairs are left as the replacement
                        // character (nothing the reasoning layer exchanges needs them).
                        if (code < 0x80) out += char (code);
                        else if (code < 0x800)
                        {
                            out += char (0xC0 | (code >> 6));
                            out += char (0x80 | (code & 0x3F));
                        }
                        else if (code >= 0xD800 && code <= 0xDFFF) out += "\xEF\xBF\xBD";
                        else
                        {
                            out += char (0xE0 | (code >> 12));
                            out += char (0x80 | ((code >> 6) & 0x3F));
                            out += char (0x80 | (code & 0x3F));
                        }
                        break;
                    }
                    default: return fail ("unknown escape");
                }
            }
            return fail ("unterminated string");
        }

        bool value (Value& out)
        {
            skip();
            if (i >= s.size()) return fail ("unexpected end of input");
            switch (s[i])
            {
                case 'n': if (! literal ("null")) return false; out = Value(); return true;
                case 't': if (! literal ("true")) return false; out = Value (true); return true;
                case 'f': if (! literal ("false")) return false; out = Value (false); return true;
                case '"': { std::string t; if (! string (t)) return false; out = Value (std::move (t)); return true; }
                case '[':
                {
                    ++i;
                    out = Value::array();
                    skip();
                    if (i < s.size() && s[i] == ']') { ++i; return true; }
                    for (;;)
                    {
                        Value item;
                        if (! value (item)) return false;
                        out.add (std::move (item));
                        skip();
                        if (i < s.size() && s[i] == ',') { ++i; continue; }
                        if (i < s.size() && s[i] == ']') { ++i; return true; }
                        return fail ("expected , or ]");
                    }
                }
                case '{':
                {
                    ++i;
                    out = Value::object();
                    skip();
                    if (i < s.size() && s[i] == '}') { ++i; return true; }
                    for (;;)
                    {
                        skip();
                        std::string key;
                        if (! string (key)) return false;
                        skip();
                        if (i >= s.size() || s[i] != ':') return fail ("expected :");
                        ++i;
                        Value item;
                        if (! value (item)) return false;
                        out.set (std::move (key), std::move (item));
                        skip();
                        if (i < s.size() && s[i] == ',') { ++i; continue; }
                        if (i < s.size() && s[i] == '}') { ++i; return true; }
                        return fail ("expected , or }");
                    }
                }
                default:
                {
                    const size_t start = i;
                    if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
                    while (i < s.size() && (std::isdigit (static_cast<unsigned char> (s[i])) || s[i] == '.' || s[i] == 'e' || s[i] == 'E'
                                            || s[i] == '-' || s[i] == '+')) ++i;
                    if (i == start) return fail ("unexpected character");
                    out = Value (std::strtod (s.substr (start, i - start).c_str(), nullptr));
                    return true;
                }
            }
        }
    };
}

bool Value::asBool (bool fallback) const noexcept
{
    if (type == Type::Bool) return boolean;
    if (type == Type::Number) return number != 0.0;
    return fallback;
}

double Value::asNumber (double fallback) const noexcept
{
    if (type == Type::Number) return number;
    if (type == Type::Bool) return boolean ? 1.0 : 0.0;
    if (type == Type::String) { try { return std::strtod (text.c_str(), nullptr); } catch (...) { return fallback; } }
    return fallback;
}

int Value::asInt (int fallback) const noexcept
{
    if (type == Type::Number || type == Type::Bool || type == Type::String)
    {
        const double d = asNumber (double (fallback));
        if (! std::isfinite (d)) return fallback;
        return int (std::lround (d));
    }
    return fallback;
}

std::string Value::asString (const std::string& fallback) const
{
    if (type == Type::String) return text;
    return fallback;
}

int Value::size() const noexcept
{
    if (type == Type::Array) return int (items.size());
    if (type == Type::Object) return int (members.size());
    return 0;
}

const Value& Value::operator[] (int index) const noexcept
{
    if (type == Type::Array && index >= 0 && index < int (items.size())) return items[size_t (index)];
    return nullValue();
}

const Value& Value::operator[] (const std::string& key) const noexcept
{
    if (type == Type::Object)
        for (const auto& m : members)
            if (m.first == key) return m.second;
    return nullValue();
}

bool Value::has (const std::string& key) const noexcept
{
    if (type != Type::Object) return false;
    for (const auto& m : members) if (m.first == key) return true;
    return false;
}

const std::string& Value::keyAt (int index) const noexcept
{
    if (type == Type::Object && index >= 0 && index < int (members.size())) return members[size_t (index)].first;
    return emptyKey();
}

const Value& Value::valueAt (int index) const noexcept
{
    if (type == Type::Object && index >= 0 && index < int (members.size())) return members[size_t (index)].second;
    return nullValue();
}

Value& Value::add (Value v)
{
    if (type != Type::Array) { type = Type::Array; items.clear(); }
    items.push_back (std::move (v));
    return *this;
}

Value& Value::set (std::string key, Value v)
{
    if (type != Type::Object) { type = Type::Object; members.clear(); }
    for (auto& m : members)
        if (m.first == key) { m.second = std::move (v); return *this; }
    members.emplace_back (std::move (key), std::move (v));
    return *this;
}

std::string Value::write (bool pretty) const
{
    std::string out;
    writeTo (out, pretty, 0);
    return out;
}

void Value::writeTo (std::string& out, bool pretty, int indent) const
{
    auto newline = [&] (int level)
    {
        if (! pretty) return;
        out += '\n';
        out.append (size_t (level) * 2, ' ');
    };

    switch (type)
    {
        case Type::Null:   out += "null"; break;
        case Type::Bool:   out += boolean ? "true" : "false"; break;
        case Type::Number: writeNumber (out, number); break;
        case Type::String: writeString (out, text); break;
        case Type::Array:
            if (items.empty()) { out += "[]"; break; }
            out += '[';
            for (size_t k = 0; k < items.size(); ++k)
            {
                if (k > 0) out += ',';
                newline (indent + 1);
                items[k].writeTo (out, pretty, indent + 1);
            }
            newline (indent);
            out += ']';
            break;
        case Type::Object:
            if (members.empty()) { out += "{}"; break; }
            out += '{';
            for (size_t k = 0; k < members.size(); ++k)
            {
                if (k > 0) out += ',';
                newline (indent + 1);
                writeString (out, members[k].first);
                out += pretty ? ": " : ":";
                members[k].second.writeTo (out, pretty, indent + 1);
            }
            newline (indent);
            out += '}';
            break;
    }
}

Value parse (const std::string& input, std::string* error)
{
    Parser p (input);
    Value v;
    if (! p.value (v))
    {
        if (error != nullptr) *error = p.error;
        return Value();
    }
    p.skip();
    if (error != nullptr) *error = p.error;
    return v;
}

bool extractObject (const std::string& input, std::string& objectOut)
{
    int depth = 0;
    bool inString = false, escaped = false;
    size_t start = std::string::npos;
    for (size_t k = 0; k < input.size(); ++k)
    {
        const char c = input[k];
        if (inString)
        {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') { inString = true; continue; }
        if (c == '{') { if (depth++ == 0) start = k; }
        else if (c == '}')
        {
            if (depth > 0 && --depth == 0 && start != std::string::npos)
            {
                objectOut = input.substr (start, k - start + 1);
                return true;
            }
        }
    }
    return false;
}

} // namespace livemix::json
