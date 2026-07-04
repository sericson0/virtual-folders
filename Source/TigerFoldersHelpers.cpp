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

std::wstring normalizeLatinAccents (const std::wstring& s)
{
    std::wstring out;
    out.reserve (s.size());
    for (wchar_t c : s)
    {
        switch (c)
        {
            // a
            case L'á': case L'à': case L'â': case L'ä': case L'ã': case L'å': out += L'a'; break;
            case L'Á': case L'À': case L'Â': case L'Ä': case L'Ã': case L'Å': out += L'A'; break;
            // e
            case L'é': case L'è': case L'ê': case L'ë': out += L'e'; break;
            case L'É': case L'È': case L'Ê': case L'Ë': out += L'E'; break;
            // i
            case L'í': case L'ì': case L'î': case L'ï': out += L'i'; break;
            case L'Í': case L'Ì': case L'Î': case L'Ï': out += L'I'; break;
            // o
            case L'ó': case L'ò': case L'ô': case L'ö': case L'õ': out += L'o'; break;
            case L'Ó': case L'Ò': case L'Ô': case L'Ö': case L'Õ': out += L'O'; break;
            // u
            case L'ú': case L'ù': case L'û': case L'ü': out += L'u'; break;
            case L'Ú': case L'Ù': case L'Û': case L'Ü': out += L'U'; break;
            // n / c
            case L'ñ': out += L'n'; break;
            case L'Ñ': out += L'N'; break;
            case L'ç': out += L'c'; break;
            case L'Ç': out += L'C'; break;
            // Spanish punctuation marks → dropped
            case L'¿': case L'¡': break;
            default:   out += c;  break;
        }
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Person-name parsing
//
//  Accepts either "First Middle Last" or "Last, First Middle". Surname particles
//  (De, Di, Del, Della, Van, Von, …) stay attached to the surname so a "First
//  Last" name like "Carlos Di Sarli" yields "Di Sarli", not "Sarli".
// ─────────────────────────────────────────────────────────────────────────────

static std::vector<std::wstring> splitWords (const std::wstring& s)
{
    std::vector<std::wstring> words;
    std::wstring cur;
    for (wchar_t c : s)
    {
        if (iswspace (c)) { if (!cur.empty()) { words.push_back (cur); cur.clear(); } }
        else              cur += c;
    }
    if (!cur.empty()) words.push_back (cur);
    return words;
}

static bool isNameParticle (const std::wstring& w)
{
    static const wchar_t* kParticles[] = {
        L"de", L"del", L"della", L"delle", L"di", L"da", L"das", L"dos", L"du",
        L"la", L"le", L"las", L"los", L"lo", L"van", L"von", L"der", L"den",
        L"st", L"san", L"santa", L"mac", L"mc"
    };
    std::wstring lw = toLowerW (w);
    for (const wchar_t* p : kParticles)
        if (lw == p) return true;
    return false;
}

// Index of the first surname word in `words` — the trailing word plus any
// leading particles, but never the whole name (there must be room for a first
// name unless the name is only a surname).
static size_t surnameStart (const std::vector<std::wstring>& words)
{
    if (words.empty()) return 0;
    size_t i = words.size() - 1;
    while (i > 0 && isNameParticle (words[i - 1])) --i;
    return i;
}

static std::wstring joinWords (const std::vector<std::wstring>& words, size_t from, size_t to)
{
    std::wstring out;
    for (size_t k = from; k < to && k < words.size(); ++k)
    {
        if (!out.empty()) out += L' ';
        out += words[k];
    }
    return out;
}

std::wstring nameLast (const std::wstring& nameIn)
{
    std::wstring name = trimWs (nameIn);
    if (name.empty()) return {};

    size_t comma = name.find (L',');
    if (comma != std::wstring::npos)
        return trimWs (name.substr (0, comma));

    std::vector<std::wstring> words = splitWords (name);
    if (words.empty()) return name;
    return joinWords (words, surnameStart (words), words.size());
}

std::wstring nameFirst (const std::wstring& nameIn)
{
    std::wstring name = trimWs (nameIn);
    if (name.empty()) return {};

    size_t comma = name.find (L',');
    if (comma != std::wstring::npos)
        return trimWs (name.substr (comma + 1));

    std::vector<std::wstring> words = splitWords (name);
    size_t start = surnameStart (words);
    return (start == 0) ? std::wstring() : joinWords (words, 0, start);
}

int yearToInt (const std::wstring& s)
{
    int year = 0, digits = 0;
    for (wchar_t c : s)
    {
        if (c >= L'0' && c <= L'9') { year = year * 10 + (c - L'0'); if (++digits == 4) break; }
        else if (digits > 0) break;
    }
    return (digits == 4 && year >= 1000) ? year : 0;
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
