//==============================================================================
// TigerFolders - Helpers
// UTF/string utilities, person-name parsing, GDI helpers, MyLists discovery
//==============================================================================

#include "TigerFoldersHelpers.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uxtheme.lib")

// ─────────────────────────────────────────────────────────────────────────────
//  UTF helpers
// ─────────────────────────────────────────────────────────────────────────────

std::wstring toWide (const std::string& utf8)
{
    if (utf8.empty()) return {};
    int len = MultiByteToWideChar (CP_UTF8, 0, utf8.c_str(), (int) utf8.size(), nullptr, 0);
    if (len <= 0) return {};
    std::wstring w (len, L'\0');
    MultiByteToWideChar (CP_UTF8, 0, utf8.c_str(), (int) utf8.size(), &w[0], len);
    return w;
}

std::string toUtf8 (const std::wstring& wide)
{
    if (wide.empty()) return {};
    int len = WideCharToMultiByte (CP_UTF8, 0, wide.c_str(), (int) wide.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string u (len, '\0');
    WideCharToMultiByte (CP_UTF8, 0, wide.c_str(), (int) wide.size(), &u[0], len, nullptr, nullptr);
    return u;
}

// ─────────────────────────────────────────────────────────────────────────────
//  String utilities
// ─────────────────────────────────────────────────────────────────────────────

std::wstring trimWs (const std::wstring& s)
{
    size_t a = 0, b = s.size();
    while (a < b && iswspace (s[a])) ++a;
    while (b > a && iswspace (s[b - 1])) --b;
    return s.substr (a, b - a);
}

std::wstring toLowerW (const std::wstring& s)
{
    std::wstring r = s;
    std::transform (r.begin(), r.end(), r.begin(), [] (wchar_t c) { return (wchar_t) towlower (c); });
    return r;
}

std::wstring toUpperW (const std::wstring& s)
{
    std::wstring r = s;
    std::transform (r.begin(), r.end(), r.begin(), [] (wchar_t c) { return (wchar_t) towupper (c); });
    return r;
}

bool wiEqual (const std::wstring& a, const std::wstring& b)
{
    return toLowerW (trimWs (a)) == toLowerW (trimWs (b));
}

std::wstring sanitizeSegment (std::wstring v)
{
    for (auto& c : v)
    {
        if (c == L'/' || c == L'\\' || c == L':' || c == L'*' || c == L'?'
            || c == L'"' || c == L'<' || c == L'>' || c == L'|')
            c = L'_';
    }
    return trimWs (v);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Person-name parsing
//
//  Accepts either "First Middle Last" or "Last, First Middle".
// ─────────────────────────────────────────────────────────────────────────────

std::wstring nameLast (const std::wstring& nameIn)
{
    std::wstring name = trimWs (nameIn);
    if (name.empty()) return {};

    size_t comma = name.find (L',');
    if (comma != std::wstring::npos)
        return trimWs (name.substr (0, comma));

    size_t space = name.find_last_of (L' ');
    return (space == std::wstring::npos) ? name : trimWs (name.substr (space + 1));
}

std::wstring nameFirst (const std::wstring& nameIn)
{
    std::wstring name = trimWs (nameIn);
    if (name.empty()) return {};

    size_t comma = name.find (L',');
    if (comma != std::wstring::npos)
        return trimWs (name.substr (comma + 1));

    size_t space = name.find_last_of (L' ');
    return (space == std::wstring::npos) ? std::wstring() : trimWs (name.substr (0, space));
}

// ─────────────────────────────────────────────────────────────────────────────
//  GDI helpers
// ─────────────────────────────────────────────────────────────────────────────

HFONT createFont (int height, int weight)
{
    return CreateFontW (-height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

void fillRect (HDC hdc, const RECT& rc, COLORREF color)
{
    HBRUSH br = CreateSolidBrush (color);
    FillRect (hdc, &rc, br);
    DeleteObject (br);
}

void frameRect (HDC hdc, const RECT& rc, COLORREF color)
{
    HBRUSH br = CreateSolidBrush (color);
    FrameRect (hdc, &rc, br);
    DeleteObject (br);
}

void drawText (HDC hdc, const RECT& rc, const std::wstring& text,
               COLORREF color, HFONT font, UINT flags)
{
    HFONT old = (HFONT) SelectObject (hdc, font);
    SetTextColor (hdc, color);
    SetBkMode (hdc, TRANSPARENT);
    RECT r = rc;
    DrawTextW (hdc, text.c_str(), -1, &r, flags | DT_NOPREFIX);
    SelectObject (hdc, old);
}

// ─────────────────────────────────────────────────────────────────────────────
//  VDJ MyLists root discovery (adapted from TigerTag)
// ─────────────────────────────────────────────────────────────────────────────

std::vector<fs::path> getVdjMyListsRootCandidates()
{
    std::vector<fs::path> candidates;

    wchar_t localAppData[MAX_PATH] = {};
    if (SHGetFolderPathW (nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData) == S_OK)
        candidates.push_back (fs::path (localAppData) / L"VirtualDJ" / L"MyLists");

    wchar_t docs[MAX_PATH] = {};
    if (SHGetFolderPathW (nullptr, CSIDL_PERSONAL, nullptr, 0, docs) == S_OK)
    {
        candidates.push_back (fs::path (docs) / L"VirtualDJ" / L"MyLists");
        candidates.push_back (fs::path (docs) / L"VirtualDJ" / L"My Lists");
    }

    return candidates;
}

fs::path getPreferredVdjMyListsRoot()
{
    auto candidates = getVdjMyListsRootCandidates();
    std::error_code ec;
    for (const auto& c : candidates)
        if (fs::is_directory (c, ec)) return c;
    return candidates.empty() ? fs::path() : candidates.front();
}
