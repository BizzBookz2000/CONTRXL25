#include "gfx.h"

namespace cx
{
const Col BANKCOL[8] = { 0xd5201f, 0xdf6216, 0xd9a521, 0xb4d11b, 0x19c4d6, 0x1d80e3, 0x8d20dd, 0xdd1e95 };

void Canvas::blend (int x, int y, Col c, int a)
{
    if (x < cx0 || y < cy0 || x >= cx1 || y >= cy1 || a <= 0) return;
    auto& d = px[(size_t) y * SCREEN_W + x];
    if (a >= 255) { d = c; return; }
    const int dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255;
    const int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    d = (Col) (((dr + ((r - dr) * a + 127) / 255) << 16) | ((dg + ((g - dg) * a + 127) / 255) << 8) | (db + ((b - db) * a + 127) / 255));
}

void Canvas::fill (int x, int y, int w, int h, Col c)
{
    const int x0 = std::max (x, cx0), y0 = std::max (y, cy0), x1 = std::min (x + w, cx1), y1 = std::min (y + h, cy1);
    for (int yy = y0; yy < y1; ++yy) std::fill (px.begin() + yy * SCREEN_W + x0, px.begin() + yy * SCREEN_W + std::max (x0, x1), c);
}

void Canvas::frame (int x, int y, int w, int h, Col c, int t)
{
    fill (x, y, w, t, c); fill (x, y + h - t, w, t, c); fill (x, y + t, t, h - 2 * t, c); fill (x + w - t, y + t, t, h - 2 * t, c);
}

static int cornerAlpha (float dx, float dy, float r)
{
    // coverage of a pixel at distance from the corner centre (4x4 supersampling)
    int in = 0;
    for (int sy = 0; sy < 4; ++sy)
        for (int sx = 0; sx < 4; ++sx)
        {
            const float px = dx + (sx + 0.5f) / 4.0f, py = dy + (sy + 0.5f) / 4.0f;
            if (px * px + py * py <= r * r) ++in;
        }
    return in * 255 / 16;
}

void Canvas::round (int x, int y, int w, int h, int r, Col c)
{
    r = std::min (r, std::min (w, h) / 2);
    if (r <= 0) { fill (x, y, w, h, c); return; }
    fill (x + r, y, w - 2 * r, h, c);
    fill (x, y + r, r, h - 2 * r, c);
    fill (x + w - r, y + r, r, h - 2 * r, c);
    for (int j = 0; j < r; ++j)
        for (int i = 0; i < r; ++i)
        {
            const int a = cornerAlpha ((float) (i - r), (float) (j - r), (float) r);
            blend (x + i, y + j, c, a); blend (x + w - 1 - i, y + j, c, a);
            blend (x + i, y + h - 1 - j, c, a); blend (x + w - 1 - i, y + h - 1 - j, c, a);
        }
}

void Canvas::roundFrame (int x, int y, int w, int h, int r, Col c, int t)
{
    r = std::min (r, std::min (w, h) / 2);
    fill (x + r, y, w - 2 * r, t, c); fill (x + r, y + h - t, w - 2 * r, t, c);
    fill (x, y + r, t, h - 2 * r, c); fill (x + w - t, y + r, t, h - 2 * r, c);
    for (int j = 0; j < r; ++j)
        for (int i = 0; i < r; ++i)
        {
            const float dx = (float) (i - r), dy = (float) (j - r);
            const int a = cornerAlpha (dx, dy, (float) r) - cornerAlpha (dx, dy, (float) (r - t));
            if (a <= 0) continue;
            blend (x + i, y + j, c, a); blend (x + w - 1 - i, y + j, c, a);
            blend (x + i, y + h - 1 - j, c, a); blend (x + w - 1 - i, y + h - 1 - j, c, a);
        }
}

void Canvas::line (float x0, float y0, float x1, float y1, Col c)
{
    const float dx = x1 - x0, dy = y1 - y0;
    const int steps = (int) std::ceil (std::max (std::fabs (dx), std::fabs (dy))) + 1;
    for (int i = 0; i <= steps; ++i)
    {
        const float t = (float) i / steps;
        pset ((int) std::lround (x0 + dx * t), (int) std::lround (y0 + dy * t), c);
    }
}

void Canvas::disc (float cx, float cy, float r, Col c)
{
    for (int y = (int) std::floor (cy - r - 1); y <= (int) std::ceil (cy + r + 1); ++y)
        for (int x = (int) std::floor (cx - r - 1); x <= (int) std::ceil (cx + r + 1); ++x)
        {
            const float d = std::sqrt ((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy));
            const float a = clampv (r - d + 0.5f, 0.0f, 1.0f);
            if (a > 0) blend (x, y, c, (int) (a * 255));
        }
}

int Canvas::textWidth (const std::string& s, const FontData& f) const
{
    int w = 0;
    for (unsigned char ch : s) { const int i = (ch >= 32 && ch < 127) ? ch - 32 : '?' - 32; w += f.g[i].adv; }
    return w;
}

std::string Canvas::fit (const std::string& s, const FontData& f, int maxW) const
{
    if (maxW <= 0 || textWidth (s, f) <= maxW) return s;
    std::string o = s;
    while (! o.empty() && textWidth (o + "..", f) > maxW) o.pop_back();
    return o + "..";
}

int Canvas::text (int x, int y, const std::string& s0, const FontData& f, Col c, Align a, int maxW)
{
    const std::string s = maxW > 0 ? fit (s0, f, maxW) : s0;
    const int w = textWidth (s, f);
    if (a == CENTER) x -= w / 2; else if (a == RIGHT) x -= w;
    for (unsigned char ch : s)
    {
        const int i = (ch >= 32 && ch < 127) ? ch - 32 : '?' - 32;
        const Glyph& g = f.g[i];
        const uint8_t* al = f.a + g.off;
        for (int yy = 0; yy < g.h; ++yy)
            for (int xx = 0; xx < g.w; ++xx)
            {
                const int v = al[yy * g.w + xx];
                if (v) blend (x + g.xo + xx, y + g.yo + yy, c, v);
            }
        x += g.adv;
    }
    return w;
}

void Canvas::image (int x, int y, const uint32_t* argb, int w, int h)
{
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
        {
            const uint32_t v = argb[j * w + i];
            blend (x + i, y + j, v & 0xFFFFFF, (int) (v >> 24));
        }
}
} // namespace cx
