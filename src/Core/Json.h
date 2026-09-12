#pragma once
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace livemix::json
{

// The smallest JSON value the engine needs: enough to write a MixContext, read a
// MixIntent back and store both with a session. Deliberately JUCE-free - src/ must
// stay framework-agnostic, and the reasoning layer has to be testable without a host.
// Object keys keep their insertion order so a serialised document is stable (the same
// context always produces the same bytes, which is what makes a golden test possible).
class Value
{
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;
    Value (bool b) : type (Type::Bool), boolean (b) {}
    Value (double d) : type (Type::Number), number (d) {}
    Value (float f) : type (Type::Number), number (double (f)) {}
    Value (int i) : type (Type::Number), number (double (i)) {}
    Value (const char* s) : type (Type::String), text (s) {}
    Value (std::string s) : type (Type::String), text (std::move (s)) {}

    static Value array() { Value v; v.type = Type::Array; return v; }
    static Value object() { Value v; v.type = Type::Object; return v; }

    Type getType() const noexcept { return type; }
    bool isNull() const noexcept { return type == Type::Null; }
    bool isArray() const noexcept { return type == Type::Array; }
    bool isObject() const noexcept { return type == Type::Object; }
    bool isNumber() const noexcept { return type == Type::Number; }
    bool isString() const noexcept { return type == Type::String; }
    bool isBool() const noexcept { return type == Type::Bool; }

    // Readers. Every one answers with the fallback rather than throwing: a malformed
    // reply from a provider is an ordinary outcome, not an exception.
    bool asBool (bool fallback = false) const noexcept;
    double asNumber (double fallback = 0.0) const noexcept;
    float asFloat (float fallback = 0.0f) const noexcept { return float (asNumber (double (fallback))); }
    int asInt (int fallback = 0) const noexcept;
    std::string asString (const std::string& fallback = {}) const;

    int size() const noexcept;                                  // array / object members
    const Value& operator[] (int index) const noexcept;         // array
    const Value& operator[] (const std::string& key) const noexcept;   // object
    bool has (const std::string& key) const noexcept;
    const std::string& keyAt (int index) const noexcept;
    const Value& valueAt (int index) const noexcept;

    // Writers.
    Value& add (Value v);                                       // array
    Value& set (std::string key, Value v);                      // object

    std::string write (bool pretty = false) const;

private:
    void writeTo (std::string& out, bool pretty, int indent) const;

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<Value> items;
    std::vector<std::pair<std::string, Value>> members;
};

// Parses `input`. On failure returns a Null value and fills `error` when given.
Value parse (const std::string& input, std::string* error = nullptr);

// Pulls the first balanced {...} out of a larger string. Reasoning providers wrap
// their answer in prose or a ``` fence often enough that this is worth having in one place.
bool extractObject (const std::string& input, std::string& objectOut);

} // namespace livemix::json
