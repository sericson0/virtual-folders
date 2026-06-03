#pragma once

#include "TigerFoldersHelpers.h"

#define NODLLEXPORT
#include "vdjPlugin8.h"
#include "vdjDsp8.h"

#include <windowsx.h>
#include <uxtheme.h>
#include <fstream>
#include <map>
#include <set>
#include <utility>

// ─────────────────────────────────────────────────────────────────────────────
//  Component data model
// ─────────────────────────────────────────────────────────────────────────────

enum class Field { Genre, Bandleader, Singer, Grouping, Label, Year, Album, VocalSplit };

// Display format for Bandleader / Singer.
//  YearRangeShort / YearRangeLong prepend the min–max recording year of that
//  person within the parent folder, e.g. "41-43 Fiorentino" / "1941-1943 Fiorentino".
enum class NameMode { FirstLast, LastFirst, Last, LastUpper, LastUpperFirst,
                      YearRangeShort, YearRangeLong };

// Grouping scope + value.
enum class GroupScope { All, Instrumental };
enum class GroupValue { Exact, Normalize };

// Year bucket width (all grid-aligned).
enum class YearMode { Y2, Y5, Y10 };

struct Component
{
    Field      field      = Field::Genre;
    NameMode   nameMode   = NameMode::FirstLast;   // Bandleader / Singer
    GroupScope groupScope = GroupScope::All;       // Grouping
    GroupValue groupValue = GroupValue::Exact;     // Grouping (legacy; scope-only now)
    GroupValue genreValue = GroupValue::Exact;     // Genre (Exact / Normalize)
    YearMode   yearMode   = YearMode::Y10;         // Year
    GroupScope yearScope  = GroupScope::All;       // Year (All / Instrumental-only)
};

bool         fieldHasSubmode (Field f);
std::wstring fieldLabel (Field f);
std::wstring componentModeLabel (const Component& c);   // human-readable mode

// ─────────────────────────────────────────────────────────────────────────────
//  Scanned song + preview tree
// ─────────────────────────────────────────────────────────────────────────────

struct ScannedSong
{
    std::wstring filePath;
    std::wstring artist;     // raw "Bandleader - Singer"
    std::wstring title;
    std::wstring genre;
    std::wstring grouping;
    std::wstring label;
    std::wstring album;
    std::wstring year;       // 4-digit string (may be empty)

    // Derived in scan:
    std::wstring bandleader;
    std::wstring singer;
    bool         instrumental = false;
};

// Flattened preview-tree row for owner-draw display.
struct PreviewRow
{
    std::wstring name;
    std::wstring path;        // full folder path from the root (exclusion identity)
    int          depth = 0;
    int          count = 0;   // songs in this node's subtree
    bool         isLeaf = false;
    bool         excluded = false;          // user unchecked this folder
    bool         ancestorExcluded = false;  // an ancestor folder is unchecked
};

// Long-running operation, driven in chunks by a WM_TIMER state machine so the
// UI thread never blocks.
enum class Op { None, Scanning, Building };

// ─────────────────────────────────────────────────────────────────────────────
//  Control IDs
// ─────────────────────────────────────────────────────────────────────────────

enum CtrlId
{
    IDC_COMBO_FIELD   = 3101,
    IDC_COMBO_MODE    = 3102,
    IDC_BTN_ADD       = 3103,
    IDC_LIST_COMPONENTS = 3104,
    IDC_BTN_REMOVE    = 3105,
    IDC_BTN_CLEAR     = 3106,
    IDC_EDIT_ROOT     = 3107,
    IDC_BTN_SCAN      = 3108,
    IDC_BTN_BUILD     = 3109,
    IDC_LIST_PREVIEW  = 3110,
    IDC_BTN_CLOSE     = 3111,
    IDC_BTN_SETTINGS  = 3112,
};

inline constexpr UINT_PTR TIMER_OP        = 1;   // drives the chunked build phase
inline constexpr UINT_PTR TIMER_KEEPALIVE = 2;   // re-asserts the window on top
inline constexpr UINT_PTR TIMER_SETTLE    = 3;   // waits for recurse_folder to populate

// The scan advances by posting this to itself rather than via WM_TIMER. WM_TIMER
// is coalesced and floored to ~10-15ms, which capped the scan at ~1k rows/sec
// regardless of read speed; a self-posted message runs as fast as rows can be read.
inline constexpr UINT WM_APP_SCANSTEP = WM_APP + 1;

