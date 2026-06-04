//==============================================================================
// TigerFolders - Core
// Plugin lifecycle, VDJ interaction, settings, component (de)serialization
//==============================================================================

#include "TigerFolders.h"

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
//  Component metadata helpers
// ─────────────────────────────────────────────────────────────────────────────

bool fieldHasSubmode (Field f)
{
    switch (f)
    {
        case Field::Genre:        // Exact / Normalize (Tango/Vals/Milonga/Cortina/Other)
        case Field::Bandleader:
        case Field::Singer:
        case Field::Grouping:
        case Field::Year:
        case Field::VocalSplit:   // singer name style for the Singers branch
            return true;
        default:
            return false;   // Label, Album
    }
}

std::wstring fieldLabel (Field f)
{
    switch (f)
    {
        case Field::Genre:      return L"Genre";
        case Field::Bandleader: return L"Bandleader";
        case Field::Singer:     return L"Singer";
        case Field::Grouping:   return L"Grouping";
        case Field::Label:      return L"Label";
        case Field::Year:       return L"Year";
        case Field::Album:      return L"Album";
        case Field::VocalSplit: return L"Singer/Inst";
    }
    return L"";
}

static std::wstring nameModeLabel (NameMode m)
{
    switch (m)
    {
        case NameMode::FirstLast:      return L"First Last";
        case NameMode::LastFirst:      return L"Last, First";
        case NameMode::Last:           return L"Last";
        case NameMode::LastUpper:      return L"LAST";
        case NameMode::LastUpperFirst: return L"LAST, First";
        case NameMode::YearRangeShort: return L"[YY] Last";
        case NameMode::YearRangeLong:  return L"[YYYY] Last";
    }
    return L"";
}

static std::wstring yearModeLabel (YearMode m)
{
    switch (m)
    {
        case YearMode::Y2:  return L"2 years";
        case YearMode::Y5:  return L"5 years";
        case YearMode::Y10: return L"10 years (decade)";
    }
    return L"";
}

static std::wstring yearScopeSuffix (GroupScope sc)
{
    return (sc == GroupScope::Instrumental) ? L" · Instrumental" : L"";
}

// Compact codes for the rhythms a mask selects: "All" for everything, otherwise
// the initials T / V / M concatenated ("TV", "VM", "TVM"). Used for the combo
// summary and as a suffix on the component's structure-list label.
std::wstring rhythmSummaryLabel (unsigned mask)
{
    if (mask == RHY_ALL || mask == 0) return L"All";
    std::wstring out;
    if (mask & RHY_TANGO)   out += L"T";
    if (mask & RHY_VALS)    out += L"V";
    if (mask & RHY_MILONGA) out += L"M";
    return out.empty() ? L"All" : out;
}

static std::wstring rhythmSuffix (unsigned mask)
{
    return (mask == RHY_ALL) ? L"" : (L"  ·  " + rhythmSummaryLabel (mask));
}

