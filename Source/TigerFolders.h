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

// "Other" small-folder cutoff. None = off; Leaf = only fold the deepest folders
// (the ones that hold songs) whose count is at/below the size; Any = fold a folder
// at any depth whose whole subtree is at/below the size (its songs collapse into a
// sibling "Other").
enum class CutoffMode { None, Leaf, Any };

// Per-component rhythm filter. A component only contributes its subfolder level
// to songs whose normalized rhythm bit is set in the mask; non-matching songs
// skip the level (e.g. a Tango-only Grouping leaves Vals/Milonga ungrouped).
// RHY_OTHER covers Cortina / Other / untagged. RHY_ALL (the default) applies the
// level to every song; it is the only state in which RHY_OTHER is ever set.
enum RhythmBit : unsigned
{
    RHY_TANGO   = 1u,
    RHY_VALS    = 2u,
    RHY_MILONGA = 4u,
    RHY_OTHER   = 8u,
};
inline constexpr unsigned RHY_ALL = RHY_TANGO | RHY_VALS | RHY_MILONGA | RHY_OTHER;

struct Component
{
    Field      field      = Field::Genre;
    NameMode   nameMode   = NameMode::FirstLast;   // Bandleader / Singer
    GroupScope groupScope = GroupScope::All;       // Grouping
    GroupValue genreValue = GroupValue::Exact;     // Genre (Exact / Normalize)
    YearMode   yearMode   = YearMode::Y10;         // Year
    GroupScope yearScope  = GroupScope::All;       // Year (All / Instrumental-only)
    unsigned   rhythmMask = RHY_ALL;               // which rhythms this level applies to
};

bool         fieldHasSubmode (Field f);
std::wstring fieldLabel (Field f);
std::wstring componentModeLabel (const Component& c);   // human-readable mode
std::wstring rhythmSummaryLabel (unsigned mask);        // "All rhythms" / "Tango, Vals"
std::wstring normalizeRhythm (const std::wstring& raw); // genre tag → Tango/Vals/Milonga/…

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

// Cached preview tree, built once per scan/structure change. Expand/collapse and
// song-listing just re-flatten this into `previewRows` without re-walking `songs`.
struct PreviewNode
{
    std::map<std::wstring, PreviewNode> children;             // ordered by name
    int count = 0;                                            // songs in this subtree
    std::vector<std::pair<std::wstring, int>> songEntries;    // direct songs: title + index
};

// Flattened preview-tree row for owner-draw display. A row is either a folder
// node or, when its parent folder is expanded, an individual song title.
struct PreviewRow
{
    std::wstring name;        // folder name, or song title for song rows
    std::wstring path;        // full folder path from the root (exclusion identity);
                              // for a song row, the path of the folder it sits in
    int          depth = 0;
    int          count = 0;   // songs in this node's subtree (folders only)
    bool         isLeaf = false;
    bool         excluded = false;          // user unchecked this folder
    bool         ancestorExcluded = false;  // an ancestor folder is unchecked
    bool         isSong  = false;           // this row is a song title, not a folder
    bool         hasSongs = false;          // folder holds songs directly (expandable)
    int          songIndex = -1;            // index into `songs` for a song row (else -1)
};

// Long-running operation, driven in chunks by a WM_TIMER state machine so the
// UI thread never blocks.
enum class Op { None, Scanning, Building };

// Preview "lens": the normal folder tree, or a flat list of problem songs so the
// DJ can find + fix them. Unfiled = landed at the root (no component matched);
// the others flag missing tags regardless of where the song filed.
enum class PreviewLens { Tree, Unfiled, NoYear, NoGenre, NoArtist };

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
    IDC_COMBO_RHYTHM  = 3113,
    IDC_EDIT_FILTER   = 3114,
};

inline constexpr UINT_PTR TIMER_OP        = 1;   // drives the chunked build phase
inline constexpr UINT_PTR TIMER_KEEPALIVE = 2;   // re-asserts the window on top
inline constexpr UINT_PTR TIMER_SETTLE    = 3;   // waits for recurse_folder to populate
inline constexpr UINT_PTR TIMER_ROOTEDIT  = 4;   // debounces root-name keystrokes

