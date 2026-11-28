// Small JSON value, parser and writer (enough for CONTRXL 25 song files).
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace cx
{
struct Json
{
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;     // keeps insertion order

    Json() = default;
    Json (bool v) : type (Bool), b (v) {}
    Json (int v) : type (Number), n (v) {}
    Json (double v) : type (Number), n (v) {}
    Json (float v) : type (Number), n (v) {}
    Json (const char* v) : type (String), s (v) {}
    Json (const std::string& v) : type (String), s (v) {}
    static Json array() { Json j; j.type = Array; return j; }
    static Json object() { Json j; j.type = Object; return j; }

    bool isNull() const { return type == Null; }
    bool isObject() const { return type == Object; }
    bool isArray() const { return type == Array; }
    const Json* find (const std::string& k) const { for (auto& kv : o) if (kv.first == k) return &kv.second; return nullptr; }
    bool has (const std::string& k) const { return find (k) != nullptr; }
    const Json& operator[] (const std::string& k) const { static const Json nul; auto* p = find (k); return p ? *p : nul; }
    Json& set (const std::string& k, Json v)
    {
        type = Object;
        for (auto& kv : o) if (kv.first == k) { kv.second = std::move (v); return kv.second; }
        o.emplace_back (k, std::move (v)); return o.back().second;
    }
    void push (Json v) { type = Array; a.push_back (std::move (v)); }

    double num (double def = 0) const { return type == Number ? n : type == Bool ? (b ? 1 : 0) : (type == String ? atofSafe (s, def) : def); }
    int integer (int def = 0) const { return type == Null ? def : (int) (num (def) < 0 ? num (def) - 0.5 : num (def) + 0.5); }
    bool boolean (bool def = false) const { return type == Bool ? b : type == Number ? n != 0 : def; }
    std::string str (const std::string& def = "") const { return type == String ? s : def; }
    double get (const std::string& k, double def) const { auto* p = find (k); return p && ! p->isNull() ? p->num (def) : def; }
    bool getb (const std::string& k, bool def) const { auto* p = find (k); return p && ! p->isNull() ? p->boolean (def) : def; }
    std::string gets (const std::string& k, const std::string& def) const { auto* p = find (k); return p && p->type == String ? p->s : def; }

    static Json parse (const std::string& text, bool* ok = nullptr);
    std::string dump (bool pretty = false) const;
private:
    static double atofSafe (const std::string& s, double def);
    void dumpTo (std::string& out, bool pretty, int indent) const;
};
} // namespace cx
