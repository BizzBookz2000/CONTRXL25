#include "json.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace cx
{
double Json::atofSafe (const std::string& s, double def)
{
    char* e = nullptr; const double v = std::strtod (s.c_str(), &e);
    return (e && e != s.c_str()) ? v : def;
}

namespace
{
struct Parser
{
    const char* p; const char* end; bool ok = true; int depth = 0;
    void ws() { while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p; }
    bool lit (const char* w) { size_t n = strlen (w); if ((size_t) (end - p) >= n && ! strncmp (p, w, n)) { p += n; return true; } return false; }
    static void utf8 (std::string& o, unsigned cp)
    {
        if (cp < 0x80) o += (char) cp;
        else if (cp < 0x800) { o += (char) (0xC0 | (cp >> 6)); o += (char) (0x80 | (cp & 63)); }
        else if (cp < 0x10000) { o += (char) (0xE0 | (cp >> 12)); o += (char) (0x80 | ((cp >> 6) & 63)); o += (char) (0x80 | (cp & 63)); }
        else { o += (char) (0xF0 | (cp >> 18)); o += (char) (0x80 | ((cp >> 12) & 63)); o += (char) (0x80 | ((cp >> 6) & 63)); o += (char) (0x80 | (cp & 63)); }
    }
    unsigned hex4()
    {
        unsigned v = 0;
        for (int i = 0; i < 4; ++i)
        {
            if (p >= end) { ok = false; return 0; }
            const char c = *p++; v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned) (c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned) (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned) (c - 'A' + 10);
            else { ok = false; return 0; }
        }
        return v;
    }
    std::string string()
    {
        std::string o; ++p;
        while (p < end && *p != '"')
        {
            if (*p == '\\')
            {
                ++p; if (p >= end) break;
                const char c = *p++;
                switch (c)
                {
                    case 'n': o += '\n'; break; case 't': o += '\t'; break; case 'r': o += '\r'; break;
                    case 'b': o += '\b'; break; case 'f': o += '\f'; break;
                    case 'u':
                    {
                        unsigned cp = hex4();
                        if (cp >= 0xD800 && cp < 0xDC00 && p + 1 < end && p[0] == '\\' && p[1] == 'u')
                        { p += 2; const unsigned lo = hex4(); cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); }
                        utf8 (o, cp); break;
                    }
                    default: o += c;
                }
            }
            else o += *p++;
        }
        if (p < end) ++p; else ok = false;
        return o;
    }
    Json value()
    {
        ws();
        if (p >= end || ++depth > 200) { ok = false; return {}; }
        Json j;
        if (*p == '{')
        {
            j.type = Json::Object; ++p; ws();
            if (p < end && *p == '}') { ++p; --depth; return j; }
            while (ok && p < end)
            {
                ws(); if (p >= end || *p != '"') { ok = false; break; }
                std::string k = string(); ws();
                if (p >= end || *p != ':') { ok = false; break; }
                ++p;
                j.o.emplace_back (std::move (k), value());
                ws();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == '}') { ++p; break; }
                ok = false;
            }
        }
        else if (*p == '[')
        {
            j.type = Json::Array; ++p; ws();
            if (p < end && *p == ']') { ++p; --depth; return j; }
            while (ok && p < end)
            {
                j.a.push_back (value()); ws();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == ']') { ++p; break; }
                ok = false;
            }
        }
        else if (*p == '"') { j.type = Json::String; j.s = string(); }
        else if (lit ("true")) { j.type = Json::Bool; j.b = true; }
        else if (lit ("false")) { j.type = Json::Bool; j.b = false; }
        else if (lit ("null")) {}
        else
        {
            char* e = nullptr;
            const std::string tmp (p, (size_t) std::min<long> (end - p, 64));
            const double v = std::strtod (tmp.c_str(), &e);
            if (e == tmp.c_str()) { ok = false; return {}; }
            p += (e - tmp.c_str());
            j.type = Json::Number; j.n = v;
        }
        --depth;
        return j;
    }
};
}

Json Json::parse (const std::string& text, bool* okOut)
{
    Parser ps { text.data(), text.data() + text.size() };
    if (text.size() >= 3 && (unsigned char) text[0] == 0xEF) ps.p += 3;   // BOM
    Json j = ps.value();
    if (okOut) *okOut = ps.ok;
    return j;
}

static void esc (std::string& out, const std::string& s)
{
    out += '"';
    for (unsigned char c : s)
    {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20) { char b[8]; snprintf (b, sizeof b, "\\u%04x", c); out += b; }
        else out += (char) c;
    }
    out += '"';
}

void Json::dumpTo (std::string& out, bool pretty, int indent) const
{
    auto nl = [&] (int ind) { if (pretty) { out += '\n'; out.append ((size_t) ind * 2, ' '); } };
    switch (type)
    {
        case Null: out += "null"; break;
        case Bool: out += b ? "true" : "false"; break;
        case Number:
        {
            if (! std::isfinite (n)) { out += "0"; break; }
            char buf[40];
            if (std::fabs (n - std::round (n)) < 1e-9 && std::fabs (n) < 1e15) snprintf (buf, sizeof buf, "%lld", (long long) std::llround (n));
            else snprintf (buf, sizeof buf, "%.7g", n);
            out += buf; break;
        }
        case String: esc (out, s); break;
        case Array:
            out += '[';
            for (size_t i = 0; i < a.size(); ++i) { if (i) out += ','; a[i].dumpTo (out, false, indent + 1); }
            out += ']'; break;
        case Object:
            out += '{';
            for (size_t i = 0; i < o.size(); ++i)
            {
                if (i) out += ',';
                nl (indent + 1);
                esc (out, o[i].first); out += pretty ? ": " : ":";
                o[i].second.dumpTo (out, pretty, indent + 1);
            }
            if (! o.empty()) nl (indent);
            out += '}'; break;
    }
}

std::string Json::dump (bool pretty) const { std::string s; dumpTo (s, pretty, 0); return s; }
} // namespace cx