// The scan advances by posting this to itself rather than via WM_TIMER. WM_TIMER
// is coalesced and floored to ~10-15ms, which capped the scan at ~1k rows/sec
// regardless of read speed; a self-posted message runs as fast as rows can be read.
inline constexpr UINT WM_APP_SCANSTEP = WM_APP + 1;

// Tooltip tool IDs for painted areas in the main window (rect-based, not HWND-based).
inline constexpr UINT_PTR ATIP_CUTMODE = 200;
inline constexpr UINT_PTR ATIP_CUTSIZE = 201;
inline constexpr UINT_PTR ATIP_YEARPAD = 202;
inline constexpr UINT_PTR ATIP_SPANISH = 203;
inline constexpr UINT_PTR ATIP_SPLIT   = 204;
inline constexpr UINT_PTR ATIP_REPLACE = 205;

// ─────────────────────────────────────────────────────────────────────────────
//  Layout constants
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr int DLG_W       = 800;
inline constexpr int DLG_H       = 460;
inline constexpr int TITLE_H     = 30;
inline constexpr int PAD         = 10;
inline constexpr int ROW_H       = 28;
inline constexpr int BTN_H       = 28;
inline constexpr int COMP_ITEM_H = 26;
inline constexpr int PREV_ITEM_H = 20;
inline constexpr int LEFT_COL_PCT = 48;