std::wstring componentModeLabel (const Component& c)
{
    std::wstring base;
    switch (c.field)
    {
        case Field::Genre:
            base = (c.genreValue == GroupValue::Normalize) ? L"Normalize" : L"Exact";
            break;
        case Field::Bandleader:
        case Field::Singer:
            base = nameModeLabel (c.nameMode);
            break;
        case Field::Grouping:
            base = (c.groupScope == GroupScope::Instrumental) ? L"Instrumental" : L"All";
            break;
        case Field::Year:
            base = yearModeLabel (c.yearMode) + yearScopeSuffix (c.yearScope);
            break;
        case Field::VocalSplit:
            base = L"Singers → " + nameModeLabel (c.nameMode);
            break;
        default:
            base = L"Exact";
            break;
    }
    return base + rhythmSuffix (c.rhythmMask);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Component (de)serialization for the settings file
// ─────────────────────────────────────────────────────────────────────────────

static std::string fieldCode (Field f)
{
    switch (f)
    {
        case Field::Genre:      return "genre";
        case Field::Bandleader: return "bandleader";
        case Field::Singer:     return "singer";
        case Field::Grouping:   return "grouping";
        case Field::Label:      return "label";
        case Field::Year:       return "year";
        case Field::Album:      return "album";
        case Field::VocalSplit: return "vocalsplit";
    }
    return "genre";
}

static std::string nameModeCode (NameMode m)
{
    switch (m)
    {
        case NameMode::FirstLast:      return "firstlast";
        case NameMode::LastFirst:      return "lastfirst";
        case NameMode::Last:           return "last";
        case NameMode::LastUpper:      return "lastupper";
        case NameMode::LastUpperFirst: return "lastupperfirst";
        case NameMode::YearRangeShort: return "yyrange";
        case NameMode::YearRangeLong:  return "yyyyrange";
    }
    return "firstlast";
}

static std::string serializeComponent (const Component& c)
{
    std::string s = fieldCode (c.field);
    switch (c.field)
    {
        case Field::Genre:
            s += ":";
            s += (c.genreValue == GroupValue::Normalize) ? "normalize" : "exact";
            break;
        case Field::Bandleader:
        case Field::Singer:
        case Field::VocalSplit:
            s += ":" + nameModeCode (c.nameMode);
            break;
        case Field::Grouping:
            s += ":";
            s += (c.groupScope == GroupScope::Instrumental) ? "inst" : "all";
            break;
        case Field::Year:
            s += ":";
            s += (c.yearMode == YearMode::Y2) ? "y2" : (c.yearMode == YearMode::Y5) ? "y5" : "y10";
            s += ":";
            s += (c.yearScope == GroupScope::Instrumental) ? "inst" : "all";
            break;
        default:
            break;
    }
    // Rhythm filter as a trailing "@<mask>" token (omitted at the default so old
    // readers and untouched components stay byte-identical).
    if (c.rhythmMask != RHY_ALL)
        s += "@" + std::to_string (c.rhythmMask);
    return s;
}

static bool parseComponent (const std::string& tokenIn, Component& out)
{
    // Peel off an optional trailing "@<mask>" rhythm filter before splitting on ':'.
    std::string token = tokenIn;
    unsigned rhythmMask = RHY_ALL;
    size_t at = token.find ('@');
    if (at != std::string::npos)
    {
        rhythmMask = (unsigned) strtoul (token.c_str() + at + 1, nullptr, 10);
        if (rhythmMask == 0) rhythmMask = RHY_ALL;
        token = token.substr (0, at);
    }

    // split on ':'
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= token.size())
    {
        size_t colon = token.find (':', start);
        if (colon == std::string::npos) { parts.push_back (token.substr (start)); break; }
        parts.push_back (token.substr (start, colon - start));
        start = colon + 1;
    }
    if (parts.empty() || parts[0].empty()) return false;

    const std::string& f = parts[0];
    Component c;
    if      (f == "genre")      c.field = Field::Genre;
    else if (f == "bandleader") c.field = Field::Bandleader;
    else if (f == "singer")     c.field = Field::Singer;
    else if (f == "grouping")   c.field = Field::Grouping;
    else if (f == "label")      c.field = Field::Label;
    else if (f == "year")       c.field = Field::Year;
    else if (f == "album")      c.field = Field::Album;
    else if (f == "vocalsplit") c.field = Field::VocalSplit;
    else return false;

    if (c.field == Field::Genre)
    {
        std::string v = (parts.size() > 1) ? parts[1] : "exact";
        c.genreValue = (v == "normalize") ? GroupValue::Normalize : GroupValue::Exact;
    }
    else if (c.field == Field::Bandleader || c.field == Field::Singer
             || c.field == Field::VocalSplit)
    {
        std::string m = (parts.size() > 1) ? parts[1] : "firstlast";
        if      (m == "firstlast")      c.nameMode = NameMode::FirstLast;
        else if (m == "lastfirst")      c.nameMode = NameMode::LastFirst;
        else if (m == "last")           c.nameMode = NameMode::Last;
        else if (m == "lastupper")      c.nameMode = NameMode::LastUpper;
        else if (m == "lastupperfirst") c.nameMode = NameMode::LastUpperFirst;
        else if (m == "yyrange")        c.nameMode = NameMode::YearRangeShort;
        else if (m == "yyyyrange")      c.nameMode = NameMode::YearRangeLong;
    }
    else if (c.field == Field::Grouping)
    {
        // Legacy entries may carry a third "normalize"/"exact" token — ignored now.
        std::string scope = (parts.size() > 1) ? parts[1] : "all";
        c.groupScope = (scope == "inst") ? GroupScope::Instrumental : GroupScope::All;
        c.groupValue = GroupValue::Exact;
    }
    else if (c.field == Field::Year)
    {
        std::string y = (parts.size() > 1) ? parts[1] : "y10";
        c.yearMode = (y == "y2") ? YearMode::Y2 : (y == "y5") ? YearMode::Y5 : YearMode::Y10;
        std::string sc = (parts.size() > 2) ? parts[2] : "all";
        c.yearScope = (sc == "inst") ? GroupScope::Instrumental : GroupScope::All;
    }

    c.rhythmMask = rhythmMask;
    out = c;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

TigerFoldersPlugin::TigerFoldersPlugin()
{
    INITCOMMONCONTROLSEX icc {};
    icc.dwSize = sizeof (icc);
    icc.dwICC  = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx (&icc);

    fontNormal = createFont (FONT_NORMAL);
    fontBold   = createFont (FONT_NORMAL, FW_BOLD);
    fontSmall  = createFont (FONT_SMALL);
    fontHeader = createFont (FONT_HEADER, FW_SEMIBOLD);
    fontTitle  = createFont (FONT_BRAND, FW_BOLD);

    if (!fontNormal) fontNormal = (HFONT) GetStockObject (DEFAULT_GUI_FONT);
    if (!fontBold)   fontBold   = (HFONT) GetStockObject (DEFAULT_GUI_FONT);
    if (!fontSmall)  fontSmall  = (HFONT) GetStockObject (DEFAULT_GUI_FONT);
    if (!fontHeader) fontHeader = fontBold;
    if (!fontTitle)  fontTitle  = (HFONT) GetStockObject (DEFAULT_GUI_FONT);

    inputBrush = CreateSolidBrush (TCol::inputBg);

    // A sensible default structure.
    components.push_back (Component { Field::Genre });
    components.push_back (Component { Field::Bandleader, NameMode::Last });
}

TigerFoldersPlugin::~TigerFoldersPlugin()
{
    if (hDlg && IsWindow (hDlg)) DestroyWindow (hDlg);
    if (fontNormal) DeleteObject (fontNormal);
    if (fontBold)   DeleteObject (fontBold);
    if (fontSmall)  DeleteObject (fontSmall);
    if (fontHeader && fontHeader != fontBold) DeleteObject (fontHeader);
    if (fontTitle)  DeleteObject (fontTitle);
    if (inputBrush) DeleteObject (inputBrush);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Plugin lifecycle
// ─────────────────────────────────────────────────────────────────────────────

HRESULT VDJ_API TigerFoldersPlugin::OnLoad()
{
    wchar_t dllPath[MAX_PATH] = {};
    GetModuleFileNameW (hInstance, dllPath, MAX_PATH);
    fs::path dllDir = fs::path (dllPath).parent_path();
    settingsPath = dllDir / L"tigerfolders_settings.ini";

    loadSettings();

    DeclareParameterButton (&paramOpen, PID_OPEN, "Open TigerFolders", "Open");
    return S_OK;
}

HRESULT VDJ_API TigerFoldersPlugin::OnGetPluginInfo (TVdjPluginInfo8* info)
{
    info->PluginName  = "TigerFolders";
    info->Author      = "TigerFolders Project";
    info->Description = "Build a tree of virtual folders from a selected folder's song tags";
    info->Version     = "1.0.0";
    info->Flags       = VDJFLAG_NODOCK;
    info->Bitmap      = nullptr;
    return S_OK;
}

// UI-only plugin; lives under SoundEffect so VDJ loads it via the DSP
// interface, but it never touches audio.
HRESULT VDJ_API TigerFoldersPlugin::OnProcessSamples (float* /*buffer*/, int /*nb*/)
{
    return S_OK;
}

ULONG VDJ_API TigerFoldersPlugin::Release()
{
    delete this;
    HINSTANCE hInst = GetModuleHandleW (nullptr);
    UnregisterClassW (WND_CLASS, hInst);
    return S_OK;
}

HRESULT VDJ_API TigerFoldersPlugin::OnGetUserInterface (TVdjPluginInterface8* pluginInterface)
{
    if (hDlg && IsWindow (hDlg))
    {
        dialogRequestedOpen  = true;
        suppressNextHideSync = false;
        ShowWindow (hDlg, SW_SHOWNOACTIVATE);
        SetWindowPos (hDlg, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        pluginInterface->Type = VDJINTERFACE_DIALOG;
        pluginInterface->hWnd = hDlg;
        return S_OK;
    }

    ensureFoldersWindowClass (hInstance);

    HWND parentHwnd = nullptr;
    double hwndVal = 0;
    if (GetInfo ("get hwnd", &hwndVal) == S_OK && hwndVal != 0)
        parentHwnd = (HWND) (intptr_t) hwndVal;

    int posX, posY;
    if (parentHwnd)
    {
        RECT pr;
        GetWindowRect (parentHwnd, &pr);
        posX = pr.left + ((pr.right - pr.left) - DLG_W) / 2;
        posY = pr.top  + ((pr.bottom - pr.top) - DLG_H) / 2;
    }
    else
    {
        posX = (GetSystemMetrics (SM_CXSCREEN) - DLG_W) / 2;
        posY = (GetSystemMetrics (SM_CYSCREEN) - DLG_H) / 2;
    }

    hDlg = CreateWindowExW (WS_EX_TOOLWINDOW, WND_CLASS, L"TigerFolders",
                            WS_POPUP | WS_CLIPCHILDREN | WS_VISIBLE,
                            posX, posY, DLG_W, DLG_H,
                            parentHwnd, nullptr, hInstance, this);

    if (hDlg)
    {
        pluginInterface->Type = VDJINTERFACE_DIALOG;
        pluginInterface->hWnd = hDlg;
        return S_OK;
    }
    return E_NOTIMPL;
}

HRESULT VDJ_API TigerFoldersPlugin::OnParameter (int id)
{
    if (id == PID_OPEN && hDlg && IsWindow (hDlg))
    {
        dialogRequestedOpen  = true;
        suppressNextHideSync = false;
        ShowWindow (hDlg, SW_SHOWNOACTIVATE);
        SetWindowPos (hDlg, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    return S_OK;
}

// ─────────────────────────────────────────────────────────────────────────────
//  VDJ interaction
// ─────────────────────────────────────────────────────────────────────────────

std::wstring TigerFoldersPlugin::vdjGetString (const char* query)
{
    char buf[2048] = {};
    HRESULT hr = GetStringInfo (query, buf, sizeof (buf));
    if (SUCCEEDED (hr) && buf[0] != '\0')
        return toWide (buf);
    return {};
}

double TigerFoldersPlugin::vdjGetValue (const char* query)
{
    double val = 0.0;
    if (FAILED (GetInfo (query, &val)))
        return 0.0;
    return val;
}

void TigerFoldersPlugin::vdjSend (const std::string& cmd)
{
    SendCommand (cmd.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
//  Settings
// ─────────────────────────────────────────────────────────────────────────────

void TigerFoldersPlugin::loadSettings()
{
    if (settingsPath.empty()) return;
    std::ifstream in (settingsPath);
    if (!in.is_open()) return;

    std::vector<Component> loaded;
    bool haveComponents = false;

    std::string line;
    while (std::getline (in, line))
    {
        auto eq = line.find ('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr (0, eq);
        std::string val = line.substr (eq + 1);

        if (key == "root")
        {
            rootName = trimWs (toWide (val));
            if (rootName.empty()) rootName = L"MyLists";   // never leave the root blank
        }
        else if (key == "splitsingers")
        {
            splitMultiSingers = (trimWs (toWide (val)) == L"1");
        }
        else if (key == "replaceexisting")
        {
            replaceExisting = (trimWs (toWide (val)) == L"1");
        }
        else if (key == "normalizespanish")
        {
            normalizeSpanish = (trimWs (toWide (val)) == L"1");
        }
        else if (key == "singleyearrange")
        {
            singleYearRange = (trimWs (toWide (val)) == L"1");
        }
        else if (key == "cutoffmode")
        {
            std::wstring m = trimWs (toWide (val));
            folderCutoffMode = (m == L"leaf") ? CutoffMode::Leaf
                             : (m == L"any")  ? CutoffMode::Any
                                              : CutoffMode::None;
        }
        else if (key == "cutoffsize")
        {
            int n = _wtoi (trimWs (toWide (val)).c_str());
            folderCutoffSize = (n < 2) ? 2 : (n > 10 ? 10 : n);
        }
        else if (key == "excluded")
        {
            std::wstring e = trimWs (toWide (val));
            if (!e.empty()) excludedFolders.insert (e);
        }
        else if (key == "components")
        {
            haveComponents = true;
            size_t start = 0;
            while (start <= val.size())
            {
                size_t semi = val.find (';', start);
                std::string tok = (semi == std::string::npos)
                    ? val.substr (start) : val.substr (start, semi - start);
                Component c;
                if (!tok.empty() && parseComponent (tok, c))
                    loaded.push_back (c);
                if (semi == std::string::npos) break;
                start = semi + 1;
            }
        }
    }

    if (haveComponents)
        components = loaded;   // may be empty if user cleared all
}

void TigerFoldersPlugin::saveSettings()
{
    if (settingsPath.empty()) return;

    fs::path tmp = settingsPath;
    tmp += ".tmp";
    {
        std::ofstream out (tmp, std::ios::trunc);
        if (!out.is_open()) return;

        out << "root=" << toUtf8 (rootName) << "\n";
        out << "splitsingers=" << (splitMultiSingers ? 1 : 0) << "\n";
        out << "replaceexisting=" << (replaceExisting ? 1 : 0) << "\n";
        out << "normalizespanish=" << (normalizeSpanish ? 1 : 0) << "\n";
        out << "singleyearrange=" << (singleYearRange ? 1 : 0) << "\n";
        out << "cutoffmode="
            << (folderCutoffMode == CutoffMode::Leaf ? "leaf"
              : folderCutoffMode == CutoffMode::Any  ? "any" : "none") << "\n";
        out << "cutoffsize=" << folderCutoffSize << "\n";
        out << "components=";
        for (size_t i = 0; i < components.size(); ++i)
        {
            if (i) out << ";";
            out << serializeComponent (components[i]);
        }
        out << "\n";
        for (const auto& e : excludedFolders)
            out << "excluded=" << toUtf8 (e) << "\n";
        out.flush();
        if (!out.good()) { out.close(); std::error_code ec; fs::remove (tmp, ec); return; }
    }
    std::error_code ec;
    fs::rename (tmp, settingsPath, ec);
    if (ec) fs::remove (tmp, ec);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DLL entry point
//
//  VirtualDJ probes plugins in Plugins64\SoundEffect via IID_IVdjPluginDsp8.
//  We also accept IID_IVdjPluginBasic8 for non-SoundEffect scans.
// ─────────────────────────────────────────────────────────────────────────────

STDAPI DllGetClassObject (REFCLSID rclsid, REFIID riid, LPVOID* ppObject)
{
    if (memcmp (&rclsid, &CLSID_VdjPlugin8, sizeof (GUID)) == 0
        && (memcmp (&riid, &IID_IVdjPluginDsp8,   sizeof (GUID)) == 0
         || memcmp (&riid, &IID_IVdjPluginBasic8, sizeof (GUID)) == 0))
    {
        *ppObject = new TigerFoldersPlugin();
        return S_OK;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}