// ─────────────────────────────────────────────────────────────────────────────
//  Layout constants
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr int DLG_W       = 720;
inline constexpr int DLG_H       = 460;
inline constexpr int TITLE_H     = 30;
inline constexpr int PAD         = 10;
inline constexpr int ROW_H       = 28;
inline constexpr int BTN_H       = 28;
inline constexpr int COMP_ITEM_H = 26;
inline constexpr int PREV_ITEM_H = 20;
inline constexpr int LEFT_COL_PCT = 52;

inline constexpr int FONT_NORMAL = 13;
inline constexpr int FONT_SMALL  = 12;
inline constexpr int FONT_HEADER = 14;
inline constexpr int FONT_BRAND  = 16;

inline constexpr const wchar_t* WND_CLASS = L"TigerFoldersVdjDialog";

// ─────────────────────────────────────────────────────────────────────────────
//  Plugin class
// ─────────────────────────────────────────────────────────────────────────────

class TigerFoldersPlugin : public IVdjPluginDsp8
{
public:
    TigerFoldersPlugin();
    ~TigerFoldersPlugin() override;

    HRESULT VDJ_API OnLoad() override;
    HRESULT VDJ_API OnGetPluginInfo (TVdjPluginInfo8* info) override;
    ULONG   VDJ_API Release() override;
    HRESULT VDJ_API OnGetUserInterface (TVdjPluginInterface8* pluginInterface) override;
    HRESULT VDJ_API OnParameter (int id) override;
    HRESULT VDJ_API OnProcessSamples (float* buffer, int nb) override;

    // VDJ helpers (TigerFolders.cpp)
    std::wstring vdjGetString (const char* query);
    double       vdjGetValue  (const char* query);
    void         vdjSend      (const std::string& cmd);

    // Settings (TigerFolders.cpp)
    void loadSettings();
    void saveSettings();

    // Path logic (TigerFoldersPath.cpp)
    std::wstring segmentFor (const Component& c, const ScannedSong& s) const;
    std::wstring buildPathFor (const ScannedSong& s) const;       // root/.../leaf
    std::wstring effectivePath (const std::wstring& full) const;  // trim at first excluded folder
    void         computeSingerYearRanges();   // fill singerYearRanges for [YY]/[YYYY] modes

    bool isUnfiled (const ScannedSong& s) const;   // path collapses to just the root

    // Preview folder exclusions (checkboxes)
    void applyExclusionFlags();                 // recompute excluded/ancestorExcluded on rows
    void togglePreviewExclusion (int rowIdx);   // check/uncheck a preview folder

    // Scan — chunked state machine (TigerFoldersScan.cpp)
    void scanBegin();
    void scanSettleStep();              // wait for recurse_folder to finish flattening
    void scanStep();                    // process one browser row per tick
    void scanFinish();
    void rebuildPreview();              // fills `previewRows` + counts from `songs`

    // Write engine (TigerFoldersWrite.cpp)
    bool resolveMyListsRoot (fs::path& out) const;   // true only if an existing MyLists dir
    bool managedRootExists() const;
    void removeManagedRoot();
    void buildPlan();                   // compute planFolders + planLeaves + counts
    void buildBegin (bool wipeFirst);
    void buildStep();                   // process a batch of folders/leaves per tick
    void buildFinish();
    bool ensureVirtualFolderListFileExists (const std::wstring& listPath);
    void registerVirtualFolder (const std::wstring& path);   // add_virtualfolder
    bool appendSongsToLeaf (const std::wstring& leafPath,
                            const std::vector<std::wstring>& songPaths) const;

    // UI (TigerFoldersUI.cpp)
    void uiRepopulateModeCombo();       // refill mode combo for current field
    void uiSyncAddButton();             // enable/disable ADD
    void uiRefreshComponentList();
    void uiRefreshPreviewList();
    void uiUpdateStatus (const std::wstring& msg, bool error = false);
    void uiResetAddRow();               // reset combos to placeholder after add/update
    void uiLoadComponentForEdit (int idx);   // double-click → edit in place
    void uiSetOpRunning (bool running); // toggle button labels/enable during scan/build

    // Parameter IDs
    enum ParamId { PID_OPEN = 0 };
    int paramOpen = 0;

    // ── State ────────────────────────────────────────────────────────────────
    std::vector<Component>   components;
    std::wstring             rootName = L"MyLists";

