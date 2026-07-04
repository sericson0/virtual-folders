#pragma once

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>

#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
//  Color palette (carried over from TigerTanda's TCol for a consistent look)
// ─────────────────────────────────────────────────────────────────────────────

namespace TCol
{
    inline const COLORREF bg          = RGB(18,  21,  31);
    inline const COLORREF panel       = RGB(22,  26,  38);
    inline const COLORREF card        = RGB(26,  30,  44);
    inline const COLORREF cardBorder  = RGB(42,  46,  62);
    inline const COLORREF textNormal  = RGB(176, 180, 192);
    inline const COLORREF textBright  = RGB(220, 224, 235);
    inline const COLORREF textMuted   = RGB(140, 145, 162);   // data at lower emphasis (counts, modes)
    inline const COLORREF textDim     = RGB(106, 110, 128);   // chrome / disabled / dead rows
    inline const COLORREF accent      = RGB(217, 108, 48);
    inline const COLORREF accentBrt   = RGB(243, 161, 15);
    inline const COLORREF buttonBg    = RGB(30,  34,  48);
    inline const COLORREF buttonHover = RGB(40,  44,  62);
    inline const COLORREF buttonDisabled = RGB(24,  28,  42);
    inline const COLORREF inputBg     = RGB(15,  18,  27);
    inline const COLORREF inputBorder = RGB(90,  96,  112);   // gray outline for input boxes
    inline const COLORREF good        = RGB(76,  175, 80);
    inline const COLORREF selSubtle   = RGB(34,  38,  54);
}

// ─────────────────────────────────────────────────────────────────────────────
//  UTF + string helpers
// ─────────────────────────────────────────────────────────────────────────────

std::wstring toWide (const std::string& utf8);
std::string  toUtf8 (const std::wstring& wide);

std::wstring trimWs (const std::wstring& s);
std::wstring toLowerW (const std::wstring& s);
std::wstring toUpperW (const std::wstring& s);
bool wiEqual (const std::wstring& a, const std::wstring& b);

// Replace characters illegal in a folder name with '_', and trim.
std::wstring sanitizeSegment (std::wstring v);

// Fold accented Latin letters to their plain ASCII base (á→a, ñ→n, ü→u, …) and
// drop the Spanish ¿ / ¡ marks. Used by the optional "Normalize Spanish" toggle.
std::wstring normalizeLatinAccents (const std::wstring& s);

// ─────────────────────────────────────────────────────────────────────────────
//  Person-name helpers (bandleader / singer)
// ─────────────────────────────────────────────────────────────────────────────

// Last name from "First Last" or "Last, First".
std::wstring nameLast (const std::wstring& name);
// First name(s) from "First Last" or "Last, First".
std::wstring nameFirst (const std::wstring& name);

// Leading 4-digit year from a tag string ("1941", "1941-04-01" → 1941); 0 when
// there is no usable 4-digit year.
int yearToInt (const std::wstring& s);

// ─────────────────────────────────────────────────────────────────────────────
//  GDI helpers
// ─────────────────────────────────────────────────────────────────────────────

HFONT createFont (int height, int weight = FW_NORMAL);
void  fillRect (HDC hdc, const RECT& rc, COLORREF color);
void  frameRect (HDC hdc, const RECT& rc, COLORREF color);
void  drawText (HDC hdc, const RECT& rc, const std::wstring& text,
                COLORREF color, HFONT font,
                UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE);

// ─────────────────────────────────────────────────────────────────────────────
//  VDJ MyLists (virtual folder) root discovery
// ─────────────────────────────────────────────────────────────────────────────

std::vector<fs::path> getVdjMyListsRootCandidates();
fs::path              getPreferredVdjMyListsRoot();