inline constexpr int FONT_NORMAL = 13;
inline constexpr int FONT_SMALL  = 12;
inline constexpr int FONT_HEADER = 11;   // section headers (smaller, darker orange)
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
    std::wstring buildPathFor (const ScannedSong& s) const;       // root/.../leaf (first path)
    // A song normally yields one path; with splitMultiSingers a multi-singer song
    // yields one path per singer (filed under each singer's folder).
    std::vector<std::wstring> buildPathsFor (const ScannedSong& s) const;
    std::wstring effectivePath (const std::wstring& full) const;  // trim at first excluded folder
    void         computeSingerYearRanges();   // fill singerYearRanges for [YY]/[YYYY] modes

    // Map of destination path → folded path for the "Other" cutoff (empty when the
    // cutoff is off). Songs whose path is a key are filed under the folded path.
    std::map<std::wstring, std::wstring> computeOtherFolding() const;

    bool isUnfiled (const ScannedSong& s) const;   // path collapses to just the root

    // Recompute the tag-hygiene counts (qNoYear/qNoGenre/qNoArtist) and cntUnfiled
    // from `songs` against the current structure. Cheap; run on every preview rebuild.
    void computeQualityCounts();

    // Merge-diff: how much a Build would add vs. what already exists on disk.
    struct BuildDiff { int newFolders = 0; int newTracks = 0; int dupTracks = 0; };
    BuildDiff computeBuildDiff() const;    // reads existing .vdjfolder files

    // Preview folder exclusions (checkboxes)
    void applyExclusionFlags();                 // recompute excluded/ancestorExcluded on rows
    void togglePreviewExclusion (int rowIdx);   // check/uncheck a preview folder
    void togglePreviewExpand (int rowIdx);      // expand/collapse a folder's song titles

    // Scan — chunked state machine (TigerFoldersScan.cpp)
    void scanBegin();
    void scanSettleStep();              // wait for recurse_folder to finish flattening
    void scanStep();                    // process one browser row per tick
    void scanFinish();
    // Rebuilds the cached `previewTree` from `songs`, then flattens it. On a fresh
    // rebuild (preserveExpansion=false) the folder hierarchy is expanded to its
    // default (all folders open, song lists collapsed); the expand/collapse toggles
    // pass true so a structure/setting change never leaves a stale expansion.
    void rebuildPreview (bool preserveExpansion = false);
    void flattenPreviewRows();          // previewTree (+ expandedFolders) → previewRows
    void expandAllFolders();            // fill expandedFolders with every folder path

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
    void uiRefreshActionButtons();      // Scan ↔ Back+Build depending on scan state
    void uiRelayout();                  // re-run applyLayout (e.g. after scan/build changes rows)

    // Parameter IDs
    enum ParamId { PID_OPEN = 0 };
    int paramOpen = 0;

    // ── State ────────────────────────────────────────────────────────────────
    std::vector<Component>   components;
    std::wstring             rootName = L"MyLists";

    // When a song has multiple singers (artist tag joined by " and " / " y "),
    // file it under each singer's folder instead of one combined "A, B" folder.
    bool                     splitMultiSingers = false;

    // Build mode: false = append/merge into any existing tree; true = delete the
    // existing same-named virtual folder first, then rebuild it from scratch.
    bool                     replaceExisting = false;

    // Fold Spanish accents (á→a, ñ→n, …) out of folder names. Default off.
    bool                     normalizeSpanish = false;

    // For [YY]/[YYYY] year-range name modes: render a single-year group as a
    // range too ("44-44" instead of "44"). Default off.
    bool                     singleYearRange = false;

    // Order tracks within each folder by recording year (then title) on build and
    // in the preview, instead of leaving them in scan/browser order. Undated tracks
    // sort last. Default off.
    bool                     sortByYear = false;

    // Small-folder "Other" cutoff: fold folders at/below folderCutoffSize songs
    // into a sibling "Other" folder. folderCutoffMode chooses the scope.
    CutoffMode               folderCutoffMode = CutoffMode::None;
    int                      folderCutoffSize = 3;   // clamped to [1, 10] when active

    std::vector<ScannedSong> songs;
    PreviewNode              previewTree;        // cached tree, flattened into rows
    std::vector<PreviewRow>  previewRows;
    int                      previewFolderCount = 0;
    std::set<std::wstring>   excludedFolders;     // full paths of unchecked folders
    std::set<std::wstring>   expandedFolders;     // folder paths whose songs are shown

    // Preview filtering + problem-song lenses.
    std::wstring             previewFilter;                     // folder-name substring filter
    PreviewLens              previewLens = PreviewLens::Tree;    // tree vs. flat problem list
    int                      qNoYear = 0, qNoGenre = 0, qNoArtist = 0;  // tag-hygiene counts
    // Clickable issue-chip rects (rect + PreviewLens as int), cached each paint so
    // the click handler hit-tests exactly what was drawn.
    std::vector<std::pair<RECT, int>> issueChipHits;

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
    GroupValue pendingGenreValue = GroupValue::Exact;
    YearMode   pendingYearMode  = YearMode::Y10;
    GroupScope pendingYearScope = GroupScope::All;
    unsigned   pendingRhythmMask = RHY_ALL;   // rhythm filter for the row being added
    bool       fieldChosen      = false;   // a primary field has been picked

    int        editingIndex     = -1;    // component being edited in place, or -1

    // Window persistence: keep the dialog floating above VDJ's browser even when
    // the user clicks folders (which steals Z-order). A 250ms keep-alive timer
    // re-asserts HWND_TOP while dialogRequestedOpen is set. suppressNextHideSync
    // swallows the WM_SHOWWINDOW(hide) we trigger ourselves so it doesn't clear
    // the "open" intent.
    bool       dialogRequestedOpen  = true;
    bool       suppressNextHideSync = false;

    // The gear (upper-left) opens a settings overlay that describes the tool and
    // hosts the build/naming option toggles. While open the two-column content is
    // hidden and the panel is painted in its place.
    bool       settingsOpen = false;

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
    HWND hComboRhythm    = nullptr;
    bool rhythmListSubclassed = false;   // dropdown listbox subclassed for checkboxes
    HWND hBtnAdd         = nullptr;
    HWND hListComponents = nullptr;
    HWND hBtnRemove      = nullptr;
    HWND hBtnClear       = nullptr;
    HWND hEditRoot       = nullptr;
    HWND hBtnScan        = nullptr;
    HWND hBtnBuild       = nullptr;
    HWND hListPreview    = nullptr;
    HWND hEditFilter     = nullptr;   // preview folder-name filter box
    HWND hBtnClose       = nullptr;
    HWND hBtnSettings    = nullptr;   // gear in the title bar → settings overlay
    HWND hModeTip        = nullptr;   // tooltip for the mode combo
    std::wstring modeTipText;         // backing buffer for the tooltip text
    HWND hSongTip        = nullptr;   // tracking tooltip for expanded song rows
    HWND hAreaTip        = nullptr;   // rect-based tooltips for painted chips + banner checks
    std::wstring songTipText;         // backing buffer for the song tooltip text
    int  songTipRow      = -1;        // preview row the song tooltip is showing for

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