    std::vector<ScannedSong> songs;
    std::vector<PreviewRow>  previewRows;
    int                      previewFolderCount = 0;
    std::set<std::wstring>   excludedFolders;     // full paths of unchecked folders

    // Min–max recording year per "parentPath/baseName" group, for [YY]/[YYYY] name
    // modes. Rebuilt from `songs` before each preview/build pass.
    std::map<std::wstring, std::pair<int,int>> singerYearRanges;

    std::wstring             selectedFolderPath;   // currently selected VDJ folder
    std::wstring             statusText;
    bool                     statusError = false;

    fs::path                 settingsPath;

    // ── Chunked operation state ───────────────────────────────────────────
    Op   op       = Op::None;
    int  opIndex  = 0;
    int  opTotal  = 0;
    bool opCancel = false;
    std::wstring preScanFolder;          // browser folder to restore after scan

    // Scan settle phase (wait for recurse_folder's async flatten to populate)
    bool scanSettling     = false;
    int  scanSettleTicks  = 0;
    int  scanLastCount    = -1;
    int  scanStableCount  = 0;
    bool mmPeriodSet      = false;   // raised the system timer resolution for the scan
    DWORD lastStatusTick  = 0;       // throttles scan-progress repaints to ~20/sec

    // Build plan (computed by buildPlan)
    std::vector<std::wstring> planFolders;   // unique folder paths, parent-first
    std::vector<std::pair<std::wstring, std::vector<std::wstring>>> planLeaves;
    size_t planLeafIdx = 0;
    bool   buildPhaseFolders = true;     // phase 1 = create folders, phase 2 = songs
    bool   buildWipeFirst = false;

    // Result counters
    int cntFolders = 0, cntFiled = 0, cntUnfiled = 0, cntErrors = 0;

    // Pending "add" selection (driven by the two combos)
    Field      pendingField    = Field::Genre;
    NameMode   pendingNameMode = NameMode::FirstLast;
    GroupScope pendingScope     = GroupScope::All;
    GroupValue pendingValue     = GroupValue::Exact;
    GroupValue pendingGenreValue = GroupValue::Exact;
    YearMode   pendingYearMode  = YearMode::Y10;
    GroupScope pendingYearScope = GroupScope::All;
    bool       fieldChosen      = false;   // a primary field has been picked

    bool       showSettings     = false;
    int        editingIndex     = -1;    // component being edited in place, or -1

    // Window persistence: keep the dialog floating above VDJ's browser even when
    // the user clicks folders (which steals Z-order). A 250ms keep-alive timer
    // re-asserts HWND_TOP while dialogRequestedOpen is set. suppressNextHideSync
    // swallows the WM_SHOWWINDOW(hide) we trigger ourselves so it doesn't clear
    // the "open" intent.
    bool       dialogRequestedOpen  = true;
    bool       suppressNextHideSync = false;

    // Drag-reorder state for the component list
    bool       dragging   = false;
    int        dragTo     = -1;
    POINT      dragStart  = {};

    // Button hover (for owner-draw highlight)
    int        hoverBtnId = -1;

    // ── Win32 handles ──────────────────────────────────────────────────────
    HWND hDlg            = nullptr;
    HWND hComboField     = nullptr;
    HWND hComboMode      = nullptr;
    HWND hBtnAdd         = nullptr;
    HWND hListComponents = nullptr;
    HWND hBtnRemove      = nullptr;
    HWND hBtnClear       = nullptr;
    HWND hEditRoot       = nullptr;
    HWND hBtnScan        = nullptr;
    HWND hBtnBuild       = nullptr;
    HWND hListPreview    = nullptr;
    HWND hBtnClose       = nullptr;
    HWND hBtnSettings    = nullptr;
    HWND hModeTip        = nullptr;   // tooltip for the mode combo
    std::wstring modeTipText;         // backing buffer for the tooltip text

    // Drag-reorder state for the component list
    int  dragFrom        = -1;

    // GDI resources
    HFONT fontNormal = nullptr;
    HFONT fontBold   = nullptr;
    HFONT fontSmall  = nullptr;
    HFONT fontHeader = nullptr;   // section headers
    HFONT fontTitle  = nullptr;
    HBRUSH inputBrush = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Window procedure + helpers
// ─────────────────────────────────────────────────────────────────────────────

LRESULT CALLBACK FoldersWndProc (HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void ensureFoldersWindowClass (HINSTANCE hInst);

STDAPI DllGetClassObject (REFCLSID rclsid, REFIID riid, LPVOID* ppObject);
