// Software drawing on the 320x240 screen.
#pragma once
#include "common.h"
#include "hw.h"

namespace cx
{
struct Glyph { int16_t w, h, xo, yo, adv; int32_t off; };
struct FontData { int ascent, height; const Glyph* g; const uint8_t* a; };
extern const FontData F_SMALL, F_TEXT, F_BOLD, F_MID, F_BIG, F_HUGE;   // assets.cpp (generated)
extern const uint32_t LOGO_S[64 * 64], LOGO_M[96 * 96], LOGO_L[150 * 150];   // ILLAHTAINMENT, ARGB
using Col = uint32_t;   // 0xRRGGBB
inline Col rgb (int r, int g, int b) { return (Col) ((clampv (r, 0, 255) << 16) | (clampv (g, 0, 255) << 8) | clampv (b, 0, 255)); }
inline Col mix (Col a, Col b, float t)
{
    t = clampv (t, 0.0f, 1.0f);
    const int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255, br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
    return rgb ((int) (ar + (br - ar) * t + 0.5f), (int) (ag + (bg - ag) * t + 0.5f), (int) (ab + (bb - ab) * t + 0.5f));
}

namespace C
{
    const Col BG = 0x0d0e11, PANEL = 0x17181c, PANEL2 = 0x202228, LINE = 0x2c2e35, TEXT = 0xe9e9ec, DIM = 0x8b8d96, FAINT = 0x55575f;
    const Col ORANGE = 0xf4a21d, RED = 0xe8232c, DISPLAY = 0x2a0709, DISPTEXT = 0xff4b4b, GREEN = 0x3ddc84, BLUE = 0x3d8bff, YELLOW = 0xf2d02a;
}
extern const Col BANKCOL[8];   // A..H, the CONTRXL 25 bank colours

enum Align { LEFT, CENTER, RIGHT };

class Canvas
{
public:
    Canvas() { px.assign ((size_t) SCREEN_W * SCREEN_H, 0); }
    std::vector<uint32_t> px;
    void clear (Col c) { std::fill (px.begin(), px.end(), c); }
    void pset (int x, int y, Col c) { if ((unsigned) x < (unsigned) SCREEN_W && (unsigned) y < (unsigned) SCREEN_H) px[(size_t) y * SCREEN_W + x] = c; }
    void blend (int x, int y, Col c, int a);
    void fill (int x, int y, int w, int h, Col c);
    void frame (int x, int y, int w, int h, Col c, int t = 1);
    void round (int x, int y, int w, int h, int r, Col c);           // filled rounded rect (anti-aliased corners)
    void roundFrame (int x, int y, int w, int h, int r, Col c, int t = 1);
    void hline (int x, int y, int w, Col c) { fill (x, y, w, 1, c); }
    void vline (int x, int y, int h, Col c) { fill (x, y, 1, h, c); }
    void line (float x0, float y0, float x1, float y1, Col c);
    void disc (float cx, float cy, float r, Col c);
    int text (int x, int y, const std::string& s, const FontData& f, Col c, Align a = LEFT, int maxW = 0);   // y = top; returns width
    int textWidth (const std::string& s, const FontData& f) const;
    std::string fit (const std::string& s, const FontData& f, int maxW) const;
    void image (int x, int y, const uint32_t* argb, int w, int h);
    void clip (int x, int y, int w, int h) { cx0 = std::max (0, x); cy0 = std::max (0, y); cx1 = std::min (SCREEN_W, x + w); cy1 = std::min (SCREEN_H, y + h); }
    void noClip() { cx0 = 0; cy0 = 0; cx1 = SCREEN_W; cy1 = SCREEN_H; }
private:
    int cx0 = 0, cy0 = 0, cx1 = SCREEN_W, cy1 = SCREEN_H;
};
} // namespace cx
