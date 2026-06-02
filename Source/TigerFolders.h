#pragma once

#include "TigerFoldersHelpers.h"

#define NODLLEXPORT
#include "vdjPlugin8.h"
#include "vdjDsp8.h"

#include <windowsx.h>
#include <uxtheme.h>
#include <fstream>
#include <map>

// ─────────────────────────────────────────────────────────────────────────────
//  Component data model
// ─────────────────────────────────────────────────────────────────────────────

enum class Field { Genre, Bandleader, Singer, Grouping, Label, Year, Album };

// Display format for Bandleader / Singer.
enum class NameMode { FirstLast, LastFirst, Last, LastUpper, LastUpperFirst };

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
    GroupValue groupValue = GroupValue::Exact;     // Grouping
    YearMode   yearMode   = YearMode::Y10;         // Year
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
    int          depth = 0;
    int          count = 0;   // songs in this node's subtree
    bool         isLeaf = false;
};

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

inline constexpr UINT_PTR TIMER_SOURCE_POLL = 1;

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
inline constexpr int FONT_SMALL  = 11;
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

    // Scan (TigerFoldersScan.cpp)
    void scanSelectedFolder();          // fills `songs`
    void rebuildPreview();              // fills `previewRows` + counts from `songs`

    // Write engine (TigerFoldersWrite.cpp)
    bool managedRootExists() const;
    void removeManagedRoot();
    void buildVirtualFolders();         // writes everything for `songs`
    bool ensureVirtualFolderListFileExists (const std::wstring& listPath);
    void ensureVirtualFolderPathExists (const std::wstring& fullPath,
                                        int& createdLevels, int& existingLevels);
    bool appendSongToVirtualFolderListFile (const std::wstring& listPath,
                                            const std::wstring& songPath) const;

    // UI (TigerFoldersUI.cpp)
    void uiRepopulateModeCombo();       // refill mode combo for current field
    void uiSyncAddButton();             // enable/disable ADD
    void uiRefreshComponentList();
    void uiRefreshPreviewList();
    void uiUpdateStatus (const std::wstring& msg);

    // Parameter IDs
    enum ParamId { PID_OPEN = 0 };
    int paramOpen = 0;

    // ── State ────────────────────────────────────────────────────────────────
    std::vector<Component>   components;
    std::wstring             rootName = L"Tango";

    std::vector<ScannedSong> songs;
    std::vector<PreviewRow>  previewRows;
    int                      previewFolderCount = 0;

    std::wstring             selectedFolderPath;   // currently selected VDJ folder
    std::wstring             statusText;

    fs::path                 settingsPath;

    // Pending "add" selection (driven by the two combos)
    Field      pendingField    = Field::Genre;
    NameMode   pendingNameMode = NameMode::FirstLast;
    GroupScope pendingScope    = GroupScope::All;
    GroupValue pendingValue    = GroupValue::Exact;
    YearMode   pendingYearMode = YearMode::Y10;
    bool       fieldChosen      = false;   // a primary field has been picked

    bool       showSettings     = false;

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

    // Drag-reorder state for the component list
    int  dragFrom        = -1;

    // GDI resources
    HFONT fontNormal = nullptr;
    HFONT fontBold   = nullptr;
    HFONT fontSmall  = nullptr;
    HFONT fontTitle  = nullptr;
    HBRUSH inputBrush = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Window procedure + helpers
// ─────────────────────────────────────────────────────────────────────────────

LRESULT CALLBACK FoldersWndProc (HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void ensureFoldersWindowClass (HINSTANCE hInst);

STDAPI DllGetClassObject (REFCLSID rclsid, REFIID riid, LPVOID* ppObject);
