//==============================================================================
// TigerFolders - Main window UI
// Two-column owner-drawn dark UI. Scan/Build run as a chunked WM_TIMER state
// machine (progress + cancel, never blocks). Components are added via a single
// row, edited in place (double-click), and drag-reordered with an insertion
// indicator.
//==============================================================================

#include "TigerFolders.h"
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

// ─────────────────────────────────────────────────────────────────────────────
//  Field ordering for the field combo (index 0 is a placeholder)
// ─────────────────────────────────────────────────────────────────────────────

static const Field kFieldOrder[] = {
    Field::Genre, Field::Bandleader, Field::Singer, Field::VocalSplit,
    Field::Grouping, Field::Label, Field::Year, Field::Album
};
static constexpr int kFieldCount = (int) (sizeof (kFieldOrder) / sizeof (kFieldOrder[0]));

static TigerFoldersPlugin* getPlugin (HWND hwnd)
{
    return reinterpret_cast<TigerFoldersPlugin*> (GetWindowLongPtrW (hwnd, GWLP_USERDATA));
}

// Forward decls (used by subclasses defined before the handlers)
static void doAdd (TigerFoldersPlugin* p);
static void doRemove (TigerFoldersPlugin* p);

// Move keyboard focus to the next/previous visible+enabled control. The window is
// a WS_POPUP (not a dialog), so Tab navigation is driven manually from each
// control's subclass rather than by IsDialogMessage.
static void cycleFocus (TigerFoldersPlugin* p, bool forward)
{
    HWND ring[] = { p->hComboField, p->hComboMode, p->hComboRhythm, p->hBtnAdd,
                    p->hListComponents, p->hEditRoot, p->hBtnRemove, p->hBtnClear,
                    p->hBtnScan, p->hBtnBuild, p->hEditFilter };
    const int n = (int) (sizeof (ring) / sizeof (ring[0]));
    HWND cur = GetFocus();
    int idx = -1;
    for (int i = 0; i < n; ++i) if (ring[i] == cur) { idx = i; break; }
    for (int step = 0; step < n; ++step)
    {
        idx = (idx + (forward ? 1 : -1) + n) % n;
        HWND h = ring[idx];
        if (h && IsWindowVisible (h) && IsWindowEnabled (h)) { SetFocus (h); return; }
    }
}

// Root-name edit: Tab navigates the focus ring; Escape hides (or cancels an edit).
static LRESULT CALLBACK editSubclass (HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                      UINT_PTR, DWORD_PTR refData)
{
    auto* p = reinterpret_cast<TigerFoldersPlugin*> (refData);
    if (msg == WM_KEYDOWN)
    {
        if (wParam == VK_TAB)
        {
            cycleFocus (p, (GetKeyState (VK_SHIFT) & 0x8000) == 0);
            return 0;
        }
        if (wParam == VK_ESCAPE)
        {
            // In the filter box, Escape first clears the filter; only an empty box
            // hides the dialog (matching the root box's hide-on-Escape).
            if (hwnd == p->hEditFilter && GetWindowTextLengthW (hwnd) > 0)
            {
                SetWindowTextW (hwnd, L"");
                return 0;
            }
            ShowWindow (p->hDlg, SW_HIDE);
            return 0;
        }
    }
    return DefSubclassProc (hwnd, msg, wParam, lParam);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Window class
// ─────────────────────────────────────────────────────────────────────────────

static bool wndClassRegistered = false;

void ensureFoldersWindowClass (HINSTANCE hInst)
{
    if (wndClassRegistered) return;
    WNDCLASSEXW wc {};
    wc.cbSize        = sizeof (wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc   = FoldersWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor (nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = WND_CLASS;
    RegisterClassExW (&wc);
    wndClassRegistered = true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Layout
// ─────────────────────────────────────────────────────────────────────────────

struct Layout
{
    RECT title, brand, settings, close;
    // Settings-overlay rects (the option checkboxes + the description block).
    RECT chkSplit, chkReplace, chkSpanish, chkYearPad, chkSortYear, setDesc;
    RECT addLabel, hdrField, hdrMode, hdrApplies;
    RECT comboField, comboMode, comboRhythm, btnAdd;
    RECT compLabel, structFrame, rootNode, compList, btnRemove, btnClear;
    RECT cutMode, cutSize;
    RECT editRoot, source;
    RECT btnScan, btnBuild, status;
    RECT prevLabel, prevList, prevExpand, prevCollapse;
    RECT prevFilter, prevIssues;   // filter box + tag-issue chip row (0-rect when hidden)
    int  leftW = 0, rightX = 0, rightW = 0;
};

static Layout computeLayout (HWND hwnd)
{
    RECT cr; GetClientRect (hwnd, &cr);
    const int W = cr.right, H = cr.bottom;
    Layout L {};

    L.title    = { 0, 0, W, TITLE_H };
    // Gear sits at the far upper-left; the brand shifts right to make room.
    L.settings = { PAD - 2, 4, PAD - 2 + 24, TITLE_H - 4 };
    L.close    = { W - 30, 4, W - 6, TITLE_H - 4 };
    L.brand    = { L.settings.right + 8, 0, W - 64, TITLE_H };

    // Settings overlay: a description block followed by four option rows. These
    // rects are only used when the gear is toggled (the panel covers the content).
    {
        const int sx = PAD + 6;
        int sy = TITLE_H + PAD + 22;            // leaves room for an "ABOUT" header
        L.setDesc = { sx, sy, W - PAD - 6, sy + 80 };
        int oy = L.setDesc.bottom + 30;         // option rows start (room for header)
        const int rowH = 44, rowW = W - PAD - 6 - sx;
        L.chkSplit    = { sx, oy + 0 * rowH, sx + rowW, oy + 0 * rowH + rowH - 8 };
        L.chkSpanish  = { sx, oy + 1 * rowH, sx + rowW, oy + 1 * rowH + rowH - 8 };
        L.chkYearPad  = { sx, oy + 2 * rowH, sx + rowW, oy + 2 * rowH + rowH - 8 };
        L.chkSortYear = { sx, oy + 3 * rowH, sx + rowW, oy + 3 * rowH + rowH - 8 };
        L.chkReplace  = { sx, oy + 4 * rowH, sx + rowW, oy + 4 * rowH + rowH - 8 };
    }

    L.leftW  = W * LEFT_COL_PCT / 100;
    L.rightX = L.leftW + PAD;
    L.rightW = W - L.rightX - PAD;

    const int lx = PAD;
    const int lr = L.leftW - PAD;
    int y = TITLE_H + PAD;

    L.addLabel  = { lx, y, lr, y + 16 }; y += 22;
    // A single compact row: Field / Mode / Genre-flag / ADD, each combo capped to
    // its content (no full-width stretch) with a small header above it.
    const int fieldW = 92, appliesW = 72, addW = 44, g = 6;
    int hdrY = y;
    int inY  = y + 13;
    L.comboField  = { lx, inY, lx + fieldW, inY + ROW_H };
    L.btnAdd      = { lr - addW, inY, lr, inY + ROW_H };
    L.comboRhythm = { lr - addW - g - appliesW, inY, lr - addW - g, inY + ROW_H };
    L.comboMode   = { lx + fieldW + g, inY, L.comboRhythm.left - g, inY + ROW_H };
    L.hdrField    = { L.comboField.left,  hdrY, L.comboField.right,  hdrY + 12 };
    L.hdrMode     = { L.comboMode.left,   hdrY, L.comboMode.right,   hdrY + 12 };
    L.hdrApplies  = { L.comboRhythm.left, hdrY, L.comboRhythm.right, hdrY + 12 };
    y = inY + ROW_H + 10;

    L.compLabel = { lx, y, lr, y + 16 }; y += 18;

    const int bottomBlock = 8 + 22 /*remove/clear*/ + 8
                          + 8 /*progress bar*/ + 10 + BTN_H /*scan+build*/ + 6 + 16 /*status*/ + PAD;
    int listBottom = H - bottomBlock;
    if (listBottom < y + 60 + COMP_ITEM_H) listBottom = y + 60 + COMP_ITEM_H;
    // The structure reads as a tree: the editable root-name box tops a bordered
    // frame (it IS the input now), the draggable component list fills the rest.
    L.structFrame = { lx, y, lr, listBottom };
    L.rootNode    = { lx, y, lr, y + COMP_ITEM_H };
    L.editRoot    = { lx + 6, y + 3, lr - 6, y + COMP_ITEM_H - 3 };
    L.compList    = { lx, y + COMP_ITEM_H, lr, listBottom };
    y = listBottom + 8;

    L.btnRemove = { lx, y, lx + 84, y + 22 };
    L.btnClear  = { lx + 90, y, lx + 90 + 72, y + 22 };
    // "Other" small-folder cutoff chips, right-aligned on the same row: a mode
    // selector (None / Leaf / Any) and a size stepper (2-10). Clicks cycle them.
    {
        const int szW = 52, mdW = 92, g2 = 6;
        L.cutSize = { lr - szW, y, lr, y + 22 };
        L.cutMode = { lr - szW - g2 - mdW, y, lr - szW - g2, y + 22 };
    }
    y += 22 + 8;

    // Slim progress bar (only painted while scanning/building).
    L.source = { lx, y, lr, y + 8 }; y += 8 + 10;

    L.btnScan  = { lx, y, lx + 130, y + BTN_H };
    L.btnBuild = { lx + 138, y, lr, y + BTN_H };
    y += BTN_H + 6;

    L.status = { lx, y, lr, y + 16 };

    int ry = TITLE_H + PAD;
    {
        const int hdrTop = ry, hdrBot = ry + 16;
        const int caW = 78, eaW = 70, gg = 8;
        L.prevCollapse = { L.rightX + L.rightW - caW, hdrTop, L.rightX + L.rightW, hdrBot };
        L.prevExpand   = { L.prevCollapse.left - gg - eaW, hdrTop, L.prevCollapse.left - gg, hdrBot };
        L.prevLabel    = { L.rightX + 8, hdrTop, L.prevExpand.left - 8, hdrBot };
    }
    ry += 20;

    // Filter box (tree view only) + tag-issue chip row (whenever a scan surfaced
    // issues). Reserving them here shifts the preview list down so nothing overlaps.
    TigerFoldersPlugin* p = getPlugin (hwnd);
    L.prevFilter = { 0, 0, 0, 0 };
    L.prevIssues = { 0, 0, 0, 0 };
    bool scanned  = p && !p->songs.empty();
    bool treeLens = !p || p->previewLens == PreviewLens::Tree;
    if (scanned && treeLens)
    {
        L.prevFilter = { L.rightX, ry, L.rightX + L.rightW, ry + 22 };
        ry += 26;
    }
    if (scanned && (p->cntUnfiled > 0 || p->qNoYear > 0 || p->qNoGenre > 0 || p->qNoArtist > 0))
    {
        L.prevIssues = { L.rightX, ry, L.rightX + L.rightW, ry + 20 };
        ry += 24;
    }

    L.prevList  = { L.rightX, ry, L.rightX + L.rightW, H - PAD };

    return L;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Combos
// ─────────────────────────────────────────────────────────────────────────────

static int modeComboIndex (const Component& c)
{
    switch (c.field)
    {
        case Field::Genre:    return (c.genreValue == GroupValue::Normalize) ? 1 : 0;
        case Field::Bandleader:
        case Field::Singer:
        case Field::VocalSplit: return (int) c.nameMode;
        case Field::Grouping: return (c.groupScope == GroupScope::Instrumental) ? 1 : 0;
        case Field::Year:     {
            int w = (c.yearMode == YearMode::Y2) ? 0 : (c.yearMode == YearMode::Y5) ? 1 : 2;
            return w + (c.yearScope == GroupScope::Instrumental ? 3 : 0);
        }
        default:              return 0;
    }
}

void TigerFoldersPlugin::uiRepopulateModeCombo()
{
    if (!hComboMode) return;
    SendMessageW (hComboMode, CB_RESETCONTENT, 0, 0);

    switch (pendingField)
    {
        case Field::Genre:
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"Exact");
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"Normalize");
            SendMessageW (hComboMode, CB_SETCURSEL, 0, 0);
            pendingGenreValue = GroupValue::Exact;
            break;
        case Field::Bandleader:
        case Field::Singer:
        case Field::VocalSplit:
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"First Last");
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"Last, First");
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"Last");
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"LAST");
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"LAST, First");
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"[YY] Last");
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"[YYYY] Last");
            {
                int def = (pendingField == Field::VocalSplit) ? 2 : 1;   // Last : Last, First
                SendMessageW (hComboMode, CB_SETCURSEL, def, 0);
                pendingNameMode = (NameMode) def;
            }
            break;
        case Field::Grouping:
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"All tracks");
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"Instrumental");
            SendMessageW (hComboMode, CB_SETCURSEL, 0, 0);
            pendingScope = GroupScope::All;
            break;
        case Field::Year:
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"2 years");
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"5 years");
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"10 years (decade)");
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"2 years · Instrumental");
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"5 years · Instrumental");
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"10 years · Instrumental");
            SendMessageW (hComboMode, CB_SETCURSEL, 2, 0);
            pendingYearMode  = YearMode::Y10;
            pendingYearScope = GroupScope::All;
            break;
        default:
            break;
    }
    ShowWindow (hComboMode, fieldHasSubmode (pendingField) ? SW_SHOW : SW_HIDE);

    if (hModeTip)
    {
        switch (pendingField)
        {
            case Field::Bandleader:
            case Field::Singer:
                modeTipText = L"Name format. [YY] = 41-43 Fiorentino, [YYYY] = 1941-1943 "
                              L"Fiorentino (year range prefixed from the parent folder).";
                break;
            case Field::VocalSplit:
                modeTipText = L"Splits into Instrumentals and Singers folders. Names the "
                              L"Singers subfolders, e.g. Singers/Fiorentino or "
                              L"Singers/41-43 Fiorentino ([YY]).";
                break;
            case Field::Genre:
                modeTipText = L"Exact = genre tag as-is. Normalize = "
                              L"Tango / Vals / Milonga / Cortina / Other.";
                break;
            case Field::Grouping:
                modeTipText = L"All = every track. Instrumental = this level applies "
                              L"to instrumentals only.";
                break;
            case Field::Year:
                modeTipText = L"Year bucket width. Instrumental = applies to instrumentals only.";
                break;
            default:
                modeTipText.clear();
                break;
        }
        TOOLINFOW ti {};
        ti.cbSize   = sizeof (ti);
        ti.hwnd     = hDlg;
        ti.uId      = (UINT_PTR) hComboMode;
        ti.lpszText = (LPWSTR) modeTipText.c_str();
        SendMessageW (hModeTip, TTM_UPDATETIPTEXT, 0, (LPARAM) &ti);
    }
}

void TigerFoldersPlugin::uiSyncAddButton()
{
    EnableWindow (hBtnAdd, (fieldChosen && op == Op::None) ? TRUE : FALSE);
    InvalidateRect (hBtnAdd, nullptr, FALSE);
}

static void readPendingMode (TigerFoldersPlugin* p)
{
    int sel = (int) SendMessageW (p->hComboMode, CB_GETCURSEL, 0, 0);
    if (sel < 0) sel = 0;
    switch (p->pendingField)
    {
        case Field::Genre:
            p->pendingGenreValue = (sel == 1) ? GroupValue::Normalize : GroupValue::Exact;
            break;
        case Field::Bandleader:
        case Field::Singer:
        case Field::VocalSplit: p->pendingNameMode = (NameMode) sel; break;
        case Field::Grouping:
            p->pendingScope = (sel == 1) ? GroupScope::Instrumental : GroupScope::All;
            break;
        case Field::Year:
        {
            int w = sel % 3;
            p->pendingYearMode  = (w == 0) ? YearMode::Y2 : (w == 1) ? YearMode::Y5 : YearMode::Y10;
            p->pendingYearScope = (sel >= 3) ? GroupScope::Instrumental : GroupScope::All;
            break;
        }
        default: break;
    }
}

void TigerFoldersPlugin::uiResetAddRow()
{
    editingIndex = -1;
    fieldChosen  = false;
    pendingField = Field::Genre;
    pendingRhythmMask = RHY_ALL;
    SendMessageW (hComboField, CB_SETCURSEL, 0, 0);   // placeholder
    if (hComboRhythm) InvalidateRect (hComboRhythm, nullptr, TRUE);
    uiRepopulateModeCombo();
    SetWindowTextW (hBtnAdd, L"ADD");
    uiSyncAddButton();
}

void TigerFoldersPlugin::uiLoadComponentForEdit (int idx)
{
    if (idx < 0 || idx >= (int) components.size() || op != Op::None) return;
    const Component& c = components[idx];
    editingIndex = idx;
    fieldChosen  = true;
    pendingField = c.field;
    pendingRhythmMask = c.rhythmMask;
    if (hComboRhythm) InvalidateRect (hComboRhythm, nullptr, TRUE);

    // Select field in combo (index+1 because 0 is the placeholder)
    for (int i = 0; i < kFieldCount; ++i)
        if (kFieldOrder[i] == c.field) { SendMessageW (hComboField, CB_SETCURSEL, i + 1, 0); break; }

    uiRepopulateModeCombo();
    if (fieldHasSubmode (c.field))
        SendMessageW (hComboMode, CB_SETCURSEL, modeComboIndex (c), 0);
    readPendingMode (this);

    SetWindowTextW (hBtnAdd, L"Update");
    uiSyncAddButton();
}

// ─────────────────────────────────────────────────────────────────────────────
//  List refresh / status
// ─────────────────────────────────────────────────────────────────────────────

void TigerFoldersPlugin::uiRefreshComponentList()
{
    if (!hListComponents) return;
    int top = (int) SendMessageW (hListComponents, LB_GETTOPINDEX, 0, 0);
    SendMessageW (hListComponents, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < components.size(); ++i)
        SendMessageW (hListComponents, LB_ADDSTRING, 0, (LPARAM) L"");
    SendMessageW (hListComponents, LB_SETTOPINDEX, top, 0);
    // The list hides when empty (so a placeholder can show); reveal it once filled.
    ShowWindow (hListComponents, components.empty() ? SW_HIDE : SW_SHOW);
    InvalidateRect (hListComponents, nullptr, FALSE);
    if (hDlg) InvalidateRect (hDlg, nullptr, FALSE);
}

void TigerFoldersPlugin::uiRefreshPreviewList()
{
    if (!hListPreview) return;
    // Rows are about to change identity; drop any song tooltip showing on one.
    if (hSongTip && songTipRow != -1)
        SendMessageW (hSongTip, TTM_TRACKACTIVATE, FALSE, 0);
    songTipRow = -1;
    SendMessageW (hListPreview, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < previewRows.size(); ++i)
        SendMessageW (hListPreview, LB_ADDSTRING, 0, (LPARAM) L"");
    // scanFinish populates rows without going through applyLayout/applyVisibility,
    // so the preview list must reveal itself here or it stays hidden after a scan.
    ShowWindow (hListPreview, previewRows.empty() ? SW_HIDE : SW_SHOW);
    InvalidateRect (hListPreview, nullptr, FALSE);
    if (hDlg) InvalidateRect (hDlg, nullptr, FALSE);
}

void TigerFoldersPlugin::togglePreviewExclusion (int rowIdx)
{
    if (op != Op::None) return;
    if (rowIdx < 0 || rowIdx >= (int) previewRows.size()) return;
    const std::wstring path = previewRows[rowIdx].path;
    if (path.empty()) return;

    if (excludedFolders.count (path)) excludedFolders.erase (path);
    else                              excludedFolders.insert (path);

    applyExclusionFlags();
    if (hListPreview) InvalidateRect (hListPreview, nullptr, FALSE);
    saveSettings();
}

void TigerFoldersPlugin::togglePreviewExpand (int rowIdx)
{
    if (op != Op::None) return;
    if (rowIdx < 0 || rowIdx >= (int) previewRows.size()) return;
    const PreviewRow& row = previewRows[rowIdx];
    if (row.isSong || row.path.empty()) return;   // every folder row is expandable

    if (expandedFolders.count (row.path)) expandedFolders.erase (row.path);
    else                                  expandedFolders.insert (row.path);

    // Just re-flatten the cached tree (no re-walk of `songs`); keep the scroll
    // position so the clicked row doesn't jump to the top of the list.
    int top = hListPreview ? (int) SendMessageW (hListPreview, LB_GETTOPINDEX, 0, 0) : 0;
    flattenPreviewRows();
    uiRefreshPreviewList();
    if (hListPreview) SendMessageW (hListPreview, LB_SETTOPINDEX, top, 0);
}

void TigerFoldersPlugin::uiUpdateStatus (const std::wstring& msg, bool error)
{
    statusText  = msg;
    statusError = error;
    if (hDlg) InvalidateRect (hDlg, nullptr, FALSE);
}

void TigerFoldersPlugin::uiSetOpRunning (bool running)
{
    BOOL en = running ? FALSE : TRUE;
    EnableWindow (hComboField, en);
    EnableWindow (hComboMode, en);
    EnableWindow (hComboRhythm, en);
    EnableWindow (hBtnRemove, en);
    EnableWindow (hBtnClear, en);
    EnableWindow (hEditRoot, en);
    uiRefreshActionButtons();
    if (running) EnableWindow (hBtnAdd, FALSE); else uiSyncAddButton();

    if (hDlg) InvalidateRect (hDlg, nullptr, FALSE);
}

// The bottom action area is a two-step workflow: before a scan there is just a
// full-width "Scan & Preview"; once a preview exists it becomes "← Back" (clears
// the scan) + "Build". While an op runs the single button reads "Cancel".
void TigerFoldersPlugin::uiRefreshActionButtons()
{
    if (!hBtnScan || !hBtnBuild || !hDlg) return;
    Layout L = computeLayout (hDlg);
    auto mv = [] (HWND h, const RECT& r) {
        if (h) MoveWindow (h, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
    };

    bool running  = (op != Op::None);
    bool haveScan = !songs.empty();
    RECT full = { L.btnScan.left, L.btnScan.top, L.btnBuild.right, L.btnScan.bottom };

    if (!running && haveScan)
    {
        SetWindowTextW (hBtnScan,  L"New Scan");
        // Replace mode is destructive (wipes the existing tree) — say so on the
        // button itself instead of relying on the sticky title-bar toggle.
        SetWindowTextW (hBtnBuild, replaceExisting ? L"Replace" : L"Build");
        mv (hBtnScan, L.btnScan);
        mv (hBtnBuild, L.btnBuild);
        ShowWindow (hBtnScan,  SW_SHOW);
        ShowWindow (hBtnBuild, SW_SHOW);
        EnableWindow (hBtnBuild, TRUE);
    }
    else
    {
        SetWindowTextW (hBtnScan, running ? L"Cancel" : L"Scan & Preview");
        mv (hBtnScan, full);
        ShowWindow (hBtnScan,  SW_SHOW);
        ShowWindow (hBtnBuild, SW_HIDE);
    }
    InvalidateRect (hBtnScan, nullptr, FALSE);
    if (hBtnBuild) InvalidateRect (hBtnBuild, nullptr, FALSE);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Visibility (Settings toggle + empty-state placeholders)
// ─────────────────────────────────────────────────────────────────────────────

static void applyVisibility (TigerFoldersPlugin* p)
{
    if (p->hComboMode)
        ShowWindow (p->hComboMode, fieldHasSubmode (p->pendingField) ? SW_SHOW : SW_HIDE);

    // Lists hide when empty so a placeholder can be painted in their place.
    if (p->hListComponents)
        ShowWindow (p->hListComponents, !p->components.empty() ? SW_SHOW : SW_HIDE);
    if (p->hListPreview)
        ShowWindow (p->hListPreview, !p->previewRows.empty() ? SW_SHOW : SW_HIDE);
    // Filter box only applies to the folder-tree view of an existing scan.
    if (p->hEditFilter)
        ShowWindow (p->hEditFilter,
                    (!p->songs.empty() && p->previewLens == PreviewLens::Tree && !p->settingsOpen)
                        ? SW_SHOW : SW_HIDE);
}

static void applyLayout (HWND hwnd, TigerFoldersPlugin* p)
{
    Layout L = computeLayout (hwnd);
    auto mv = [] (HWND h, const RECT& r) {
        if (h) MoveWindow (h, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
    };
    auto mvCombo = [] (HWND h, const RECT& r) {
        if (h) MoveWindow (h, r.left, r.top, r.right - r.left, (r.bottom - r.top) + 200, TRUE);
    };

    mv (p->hBtnClose, L.close);
    mv (p->hBtnSettings, L.settings);
    mvCombo (p->hComboField, L.comboField);
    mvCombo (p->hComboMode, L.comboMode);
    mvCombo (p->hComboRhythm, L.comboRhythm);
    mv (p->hBtnAdd, L.btnAdd);
    mv (p->hListComponents, L.compList);
    mv (p->hBtnRemove, L.btnRemove);
    mv (p->hBtnClear, L.btnClear);
    mv (p->hEditRoot, L.editRoot);
    // Single-line edits top-align their text against the formatting rectangle;
    // inset it so the text sits vertically centered, matching the "ROOT" label.
    if (p->hEditRoot)
    {
        RECT erc; GetClientRect (p->hEditRoot, &erc);
        int th  = FONT_NORMAL + 5;                       // ~line height for the 13px UI font
        int top = ((erc.bottom - erc.top) - th) / 2;
        if (top < 0) top = 0;
        RECT fmt = { 4, top, erc.right - 2, erc.bottom };
        SendMessageW (p->hEditRoot, EM_SETRECTNP, 0, (LPARAM) &fmt);
    }
    mv (p->hListPreview, L.prevList);
    mv (p->hEditFilter, L.prevFilter);
    if (p->hEditFilter)
    {
        RECT erc; GetClientRect (p->hEditFilter, &erc);
        int th  = FONT_NORMAL + 5;
        int top = ((erc.bottom - erc.top) - th) / 2;
        if (top < 0) top = 0;
        RECT fmt = { 6, top, erc.right - 2, erc.bottom };
        SendMessageW (p->hEditFilter, EM_SETRECTNP, 0, (LPARAM) &fmt);
    }

    p->uiRefreshActionButtons();   // positions + labels Scan / Back / Build per state
    applyVisibility (p);

    // The settings overlay covers the two-column content: hide every content child
    // so only the painted panel (and the title-bar gear/close) shows through.
    if (p->settingsOpen)
    {
        HWND content[] = { p->hComboField, p->hComboMode, p->hComboRhythm, p->hBtnAdd,
                           p->hListComponents, p->hBtnRemove, p->hBtnClear, p->hEditRoot,
                           p->hBtnScan, p->hBtnBuild, p->hListPreview, p->hEditFilter };
        for (HWND h : content) if (h) ShowWindow (h, SW_HIDE);
    }

    // Keep area tooltip rects in sync with the current layout.
    if (p->hAreaTip)
    {
        auto updateTipRect = [&] (UINT_PTR id, const RECT& r) {
            TOOLINFOW ti {}; ti.cbSize = sizeof (ti);
            ti.hwnd = hwnd;
            ti.uId  = id;
            ti.rect = r;
            SendMessageW (p->hAreaTip, TTM_NEWTOOLRECT, 0, (LPARAM) &ti);
        };
        updateTipRect (ATIP_CUTMODE, L.cutMode);
        updateTipRect (ATIP_CUTSIZE, L.cutSize);
    }
}

// Member entry point back into applyLayout — used when a scan/build populates or
// clears rows so the preview list + filter/issue rows reposition to the new state.
void TigerFoldersPlugin::uiRelayout()
{
    if (hDlg) applyLayout (hDlg, this);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Subclasses: component list (drag + keys), combos (arrow + Enter), buttons (hover)
// ─────────────────────────────────────────────────────────────────────────────

static LRESULT CALLBACK compListSubclass (HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                          UINT_PTR, DWORD_PTR refData)
{
    auto* p = reinterpret_cast<TigerFoldersPlugin*> (refData);
    switch (msg)
    {
        case WM_LBUTTONDOWN:
        {
            POINT pt { GET_X_LPARAM (lParam), GET_Y_LPARAM (lParam) };
            DWORD r = (DWORD) SendMessageW (hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM (pt.x, pt.y));
            if (HIWORD (r) == 0)
            {
                p->dragFrom  = LOWORD (r);
                p->dragStart = pt;
                p->dragging  = false;
                SetCapture (hwnd);
            }
            break;
        }
        case WM_MOUSEMOVE:
        {
            if (p->dragFrom >= 0)
            {
                POINT pt { GET_X_LPARAM (lParam), GET_Y_LPARAM (lParam) };
                if (!p->dragging
                    && (abs (pt.y - p->dragStart.y) > 4 || abs (pt.x - p->dragStart.x) > 4))
                    p->dragging = true;
                if (p->dragging)
                {
                    DWORD r = (DWORD) SendMessageW (hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM (pt.x, pt.y));
                    int to = (HIWORD (r) == 0) ? LOWORD (r) : (int) p->components.size() - 1;
                    if (to != p->dragTo) { p->dragTo = to; InvalidateRect (hwnd, nullptr, FALSE); }
                }
            }
            break;
        }
        case WM_LBUTTONUP:
        {
            if (p->dragFrom >= 0)
            {
                ReleaseCapture();
                int from = p->dragFrom, to = p->dragTo;
                bool wasDragging = p->dragging;
                p->dragFrom = -1; p->dragTo = -1; p->dragging = false;
                if (wasDragging && from >= 0 && to >= 0
                    && from < (int) p->components.size() && to < (int) p->components.size()
                    && from != to)
                {
                    Component c = p->components[from];
                    p->components.erase (p->components.begin() + from);
                    p->components.insert (p->components.begin() + to, c);
                    p->uiRefreshComponentList();
                    SendMessageW (hwnd, LB_SETCURSEL, to, 0);
                    if (!p->songs.empty()) { p->rebuildPreview(); p->uiRefreshPreviewList(); }
                    p->saveSettings();
                }
                InvalidateRect (hwnd, nullptr, FALSE);
            }
            break;
        }
        case WM_KEYDOWN:
            if (wParam == VK_TAB)
            {
                cycleFocus (p, (GetKeyState (VK_SHIFT) & 0x8000) == 0);
                return 0;
            }
            if (wParam == VK_DELETE) { doRemove (p); return 0; }
            if (wParam == VK_ESCAPE)
            {
                // While editing a component, Escape cancels the edit (back to ADD);
                // otherwise it hides the dialog.
                if (p->editingIndex >= 0) { p->uiResetAddRow(); applyLayout (p->hDlg, p); }
                else                      ShowWindow (p->hDlg, SW_HIDE);
                return 0;
            }
            break;
    }
    return DefSubclassProc (hwnd, msg, wParam, lParam);
}

// Multi-line metadata block shown in the hover tooltip for an expanded song row.
static std::wstring songTooltipText (const ScannedSong& s)
{
    auto add = [] (std::wstring& t, const wchar_t* label, const std::wstring& val) {
        if (!val.empty()) t += std::wstring (label) + L": " + val + L"\r\n";
    };
    std::wstring title = trimWs (s.title);
    if (title.empty()) title = fs::path (s.filePath).stem().wstring();

    // Ordered for how a tango DJ reads a tanda: orquesta → singer → year → rhythm
    // first, then the rest. File path last (longest, least scannable).
    std::wstring t;
    add (t, L"Title",      title);
    add (t, L"Orquesta",   s.bandleader);
    add (t, L"Singer",     s.instrumental ? std::wstring (L"Instrumental") : s.singer);
    add (t, L"Year",       s.year);
    add (t, L"Rhythm",     s.genre.empty() ? std::wstring() : normalizeRhythm (s.genre));
    add (t, L"Genre",      s.genre);
    add (t, L"Grouping",   s.grouping);
    add (t, L"Album",      s.album);
    add (t, L"Label",      s.label);
    add (t, L"File",       s.filePath);
    while (!t.empty() && (t.back() == L'\n' || t.back() == L'\r')) t.pop_back();
    return t;
}

static void hideSongTip (TigerFoldersPlugin* p)
{
    if (p->hSongTip && p->songTipRow != -1)
        SendMessageW (p->hSongTip, TTM_TRACKACTIVATE, FALSE, 0);
    p->songTipRow = -1;
}

// Show / move the tracking tooltip for the song row under the cursor (client pt).
static void updateSongTip (TigerFoldersPlugin* p, HWND list, POINT pt)
{
    DWORD r = (DWORD) SendMessageW (list, LB_ITEMFROMPOINT, 0, MAKELPARAM (pt.x, pt.y));
    int idx = (HIWORD (r) == 0) ? LOWORD (r) : -1;

    // LB_ITEMFROMPOINT returns the nearest item even for empty space below the last
    // row; confirm the cursor is genuinely inside the item's rect before showing.
    if (idx >= 0)
    {
        RECT ir {};
        if (SendMessageW (list, LB_GETITEMRECT, idx, (LPARAM) &ir) == LB_ERR
            || !PtInRect (&ir, pt))
            idx = -1;
    }

    bool isSongRow = idx >= 0 && idx < (int) p->previewRows.size()
                  && p->previewRows[idx].isSong
                  && p->previewRows[idx].songIndex >= 0
                  && p->previewRows[idx].songIndex < (int) p->songs.size();
    if (!isSongRow) { hideSongTip (p); return; }
    if (idx == p->songTipRow) return;   // already showing for this row

    p->songTipText = songTooltipText (p->songs[p->previewRows[idx].songIndex]);

    TOOLINFOW ti {};
    ti.cbSize   = sizeof (ti);
    ti.uFlags   = TTF_IDISHWND | TTF_TRACK | TTF_ABSOLUTE;
    ti.hwnd     = p->hDlg;
    ti.uId      = (UINT_PTR) list;
    ti.lpszText = (LPWSTR) p->songTipText.c_str();
    SendMessageW (p->hSongTip, TTM_UPDATETIPTEXT, 0, (LPARAM) &ti);

    POINT sp = pt; ClientToScreen (list, &sp);
    SendMessageW (p->hSongTip, TTM_TRACKPOSITION, 0, MAKELPARAM (sp.x + 16, sp.y + 18));
    SendMessageW (p->hSongTip, TTM_TRACKACTIVATE, TRUE, (LPARAM) &ti);
    p->songTipRow = idx;
}

// Preview list clicks: the checkbox gutter (far left) toggles a folder's
// include/exclude; clicking anywhere else on a folder row expands or collapses it
// (child folders + its own song titles). Exclusion is non-destructive and lives
// only on the checkbox, so a plain row click can never drop a branch by mistake.
// Song rows are inert.
static LRESULT CALLBACK previewListSubclass (HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                             UINT_PTR, DWORD_PTR refData)
{
    auto* p = reinterpret_cast<TigerFoldersPlugin*> (refData);
    if (msg == WM_MOUSEMOVE)
    {
        POINT pt { GET_X_LPARAM (lParam), GET_Y_LPARAM (lParam) };
        updateSongTip (p, hwnd, pt);
        TRACKMOUSEEVENT t { sizeof (t), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent (&t);
    }
    else if (msg == WM_MOUSELEAVE)
    {
        hideSongTip (p);
    }
    else if (msg == WM_LBUTTONDOWN)
    {
        POINT pt { GET_X_LPARAM (lParam), GET_Y_LPARAM (lParam) };
        DWORD r = (DWORD) SendMessageW (hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM (pt.x, pt.y));
        if (HIWORD (r) == 0)
        {
            int idx = LOWORD (r);
            if (idx >= 0 && idx < (int) p->previewRows.size())
            {
                const PreviewRow& row = p->previewRows[idx];
                if (row.isSong) return 0;                       // titles aren't interactive

                if (pt.x < 24) p->togglePreviewExclusion (idx); // checkbox gutter
                else           p->togglePreviewExpand (idx);    // anywhere else → expand/collapse
                return 0;
            }
        }
    }
    return DefSubclassProc (hwnd, msg, wParam, lParam);
}

// Apply a click on a rhythm-filter checkbox. Item 0 = "All" (the everything
// default); items 1-3 = Tango / Vals / Milonga. Picking a specific rhythm while
// "All" is active narrows to just that rhythm; clearing the last one reverts to
// "All" so a component never filters out every song.
static void toggleRhythm (TigerFoldersPlugin* p, int item)
{
    if (item == 0) { p->pendingRhythmMask = RHY_ALL; return; }

    unsigned bit = (item == 1) ? RHY_TANGO : (item == 2) ? RHY_VALS : RHY_MILONGA;
    if (p->pendingRhythmMask == RHY_ALL) p->pendingRhythmMask = bit;
    else                                 p->pendingRhythmMask ^= bit;
    p->pendingRhythmMask &= (RHY_TANGO | RHY_VALS | RHY_MILONGA);
    if (p->pendingRhythmMask == 0) p->pendingRhythmMask = RHY_ALL;
}

// The rhythm combo is a multi-check dropdown: a click on a list item toggles that
// rhythm and is swallowed so the dropdown stays open (a real combobox would commit
// the selection and close). Clicks outside an item fall through to close it.
static LRESULT CALLBACK rhythmListSubclass (HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                            UINT_PTR, DWORD_PTR refData)
{
    auto* p = reinterpret_cast<TigerFoldersPlugin*> (refData);
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP)
    {
        POINT pt { GET_X_LPARAM (lParam), GET_Y_LPARAM (lParam) };
        DWORD r = (DWORD) SendMessageW (hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM (pt.x, pt.y));
        if (HIWORD (r) == 0)   // a real item under the cursor → toggle, keep open
        {
            if (msg == WM_LBUTTONDOWN)
            {
                toggleRhythm (p, LOWORD (r));
                InvalidateRect (hwnd, nullptr, FALSE);
                if (p->hComboRhythm) InvalidateRect (p->hComboRhythm, nullptr, TRUE);
            }
            return 0;   // swallow so the combo doesn't commit + close
        }
    }
    return DefSubclassProc (hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK comboSubclass (HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                       UINT_PTR, DWORD_PTR refData)
{
    auto* p = reinterpret_cast<TigerFoldersPlugin*> (refData);
    if (msg == WM_KEYDOWN)
    {
        if (wParam == VK_TAB)
        {
            cycleFocus (p, (GetKeyState (VK_SHIFT) & 0x8000) == 0);
            return 0;
        }
        if (wParam == VK_RETURN) { if (p->fieldChosen && p->op == Op::None) doAdd (p); return 0; }
        if (wParam == VK_ESCAPE)
        {
            if (p->editingIndex >= 0) { p->uiResetAddRow(); applyLayout (p->hDlg, p); }
            else                      ShowWindow (p->hDlg, SW_HIDE);
            return 0;
        }
    }
    LRESULT res = DefSubclassProc (hwnd, msg, wParam, lParam);
    if (msg == WM_PAINT)
    {
        // Overpaint the system drop-arrow with a dark box + accent chevron.
        RECT rc; GetClientRect (hwnd, &rc);
        const int aw = 18;
        RECT ar { rc.right - aw, rc.top, rc.right, rc.bottom };
        HDC dc = GetDC (hwnd);
        fillRect (dc, ar, TCol::inputBg);
        drawText (dc, ar, L"▾", TCol::textNormal, p->fontSmall,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        ReleaseDC (hwnd, dc);
    }
    return res;
}

static LRESULT CALLBACK buttonSubclass (HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                        UINT_PTR, DWORD_PTR refData)
{
    auto* p = reinterpret_cast<TigerFoldersPlugin*> (refData);
    if (msg == WM_MOUSEMOVE)
    {
        int cid = GetDlgCtrlID (hwnd);
        if (p->hoverBtnId != cid) { p->hoverBtnId = cid; InvalidateRect (hwnd, nullptr, FALSE); }
        TRACKMOUSEEVENT t { sizeof (t), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent (&t);
    }
    else if (msg == WM_MOUSELEAVE)
    {
        if (p->hoverBtnId == GetDlgCtrlID (hwnd)) { p->hoverBtnId = -1; InvalidateRect (hwnd, nullptr, FALSE); }
    }
    else if (msg == WM_KEYDOWN && wParam == VK_TAB)
    {
        cycleFocus (p, (GetKeyState (VK_SHIFT) & 0x8000) == 0);
        return 0;
    }
    return DefSubclassProc (hwnd, msg, wParam, lParam);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Owner-draw
// ─────────────────────────────────────────────────────────────────────────────

static void drawButton (TigerFoldersPlugin* p, DRAWITEMSTRUCT* dis)
{
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    int  id = (int) dis->CtlID;
    bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    bool pressed  = (dis->itemState & ODS_SELECTED) != 0;
    bool hover    = (p->hoverBtnId == id) && !disabled;

    wchar_t text[128] = {};
    GetWindowTextW (dis->hwndItem, text, 127);

    // Title-bar icon button: flat, no border, sits on the title panel.
    if (id == IDC_BTN_CLOSE)
    {
        fillRect (hdc, rc, hover ? TCol::buttonHover : TCol::panel);
        COLORREF ic = hover ? RGB (240, 90, 90) : TCol::textNormal;
        drawText (hdc, rc, text, ic, p->fontNormal, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    // Settings gear: flat title-bar icon; lit (accent) while the overlay is open.
    if (id == IDC_BTN_SETTINGS)
    {
        bool lit = hover || p->settingsOpen;
        fillRect (hdc, rc, lit ? TCol::buttonHover : TCol::panel);
        COLORREF ic = lit ? TCol::accentBrt : TCol::textNormal;
        drawText (hdc, rc, text, ic, p->fontNormal, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    // Button labels are uppercased.
    std::wstring label = toUpperW (text);

    // Uniform gray buttons: "active" = simply not grayed out; hover = a lighter
    // gray; pressed = lighter still. The only exception is Scan→Cancel while an op
    // runs, which keeps a red danger tint so it reads as a stop action.
    bool cancel = (id == IDC_BTN_SCAN && p->op != Op::None);
    // Build button in Replace mode wears a warning (amber) skin so the destructive
    // wipe-and-rebuild is visible at the moment of action.
    bool replace = (id == IDC_BTN_BUILD && p->replaceExisting && p->op == Op::None);

    COLORREF bg, border, tc;
    if (disabled)
    {
        bg = TCol::buttonDisabled; border = TCol::cardBorder; tc = TCol::textDim;
    }
    else if (cancel)
    {
        bg = pressed ? RGB (120, 44, 44) : hover ? RGB (96, 44, 44) : RGB (70, 28, 28);
        border = RGB (150, 70, 70); tc = RGB (240, 150, 150);
    }
    else if (replace)
    {
        bg = pressed ? RGB (140, 84, 30) : hover ? RGB (112, 66, 26) : RGB (84, 50, 22);
        border = TCol::accent; tc = TCol::accentBrt;
    }
    else
    {
        bg = pressed ? TCol::selSubtle : hover ? TCol::buttonHover : TCol::buttonBg;
        border = hover ? TCol::inputBorder : TCol::cardBorder;
        tc = TCol::textBright;
    }

    // Paint the corners with the surrounding column color so the rounded edges
    // blend, then clip-fill the rounded body and stroke a 1px rounded border.
    fillRect (hdc, rc, TCol::bg);

    HRGN rgn = CreateRoundRectRgn (rc.left, rc.top, rc.right, rc.bottom, 7, 7);
    SelectClipRgn (hdc, rgn);
    fillRect (hdc, rc, bg);
    SelectClipRgn (hdc, nullptr);
    DeleteObject (rgn);

    HPEN   pen   = CreatePen (PS_SOLID, 1, border);
    HPEN   oldPn = (HPEN)   SelectObject (hdc, pen);
    HBRUSH oldBr = (HBRUSH) SelectObject (hdc, GetStockObject (NULL_BRUSH));
    RoundRect (hdc, rc.left, rc.top, rc.right, rc.bottom, 7, 7);
    SelectObject (hdc, oldPn);
    SelectObject (hdc, oldBr);
    DeleteObject (pen);

    drawText (hdc, rc, label, tc, p->fontNormal,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void drawComboItem (TigerFoldersPlugin* p, DRAWITEMSTRUCT* dis)
{
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    bool sel = (dis->itemState & ODS_SELECTED) != 0;
    bool disabled = (dis->itemState & ODS_DISABLED) != 0;

    // Rhythm filter: a multi-check dropdown. The closed field (ODS_COMBOBOXEDIT)
    // shows a summary of the pending mask; each dropdown row shows a checkbox.
    if (dis->hwndItem == p->hComboRhythm)
    {
        if (dis->itemState & ODS_COMBOBOXEDIT)
        {
            fillRect (hdc, rc, TCol::inputBg);
            RECT tr = rc; tr.left += 7;
            drawText (hdc, tr, rhythmSummaryLabel (p->pendingRhythmMask),
                      disabled ? TCol::textDim : TCol::textBright, p->fontNormal,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            return;
        }

        fillRect (hdc, rc, sel ? TCol::selSubtle : TCol::inputBg);
        int item = (int) dis->itemID;
        unsigned mask = p->pendingRhythmMask;
        bool checked = (item == 0) ? (mask == RHY_ALL)
                     : (item == 1) ? ((mask & RHY_TANGO)   != 0)
                     : (item == 2) ? ((mask & RHY_VALS)    != 0)
                                   : ((mask & RHY_MILONGA) != 0);
        const int cbSize = 13;
        int cy = rc.top + ((rc.bottom - rc.top) - cbSize) / 2;
        RECT cb = { rc.left + 7, cy, rc.left + 7 + cbSize, cy + cbSize };
        if (checked)
        {
            fillRect (hdc, cb, TCol::selSubtle);
            frameRect (hdc, cb, TCol::inputBorder);
            drawText (hdc, cb, L"✓", TCol::textBright, p->fontSmall,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        else
            frameRect (hdc, cb, TCol::textDim);

        static const wchar_t* kNames[] = { L"All", L"Tango", L"Vals", L"Milonga" };
        RECT tr = { rc.left + 7 + cbSize + 8, rc.top, rc.right, rc.bottom };
        drawText (hdc, tr, (item >= 0 && item < 4) ? kNames[item] : L"",
                  TCol::textBright, p->fontNormal, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    fillRect (hdc, rc, sel ? TCol::selSubtle : TCol::inputBg);

    std::wstring text;
    bool placeholder = false;
    if (dis->itemID != (UINT) -1)
    {
        wchar_t buf[128] = {};
        SendMessageW (dis->hwndItem, CB_GETLBTEXT, dis->itemID, (LPARAM) buf);
        text = buf;
        if (dis->hwndItem == p->hComboField && dis->itemID == 0) placeholder = true;
    }
    RECT tr = rc; tr.left += 7;
    COLORREF tc = disabled ? TCol::textDim : placeholder ? TCol::textDim : TCol::textBright;
    drawText (hdc, tr, text, tc, p->fontNormal,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void drawComponentItem (TigerFoldersPlugin* p, DRAWITEMSTRUCT* dis)
{
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    int idx = (int) dis->itemID;
    bool sel = (dis->itemState & ODS_SELECTED) != 0;
    bool editing = (idx == p->editingIndex);

    fillRect (hdc, rc, editing ? RGB (52, 44, 28) : sel ? TCol::selSubtle : TCol::card);
    if (idx < 0 || idx >= (int) p->components.size()) return;
    const Component& c = p->components[idx];

    // Drag grip stays at a fixed left edge so every row is easy to grab. Gutter
    // glyphs use fontSmall to match the preview list's chevrons / bullets / ♪.
    RECT grip = { rc.left + 6, rc.top, rc.left + 20, rc.bottom };
    drawText (hdc, grip, L"≡", TCol::textDim, p->fontSmall, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Indent by depth so the chain reads like a nested folder structure. The first
    // component sits at the base indent (a direct child of the root row above), so
    // there is no extra root-level offset.
    int indent  = idx * 16;
    int branchX = rc.left + 24 + indent;
    RECT branch = { branchX, rc.top, branchX + 16, rc.bottom };
    drawText (hdc, branch, L"└", TCol::textDim, p->fontSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Primary choice (the field) is bold + bright; the accessory mode text (name
    // style / year width / rhythm flag, …) trails it in dim gray.
    RECT tr = { branchX + 16, rc.top, rc.right - 8, rc.bottom };
    std::wstring primary   = fieldLabel (c.field);
    std::wstring accessory = componentModeLabel (c);

    HFONT oldF = (HFONT) SelectObject (hdc, p->fontBold);
    SIZE psz {}; GetTextExtentPoint32W (hdc, primary.c_str(), (int) primary.size(), &psz);
    SelectObject (hdc, oldF);

    drawText (hdc, tr, primary, TCol::textBright, p->fontBold,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT ar = { tr.left + psz.cx + 14, tr.top, tr.right, tr.bottom };
    if (ar.left < ar.right)
        drawText (hdc, ar, accessory, TCol::textMuted, p->fontNormal,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT sep = { rc.left, rc.bottom - 1, rc.right, rc.bottom };
    fillRect (hdc, sep, TCol::cardBorder);

    // Drag insertion indicator
    if (p->dragging && idx == p->dragTo)
    {
        RECT line2 = { rc.left, rc.top, rc.right, rc.top + 2 };
        fillRect (hdc, line2, TCol::accentBrt);
    }
}

static void drawPreviewItem (TigerFoldersPlugin* p, DRAWITEMSTRUCT* dis)
{
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    int idx = (int) dis->itemID;
    fillRect (hdc, rc, TCol::panel);
    if (idx < 0 || idx >= (int) p->previewRows.size()) return;

    const PreviewRow& row = p->previewRows[idx];
    bool dim = row.excluded || row.ancestorExcluded;
    // Song name column is reserved one indent step right of its folder, past the
    // checkbox + chevron gutter (38px) plus a level per depth.
    int indent = 38 + row.depth * 16;

    // Song rows: a quiet, indented title with a small note glyph. No checkbox or
    // count — they are just the contents of the folder above.
    if (row.isSong)
    {
        RECT tr = { rc.left + indent, rc.top, rc.right - 8, rc.bottom };
        drawText (hdc, tr, L"♪  " + row.name, dim ? TCol::textDim : TCol::textNormal,
                  p->fontSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }

    bool ownChecked = !row.excluded;

    // Include/exclude checkbox at a fixed left edge (click to toggle).
    const int cbSize = 13;
    int cy = rc.top + ((rc.bottom - rc.top) - cbSize) / 2;
    RECT cb = { rc.left + 6, cy, rc.left + 6 + cbSize, cy + cbSize };
    if (ownChecked)
    {
        // Checked = subtle gray highlight + a bright checkmark (no orange fill).
        fillRect (hdc, cb, TCol::selSubtle);
        frameRect (hdc, cb, TCol::inputBorder);
        drawText (hdc, cb, L"✓", TCol::textBright, p->fontSmall,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    else
        frameRect (hdc, cb, TCol::textDim);

    // Expand/collapse chevron — every folder row is expandable (its child folders
    // and/or its own song titles). Left edge at indent-14 (x=24 at depth 0) so it
    // never dips into the checkbox click zone (pt.x < 24 toggles include/exclude).
    {
        // While a filter is active the matched spine is force-expanded, so show
        // every visible folder as open regardless of its stored expand state.
        bool expanded = !p->previewFilter.empty() || p->expandedFolders.count (row.path) > 0;
        RECT chev = { rc.left + indent - 14, rc.top, rc.left + indent - 2, rc.bottom };
        drawText (hdc, chev, expanded ? L"▾" : L"▸",
                  dim ? TCol::textDim : TCol::textNormal, p->fontSmall,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    RECT tr = { rc.left + indent, rc.top, rc.right - 46, rc.bottom };
    std::wstring name = (row.isLeaf ? L"• " : L"") + row.name;
    COLORREF nameCol = dim ? TCol::textDim
                     : row.isLeaf ? TCol::textNormal : TCol::textBright;
    // The first level (top-level folders) is bold so the tree's spine stands out.
    HFONT nameFont = (row.depth == 0) ? p->fontBold : p->fontNormal;
    drawText (hdc, tr, name, nameCol, nameFont,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    // A directly-excluded folder is struck through ("you turned this off"), which
    // reads differently from a row merely dimmed because an ancestor is excluded.
    if (row.excluded)
    {
        HFONT oldF = (HFONT) SelectObject (hdc, nameFont);
        SIZE ns {}; GetTextExtentPoint32W (hdc, name.c_str(), (int) name.size(), &ns);
        SelectObject (hdc, oldF);
        int sx2 = tr.left + ns.cx; if (sx2 > tr.right) sx2 = tr.right;
        int sy  = tr.top + (tr.bottom - tr.top) / 2;
        RECT strike = { tr.left, sy, sx2, sy + 1 };
        fillRect (hdc, strike, TCol::textDim);
    }

    RECT cnt = { rc.right - 46, rc.top, rc.right - 6, rc.bottom };
    drawText (hdc, cnt, L"(" + std::to_wstring (row.count) + L")", TCol::textMuted,
              p->fontSmall, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Painting
// ─────────────────────────────────────────────────────────────────────────────

// Settings overlay: a short description of the tool followed by the build/naming
// option toggles that used to crowd the title bar. Drawn over the whole content
// area (the content children are hidden by applyLayout while this is open).
static void paintSettings (HDC mem, TigerFoldersPlugin* p, const Layout& L, const RECT& cr)
{
    RECT body = { 0, TITLE_H, cr.right, cr.bottom };
    fillRect (mem, body, TCol::bg);

    // "About" header + word-wrapped description of what the plugin does.
    RECT aboutHdr = { L.setDesc.left, L.setDesc.top - 20, L.setDesc.right, L.setDesc.top - 4 };
    drawText (mem, aboutHdr, L"ABOUT", TCol::textMuted, p->fontHeader,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    drawText (mem, L.setDesc,
              L"TigerFolders builds VirtualDJ virtual folders automatically from your "
              L"song tags. Select a browser folder in VirtualDJ, choose how to group "
              L"tracks (genre, orquesta, singer, year…), preview the tree, then Build "
              L"to create the folders.",
              TCol::textNormal, p->fontNormal, DT_LEFT | DT_TOP | DT_WORDBREAK);

    // "Options" header above the toggle rows.
    RECT optHdr = { L.chkSplit.left, L.chkSplit.top - 22, L.chkSplit.right, L.chkSplit.top - 6 };
    drawText (mem, optHdr, L"OPTIONS", TCol::textMuted, p->fontHeader,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    auto drawOpt = [&] (const RECT& row, bool on, const std::wstring& label,
                        const std::wstring& desc) {
        const int cbSize = 16;
        RECT cb = { row.left, row.top + 1, row.left + cbSize, row.top + 1 + cbSize };
        if (on)
        {
            fillRect (mem, cb, TCol::selSubtle);
            frameRect (mem, cb, TCol::accent);
            drawText (mem, cb, L"✓", TCol::accentBrt, p->fontSmall,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        else
            frameRect (mem, cb, TCol::inputBorder);

        RECT lr = { row.left + cbSize + 12, row.top, row.right, row.top + 18 };
        drawText (mem, lr, label, on ? TCol::textBright : TCol::textNormal,
                  p->fontBold, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT dr = { row.left + cbSize + 12, row.top + 18, row.right, row.top + 35 };
        drawText (mem, dr, desc, TCol::textMuted, p->fontSmall,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    };

    drawOpt (L.chkSplit,   p->splitMultiSingers, L"Split singers",
             L"File multi-singer songs under each singer's own folder.");
    drawOpt (L.chkSpanish, p->normalizeSpanish,  L"Normalize Spanish",
             L"Fold accented characters to ASCII (á→a, ñ→n) in folder names.");
    drawOpt (L.chkYearPad, p->singleYearRange,   L"Pad years",
             L"Show a single-year group as a range, e.g. 1944–1944.");
    drawOpt (L.chkSortYear, p->sortByYear,       L"Sort tracks by year",
             L"Order tracks within each folder chronologically (undated last).");
    drawOpt (L.chkReplace, p->replaceExisting,   L"Replace existing",
             L"On Build, delete the existing tree first, then rebuild from scratch.");
}

static void paintWindow (HWND hwnd, TigerFoldersPlugin* p)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint (hwnd, &ps);
    RECT cr; GetClientRect (hwnd, &cr);
    Layout L = computeLayout (hwnd);

    HDC mem = CreateCompatibleDC (hdc);
    HBITMAP bmp = CreateCompatibleBitmap (hdc, cr.right, cr.bottom);
    HBITMAP oldBmp = (HBITMAP) SelectObject (mem, bmp);

    fillRect (mem, cr, TCol::bg);

    // Title bar
    fillRect (mem, L.title, TCol::panel);
    RECT tsep = { 0, TITLE_H - 1, cr.right, TITLE_H };
    fillRect (mem, tsep, TCol::cardBorder);
    drawText (mem, L.brand, L"TigerFolders", TCol::accentBrt, p->fontTitle,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (p->settingsOpen)
    {
        paintSettings (mem, p, L, cr);
        BitBlt (hdc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
        SelectObject (mem, oldBmp);
        DeleteObject (bmp);
        DeleteDC (mem);
        EndPaint (hwnd, &ps);
        return;
    }

    {
        // Right-column panel surface + frame to separate the two columns
        RECT rPanel = { L.rightX - 2, TITLE_H + 4, cr.right - 4, cr.bottom - 4 };
        fillRect (mem, rPanel, TCol::panel);
        frameRect (mem, rPanel, TCol::cardBorder);

        drawText (mem, L.addLabel, L"ADD COMPONENT", TCol::textMuted, p->fontHeader,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        drawText (mem, L.compLabel, L"STRUCTURE", TCol::textMuted, p->fontHeader,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        // Discoverability hint (the three core interactions aren't otherwise obvious):
        // top row is the root, drag to reorder the nesting, double-click to edit.
        drawText (mem, L.compLabel,
                  p->components.empty() ? L"top = root folder"
                                        : L"drag to reorder · double-click to edit",
                  TCol::textDim, p->fontSmall,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        // Small headers above each add-row input.
        drawText (mem, L.hdrField, L"FIELD", TCol::textDim, p->fontSmall,
                  DT_LEFT | DT_BOTTOM | DT_SINGLELINE);
        if (fieldHasSubmode (p->pendingField))
            drawText (mem, L.hdrMode, L"MODE", TCol::textDim, p->fontSmall,
                      DT_LEFT | DT_BOTTOM | DT_SINGLELINE);
        drawText (mem, L.hdrApplies, L"RHYTHM", TCol::textDim, p->fontSmall,
                  DT_LEFT | DT_BOTTOM | DT_SINGLELINE);

        // Structure frame: the editable root-name box (hEditRoot) sits in the top
        // node and IS the root input; the component list fills the rest.
        frameRect (mem, L.structFrame, TCol::cardBorder);
        {
            frameRect (mem, L.editRoot, TCol::inputBorder);   // outline the root edit
            RECT sep = { L.rootNode.left, L.rootNode.bottom - 1, L.rootNode.right, L.rootNode.bottom };
            fillRect (mem, sep, TCol::cardBorder);
        }
        if (p->components.empty())
            drawText (mem, L.compList, L"Pick a field above and click ADD",
                      TCol::textDim, p->fontSmall, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // "Other" small-folder cutoff chips (mode + size), clicked to cycle.
        {
            auto chip = [&] (const RECT& r, const std::wstring& txt, bool active, bool enabled) {
                fillRect (mem, r, TCol::card);
                frameRect (mem, r, active ? TCol::inputBorder : TCol::cardBorder);
                COLORREF tc = !enabled ? TCol::textDim
                            : active   ? TCol::textBright : TCol::textNormal;
                drawText (mem, r, txt, tc, p->fontSmall, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            };
            bool on = (p->folderCutoffMode != CutoffMode::None);
            const wchar_t* mlab = (p->folderCutoffMode == CutoffMode::Leaf) ? L"Other: Leaf"
                                : (p->folderCutoffMode == CutoffMode::Any)  ? L"Other: Any"
                                                                            : L"Other: Off";
            chip (L.cutMode, mlab, on, true);
            chip (L.cutSize, L"≤ " + std::to_wstring (p->folderCutoffSize), on, on);
        }

        // Slim progress bar while an op runs (idle: blank).
        if (p->op != Op::None)
        {
            RECT bar = L.source;
            fillRect (mem, bar, TCol::card);
            frameRect (mem, bar, TCol::cardBorder);
            int total = p->opTotal > 0 ? p->opTotal : 1;
            int done  = (p->op == Op::Scanning)
                        ? p->opIndex
                        : (p->buildPhaseFolders ? p->opIndex
                           : (int) (p->planFolders.size() + p->planLeafIdx));
            if (done > total) done = total;
            RECT fillr = bar;
            fillr.right = bar.left + (LONG) ((bar.right - bar.left) * (double) done / total);
            fillr.left += 1; fillr.top += 1; fillr.bottom -= 1;
            if (fillr.right > fillr.left) fillRect (mem, fillr, TCol::accent);
        }

        if (!p->statusText.empty())
            drawText (mem, L.status, p->statusText,
                      p->statusError ? RGB (240, 120, 110) : TCol::good,
                      p->fontSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        // Right-column header: "PREVIEW" + count / lens info in muted gray.
        drawText (mem, L.prevLabel, L"PREVIEW", TCol::textMuted, p->fontHeader,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (!p->songs.empty())
        {
            HFONT oldHf = (HFONT) SelectObject (mem, p->fontHeader);
            SIZE sz {}; GetTextExtentPoint32W (mem, L"PREVIEW", 7, &sz);
            SelectObject (mem, oldHf);
            std::wstring info;
            if (p->previewLens == PreviewLens::Tree)
                info = L"  ·  " + std::to_wstring (p->songs.size()) + L" songs → "
                     + std::to_wstring (p->previewFolderCount) + L" folders";
            else
            {
                const wchar_t* ln = (p->previewLens == PreviewLens::Unfiled) ? L"unfiled"
                                  : (p->previewLens == PreviewLens::NoYear)  ? L"missing year"
                                  : (p->previewLens == PreviewLens::NoGenre) ? L"missing genre"
                                                                             : L"missing artist";
                info = L"  ·  " + std::to_wstring (p->previewRows.size())
                     + L" tracks · " + ln;
            }
            RECT ir = L.prevLabel; ir.left += sz.cx;
            drawText (mem, ir, info, TCol::textNormal, p->fontHeader,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

            // Expand-all / Collapse-all links (folder spine only, not in a lens).
            if (p->previewLens == PreviewLens::Tree)
            {
                drawText (mem, L.prevExpand,   L"Expand all",   TCol::textNormal, p->fontSmall,
                          DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                drawText (mem, L.prevCollapse, L"Collapse all", TCol::textNormal, p->fontSmall,
                          DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            }
        }

        // Filter-box outline (the child EDIT paints its own text over this).
        if (L.prevFilter.right > L.prevFilter.left)
            frameRect (mem, L.prevFilter, TCol::inputBorder);

        // Tag-issue chips: click one to swap the preview to a flat list of those
        // problem songs (so tags can be fixed); click the active chip to go back.
        p->issueChipHits.clear();
        if (L.prevIssues.right > L.prevIssues.left)
        {
            struct Cat { PreviewLens lens; const wchar_t* label; int count; };
            Cat cats[] = {
                { PreviewLens::Unfiled,  L"Unfiled",   p->cntUnfiled },
                { PreviewLens::NoYear,   L"No year",   p->qNoYear },
                { PreviewLens::NoGenre,  L"No genre",  p->qNoGenre },
                { PreviewLens::NoArtist, L"No artist", p->qNoArtist },
            };
            RECT warn = { L.prevIssues.left, L.prevIssues.top,
                          L.prevIssues.left + 16, L.prevIssues.bottom };
            drawText (mem, warn, L"⚠", TCol::accentBrt, p->fontSmall,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            int x = warn.right + 4;
            for (const auto& c : cats)
            {
                if (c.count <= 0) continue;
                std::wstring t = std::wstring (c.label) + L" " + std::to_wstring (c.count);
                HFONT oldF = (HFONT) SelectObject (mem, p->fontSmall);
                SIZE ts {}; GetTextExtentPoint32W (mem, t.c_str(), (int) t.size(), &ts);
                SelectObject (mem, oldF);
                RECT chip = { x, L.prevIssues.top, x + ts.cx + 16, L.prevIssues.bottom };
                if (chip.right > L.prevIssues.right) break;   // out of room
                bool active = (p->previewLens == c.lens);
                fillRect (mem, chip, active ? TCol::selSubtle : TCol::card);
                frameRect (mem, chip, active ? TCol::accent : TCol::cardBorder);
                drawText (mem, chip, t, active ? TCol::accentBrt : TCol::textNormal,
                          p->fontSmall, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                p->issueChipHits.push_back ({ chip, (int) c.lens });
                x = chip.right + 6;
            }
        }

        frameRect (mem, L.prevList, TCol::cardBorder);
        if (p->previewRows.empty())
        {
            std::wstring empty = p->songs.empty()
                ? L"Select a folder in VirtualDJ\nand click Scan & Preview"
                : (!trimWs (p->previewFilter).empty() ? L"No folders match the filter"
                                                      : L"No matching songs");
            drawText (mem, L.prevList, empty,
                      TCol::textDim, p->fontSmall, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        }
    }

    BitBlt (hdc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
    SelectObject (mem, oldBmp);
    DeleteObject (bmp);
    DeleteDC (mem);
    EndPaint (hwnd, &ps);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Actions
// ─────────────────────────────────────────────────────────────────────────────

static void doAdd (TigerFoldersPlugin* p)
{
    if (!p->fieldChosen || p->op != Op::None) return;
    readPendingMode (p);

    Component c;
    c.field      = p->pendingField;
    c.nameMode   = p->pendingNameMode;
    c.groupScope = p->pendingScope;
    c.genreValue = p->pendingGenreValue;
    c.yearMode   = p->pendingYearMode;
    c.yearScope  = p->pendingYearScope;
    c.rhythmMask = p->pendingRhythmMask;

    if (p->editingIndex >= 0 && p->editingIndex < (int) p->components.size())
        p->components[p->editingIndex] = c;     // edit in place
    else
        p->components.push_back (c);

    p->uiResetAddRow();
    applyLayout (p->hDlg, p);                    // show/hide list + mode combo
    p->uiRefreshComponentList();
    if (!p->songs.empty()) { p->rebuildPreview(); p->uiRefreshPreviewList(); }
    p->saveSettings();
}

static void doRemove (TigerFoldersPlugin* p)
{
    if (p->op != Op::None) return;
    int sel = (int) SendMessageW (p->hListComponents, LB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int) p->components.size()) return;
    p->components.erase (p->components.begin() + sel);
    if (p->editingIndex == sel) p->uiResetAddRow();
    applyLayout (p->hDlg, p);
    p->uiRefreshComponentList();
    if (!p->songs.empty()) { p->rebuildPreview(); p->uiRefreshPreviewList(); }
    p->saveSettings();
}

static void doClear (TigerFoldersPlugin* p)
{
    if (p->op != Op::None || p->components.empty()) return;
    if (MessageBoxW (p->hDlg, L"Clear the entire structure?", L"TigerFolders",
                     MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
        return;
    p->components.clear();
    p->uiResetAddRow();
    applyLayout (p->hDlg, p);
    p->uiRefreshComponentList();
    if (!p->songs.empty()) { p->rebuildPreview(); p->uiRefreshPreviewList(); }
    p->saveSettings();
}

static void doBuild (TigerFoldersPlugin* p)
{
    if (p->op != Op::None) return;
    if (p->components.empty()) { p->uiUpdateStatus (L"Add at least one component first", true); return; }
    if (p->songs.empty())      { p->uiUpdateStatus (L"Scan a folder first", true); return; }

    // The banner "Replace existing" toggle decides the build mode: replace deletes
    // the same-named tree first; append (default) merges into whatever is there.
    bool wipe = p->replaceExisting;
    if (wipe && p->managedRootExists())
    {
        std::wstring root = sanitizeSegment (p->rootName);
        std::wstring text =
            L"Replace mode: this will DELETE the existing \"" + root +
            L"\" virtual folder tree, then rebuild it from scratch.\n\nContinue?";
        if (MessageBoxW (p->hDlg, text.c_str(), L"TigerFolders — Replace",
                         MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
        {
            p->uiUpdateStatus (L"Build cancelled");
            return;
        }
    }
    else if (!wipe && p->managedRootExists())
    {
        // Merge into an existing tree: show exactly what this Build would add
        // (new folders / new tracks) vs. what's already present, before writing.
        p->buildPlan();
        TigerFoldersPlugin::BuildDiff d = p->computeBuildDiff();
        if (d.newFolders == 0 && d.newTracks == 0)
        {
            p->uiUpdateStatus (L"Already up to date · nothing new to merge");
            return;
        }
        std::wstring root = sanitizeSegment (p->rootName);
        std::wstring text =
            L"Merge into the existing \"" + root + L"\" tree:\n\n"
            L"    + " + std::to_wstring (d.newFolders) + L" new folders\n"
            L"    + " + std::to_wstring (d.newTracks) + L" new tracks\n"
            L"    " + std::to_wstring (d.dupTracks) + L" already present (skipped)\n\n"
            L"Continue?";
        if (MessageBoxW (p->hDlg, text.c_str(), L"TigerFolders — Merge",
                         MB_OKCANCEL | MB_ICONINFORMATION) != IDOK)
        {
            p->uiUpdateStatus (L"Build cancelled");
            return;
        }
    }
    p->buildBegin (wipe);
}

// Clear the preview filter + any active problem-song lens (on a fresh scan/back).
static void resetPreviewView (TigerFoldersPlugin* p)
{
    p->previewLens   = PreviewLens::Tree;
    p->previewFilter.clear();
    if (p->hEditFilter) SetWindowTextW (p->hEditFilter, L"");
}

// Switch the preview between the folder tree and a flat problem-song lens. Clicking
// the active lens returns to the tree. Repositions (the filter row only exists in
// tree view) and re-flattens the cached tree.
static void setPreviewLens (TigerFoldersPlugin* p, PreviewLens lens)
{
    if (p->op != Op::None) return;
    p->previewLens = (p->previewLens == lens) ? PreviewLens::Tree : lens;
    applyLayout (p->hDlg, p);
    p->flattenPreviewRows();
    p->uiRefreshPreviewList();
    InvalidateRect (p->hDlg, nullptr, FALSE);
}

// "← Back" — discard the scan/preview and return to the Scan step.
static void doBack (TigerFoldersPlugin* p)
{
    if (p->op != Op::None) return;
    p->songs.clear();
    p->previewRows.clear();
    p->previewFolderCount = 0;
    p->expandedFolders.clear();
    p->cntUnfiled = 0;
    resetPreviewView (p);
    p->uiRefreshPreviewList();
    p->uiUpdateStatus (L"");
    applyLayout (p->hDlg, p);     // repositions buttons back to a single full-width Scan
}

// ─────────────────────────────────────────────────────────────────────────────
//  Control creation
// ─────────────────────────────────────────────────────────────────────────────

static void createControls (HWND hwnd, TigerFoldersPlugin* p, HINSTANCE hInst)
{
    auto mkButton = [&] (int id, const wchar_t* text) -> HWND {
        HWND h = CreateWindowExW (0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 10, 10, hwnd, (HMENU) (intptr_t) id, hInst, nullptr);
        SetWindowSubclass (h, buttonSubclass, 10, (DWORD_PTR) p);
        return h;
    };

    p->hBtnClose    = mkButton (IDC_BTN_CLOSE, L"✕");
    p->hBtnSettings = mkButton (IDC_BTN_SETTINGS, L"⚙");   // gear → settings overlay

    p->hComboField = CreateWindowExW (0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL,
        0, 0, 10, 200, hwnd, (HMENU) IDC_COMBO_FIELD, hInst, nullptr);
    p->hComboMode = CreateWindowExW (0, L"COMBOBOX", L"",
        WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL,
        0, 0, 10, 200, hwnd, (HMENU) IDC_COMBO_MODE, hInst, nullptr);
    SetWindowTheme (p->hComboField, L"", L"");
    SetWindowTheme (p->hComboMode, L"", L"");
    SetWindowSubclass (p->hComboField, comboSubclass, 11, (DWORD_PTR) p);
    SetWindowSubclass (p->hComboMode, comboSubclass, 11, (DWORD_PTR) p);

    SendMessageW (p->hComboField, CB_ADDSTRING, 0, (LPARAM) L"— field —");
    for (int i = 0; i < kFieldCount; ++i)
        SendMessageW (p->hComboField, CB_ADDSTRING, 0, (LPARAM) fieldLabel (kFieldOrder[i]).c_str());
    SendMessageW (p->hComboField, CB_SETCURSEL, 0, 0);

    // Rhythm filter — a multi-check dropdown (All / Tango / Vals / Milonga). Its
    // dropdown listbox is subclassed so item clicks toggle checks instead of
    // committing a selection and closing.
    p->hComboRhythm = CreateWindowExW (0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
        0, 0, 10, 200, hwnd, (HMENU) IDC_COMBO_RHYTHM, hInst, nullptr);
    SetWindowTheme (p->hComboRhythm, L"", L"");
    SetWindowSubclass (p->hComboRhythm, comboSubclass, 11, (DWORD_PTR) p);
    SendMessageW (p->hComboRhythm, CB_ADDSTRING, 0, (LPARAM) L"All");
    SendMessageW (p->hComboRhythm, CB_ADDSTRING, 0, (LPARAM) L"Tango");
    SendMessageW (p->hComboRhythm, CB_ADDSTRING, 0, (LPARAM) L"Vals");
    SendMessageW (p->hComboRhythm, CB_ADDSTRING, 0, (LPARAM) L"Milonga");
    SendMessageW (p->hComboRhythm, CB_SETCURSEL, 0, 0);
    {
        COMBOBOXINFO cbi { sizeof (cbi) };
        if (GetComboBoxInfo (p->hComboRhythm, &cbi) && cbi.hwndList)
        {
            SetWindowSubclass (cbi.hwndList, rhythmListSubclass, 20, (DWORD_PTR) p);
            p->rhythmListSubclassed = true;
        }
    }

    p->hBtnAdd = mkButton (IDC_BTN_ADD, L"ADD");
    EnableWindow (p->hBtnAdd, FALSE);

    p->hListComponents = CreateWindowExW (0, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_OWNERDRAWFIXED | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        0, 0, 10, 10, hwnd, (HMENU) IDC_LIST_COMPONENTS, hInst, nullptr);
    SetWindowSubclass (p->hListComponents, compListSubclass, 1, (DWORD_PTR) p);

    p->hBtnRemove = mkButton (IDC_BTN_REMOVE, L"Remove");
    p->hBtnClear  = mkButton (IDC_BTN_CLEAR, L"Clear");

    p->hEditRoot = CreateWindowExW (0, L"EDIT", p->rootName.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 10, 10, hwnd, (HMENU) IDC_EDIT_ROOT, hInst, nullptr);
    SetWindowSubclass (p->hEditRoot, editSubclass, 12, (DWORD_PTR) p);
    SendMessageW (p->hEditRoot, WM_SETFONT, (WPARAM) p->fontNormal, TRUE);
    // Cue when blank — this top box is the tree's root folder name.
    SendMessageW (p->hEditRoot, EM_SETCUEBANNER, TRUE, (LPARAM) L"Root folder name (e.g. MyLists)");

    p->hBtnScan  = mkButton (IDC_BTN_SCAN, L"Scan & Preview");
    p->hBtnBuild = mkButton (IDC_BTN_BUILD, L"Build");

    p->hListPreview = CreateWindowExW (0, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_OWNERDRAWFIXED | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        0, 0, 10, 10, hwnd, (HMENU) IDC_LIST_PREVIEW, hInst, nullptr);
    SetWindowSubclass (p->hListPreview, previewListSubclass, 2, (DWORD_PTR) p);

    // Preview folder-name filter (hidden until a scan exists; reuses editSubclass
    // for Tab / Escape). Colors come from the generic WM_CTLCOLOREDIT handler.
    p->hEditFilter = CreateWindowExW (0, L"EDIT", L"",
        WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 10, 10, hwnd, (HMENU) IDC_EDIT_FILTER, hInst, nullptr);
    SetWindowSubclass (p->hEditFilter, editSubclass, 13, (DWORD_PTR) p);
    SendMessageW (p->hEditFilter, WM_SETFONT, (WPARAM) p->fontNormal, TRUE);
    SendMessageW (p->hEditFilter, EM_SETCUEBANNER, TRUE, (LPARAM) L"Filter folders…");

    // Tooltip on the mode combo (text is refreshed per field in uiRepopulateModeCombo).
    p->hModeTip = CreateWindowExW (WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0, 0, hwnd, nullptr, hInst, nullptr);
    if (p->hModeTip)
    {
        TOOLINFOW ti {};
        ti.cbSize   = sizeof (ti);
        ti.uFlags   = TTF_IDISHWND | TTF_SUBCLASS;
        ti.hwnd     = hwnd;
        ti.uId      = (UINT_PTR) p->hComboMode;
        ti.lpszText = (LPWSTR) L"";
        SendMessageW (p->hModeTip, TTM_ADDTOOL, 0, (LPARAM) &ti);

        // A second (static-text) tool on the same tooltip control for the rhythm
        // combo, explaining what the per-level rhythm filter does.
        TOOLINFOW tr {};
        tr.cbSize   = sizeof (tr);
        tr.uFlags   = TTF_IDISHWND | TTF_SUBCLASS;
        tr.hwnd     = hwnd;
        tr.uId      = (UINT_PTR) p->hComboRhythm;
        tr.lpszText = (LPWSTR) L"Apply this level only to the chosen rhythms. "
                               L"\"All\" files every track; pick Tango/Vals/Milonga to "
                               L"group only those (e.g. a Tango-only Singer level leaves "
                               L"Vals and Milonga ungrouped).";
        SendMessageW (p->hModeTip, TTM_ADDTOOL, 0, (LPARAM) &tr);

        SendMessageW (p->hModeTip, TTM_SETMAXTIPWIDTH, 0, 360);
        // Strip the visual style so the custom bg/text colors take effect (the Aero
        // tooltip theme otherwise ignores TTM_SETTIPBKCOLOR).
        SetWindowTheme (p->hModeTip, L"", L"");
        SendMessageW (p->hModeTip, TTM_SETTIPBKCOLOR,   (WPARAM) TCol::card, 0);
        SendMessageW (p->hModeTip, TTM_SETTIPTEXTCOLOR, (WPARAM) TCol::textBright, 0);
    }

    // Tracking tooltip for expanded song rows: shows the song's metadata while the
    // cursor hovers a title (driven manually from the preview list's WM_MOUSEMOVE).
    p->hSongTip = CreateWindowExW (WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0, 0, hwnd, nullptr, hInst, nullptr);
    if (p->hSongTip)
    {
        TOOLINFOW ti {};
        ti.cbSize   = sizeof (ti);
        ti.uFlags   = TTF_IDISHWND | TTF_TRACK | TTF_ABSOLUTE;
        ti.hwnd     = hwnd;
        ti.uId      = (UINT_PTR) p->hListPreview;
        ti.lpszText = (LPWSTR) L"";
        SendMessageW (p->hSongTip, TTM_ADDTOOL, 0, (LPARAM) &ti);
        SendMessageW (p->hSongTip, TTM_SETMAXTIPWIDTH, 0, 600);   // enables multi-line text
        SetWindowTheme (p->hSongTip, L"", L"");                   // let custom colors apply
        SendMessageW (p->hSongTip, TTM_SETTIPBKCOLOR,   (WPARAM) TCol::card, 0);
        SendMessageW (p->hSongTip, TTM_SETTIPTEXTCOLOR, (WPARAM) TCol::textBright, 0);
        RECT tipMargin { 8, 6, 8, 6 };
        SendMessageW (p->hSongTip, TTM_SETMARGIN, 0, (LPARAM) &tipMargin);
    }

    // Rect-based tooltips for painted areas: "Other" cutoff chips + banner checkboxes.
    p->hAreaTip = CreateWindowExW (WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0, 0, hwnd, nullptr, hInst, nullptr);
    if (p->hAreaTip)
    {
        auto addTool = [&] (UINT_PTR id, const wchar_t* text) {
            TOOLINFOW ti {}; ti.cbSize = sizeof (ti);
            ti.uFlags   = TTF_SUBCLASS;
            ti.hwnd     = hwnd;
            ti.uId      = id;
            ti.lpszText = (LPWSTR) text;
            SetRect (&ti.rect, 0, 0, 1, 1);   // placeholder — updated by applyLayout
            SendMessageW (p->hAreaTip, TTM_ADDTOOL, 0, (LPARAM) &ti);
        };
        addTool (ATIP_CUTMODE,
            L"Fold small folders into an 'Other' folder.\n"
            L"Leaf – fold only the deepest (song-container) folders.\n"
            L"Any – fold any folder whose whole subtree is small.\n"
            L"Click to cycle Off → Leaf → Any.");
        addTool (ATIP_CUTSIZE,
            L"Maximum song count to fold into 'Other'.\n"
            L"Left-click to increase, right-click to decrease.");
        // The naming/build-mode toggles (Pad years · Normalize Spanish · Split
        // singers · Replace) now live in the settings overlay (the gear), where each
        // carries its own inline description, so they no longer need title-bar tips.
        SendMessageW (p->hAreaTip, TTM_SETMAXTIPWIDTH, 0, 380);
        SetWindowTheme (p->hAreaTip, L"", L"");
        SendMessageW (p->hAreaTip, TTM_SETTIPBKCOLOR,   (WPARAM) TCol::card, 0);
        SendMessageW (p->hAreaTip, TTM_SETTIPTEXTCOLOR, (WPARAM) TCol::textBright, 0);
    }

    p->uiRepopulateModeCombo();
    p->uiRefreshComponentList();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Window procedure
// ─────────────────────────────────────────────────────────────────────────────

LRESULT CALLBACK FoldersWndProc (HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_CREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*> (lParam);
        auto* p = reinterpret_cast<TigerFoldersPlugin*> (cs->lpCreateParams);
        SetWindowLongPtrW (hwnd, GWLP_USERDATA, (LONG_PTR) p);
        p->hDlg = hwnd;
        createControls (hwnd, p, cs->hInstance);
        applyLayout (hwnd, p);
        p->dialogRequestedOpen  = true;
        p->suppressNextHideSync = false;
        SetTimer (hwnd, TIMER_KEEPALIVE, 250, nullptr);
        return 0;
    }

    auto* p = getPlugin (hwnd);
    if (!p) return DefWindowProcW (hwnd, msg, wParam, lParam);

    switch (msg)
    {
        case WM_APP_SCANSTEP:
            if (p->op == Op::Scanning && !p->scanSettling) p->scanStep();
            return 0;

        case WM_TIMER:
            if (wParam == TIMER_OP)
            {
                if (p->op == Op::Building) p->buildStep();
            }
            else if (wParam == TIMER_SETTLE)
            {
                if (p->op == Op::Scanning && p->scanSettling) p->scanSettleStep();
            }
            else if (wParam == TIMER_ROOTEDIT)
            {
                // Debounced root-name commit: rebuild the preview + persist once.
                KillTimer (hwnd, TIMER_ROOTEDIT);
                if (!p->songs.empty()) { p->rebuildPreview(); p->uiRefreshPreviewList(); }
                p->saveSettings();
            }
            else if (wParam == TIMER_KEEPALIVE)
            {
                // Re-assert the dialog above VDJ's browser. Clicking a folder in
                // VDJ steals the Z-order; this puts us back on top (without
                // stealing focus) so the window never disappears behind VDJ.
                if (p->dialogRequestedOpen)
                {
                    HWND owner = GetWindow (hwnd, GW_OWNER);
                    bool vdjMinimised = owner && IsIconic (owner);
                    if (!vdjMinimised)
                    {
                        if (!IsWindowVisible (hwnd))
                            ShowWindow (hwnd, SW_SHOWNOACTIVATE);
                        SetWindowPos (hwnd, HWND_TOP, 0, 0, 0, 0,
                                      SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                    }
                    else if (IsWindowVisible (hwnd))
                    {
                        p->suppressNextHideSync = true;
                        ShowWindow (hwnd, SW_HIDE);
                    }
                }
            }
            return 0;

        case WM_ACTIVATE:
            // Losing activation (clicking VDJ, another app) should dismiss the song
            // tooltip — it's a topmost popup that would otherwise hang over VDJ.
            if (LOWORD (wParam) == WA_INACTIVE) hideSongTip (p);
            break;

        case WM_SHOWWINDOW:
            // Keep dialogRequestedOpen in sync with real visibility, but ignore
            // the hide we trigger ourselves (e.g. when VDJ is minimised).
            if (wParam)                       p->dialogRequestedOpen = true;
            else if (p->suppressNextHideSync) p->suppressNextHideSync = false;
            else                              p->dialogRequestedOpen = false;
            return 0;

        case WM_SIZE:
            applyLayout (hwnd, p);
            InvalidateRect (hwnd, nullptr, FALSE);
            return 0;

        case WM_GETMINMAXINFO:
        {
            auto* mmi = reinterpret_cast<MINMAXINFO*> (lParam);
            mmi->ptMinTrackSize.x = 680;
            mmi->ptMinTrackSize.y = 420;
            return 0;
        }

        case WM_NCHITTEST:
        {
            // Borderless popup: synthesize resize grips on the left/right/bottom
            // edges and the two bottom corners. The top edge is left alone — it
            // holds the brand, toggles and close button (and the caption drag).
            POINT pt { GET_X_LPARAM (lParam), GET_Y_LPARAM (lParam) };
            ScreenToClient (hwnd, &pt);
            RECT cr; GetClientRect (hwnd, &cr);
            const int M = 6;
            bool left   = pt.x < M;
            bool right  = pt.x >= cr.right - M;
            bool bottom = pt.y >= cr.bottom - M;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (bottom && left)  return HTBOTTOMLEFT;
            if (right)           return HTRIGHT;
            if (left)            return HTLEFT;
            if (bottom)          return HTBOTTOM;
            return HTCLIENT;
        }

        case WM_PAINT:
            paintWindow (hwnd, p);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_MEASUREITEM:
        {
            auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*> (lParam);
            if (mis->CtlType == ODT_COMBOBOX) mis->itemHeight = 22;
            else if (mis->CtlID == IDC_LIST_COMPONENTS) mis->itemHeight = COMP_ITEM_H;
            else if (mis->CtlID == IDC_LIST_PREVIEW)     mis->itemHeight = PREV_ITEM_H;
            return TRUE;
        }

        case WM_DRAWITEM:
        {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*> (lParam);
            if (dis->CtlType == ODT_BUTTON)        drawButton (p, dis);
            else if (dis->CtlType == ODT_COMBOBOX) drawComboItem (p, dis);
            else if (dis->CtlID == IDC_LIST_COMPONENTS) drawComponentItem (p, dis);
            else if (dis->CtlID == IDC_LIST_PREVIEW)    drawPreviewItem (p, dis);
            return TRUE;
        }

        case WM_CTLCOLOREDIT:
        {
            HDC dc = (HDC) wParam;
            SetTextColor (dc, TCol::textBright);
            SetBkColor (dc, TCol::inputBg);
            return (LRESULT) p->inputBrush;
        }
        case WM_CTLCOLORLISTBOX:
        {
            HDC dc = (HDC) wParam;
            SetTextColor (dc, TCol::textBright);
            SetBkColor (dc, TCol::panel);
            SetDCBrushColor (dc, TCol::panel);
            return (LRESULT) GetStockObject (DC_BRUSH);
        }

        case WM_COMMAND:
        {
            int id   = LOWORD (wParam);
            int code = HIWORD (wParam);

            if (id == IDC_COMBO_FIELD && code == CBN_SELCHANGE)
            {
                int sel = (int) SendMessageW (p->hComboField, CB_GETCURSEL, 0, 0);
                p->fieldChosen = (sel > 0);
                if (p->fieldChosen) p->pendingField = kFieldOrder[sel - 1];
                if (p->editingIndex >= 0) { p->editingIndex = -1; SetWindowTextW (p->hBtnAdd, L"ADD"); }
                p->uiRepopulateModeCombo();
                applyLayout (hwnd, p);
                p->uiSyncAddButton();
                return 0;
            }
            if (id == IDC_COMBO_MODE && code == CBN_SELCHANGE) { readPendingMode (p); return 0; }
            if (id == IDC_COMBO_RHYTHM && code == CBN_DROPDOWN && !p->rhythmListSubclassed)
            {
                // Belt-and-suspenders: subclass the dropdown listbox here too, in
                // case GetComboBoxInfo at creation time didn't yet have the list.
                COMBOBOXINFO cbi { sizeof (cbi) };
                if (GetComboBoxInfo (p->hComboRhythm, &cbi) && cbi.hwndList)
                {
                    SetWindowSubclass (cbi.hwndList, rhythmListSubclass, 20, (DWORD_PTR) p);
                    p->rhythmListSubclassed = true;
                }
                return 0;
            }
            if (id == IDC_EDIT_ROOT && code == EN_CHANGE)
            {
                wchar_t buf[256] = {};
                GetWindowTextW (p->hEditRoot, buf, 255);
                p->rootName = trimWs (buf);
                // Debounce the expensive preview rebuild + settings write: coalesce
                // rapid keystrokes into one update ~300ms after typing stops, so a
                // large library doesn't re-walk on every character (and the INI isn't
                // rewritten to disk per keystroke).
                SetTimer (hwnd, TIMER_ROOTEDIT, 300, nullptr);
                return 0;
            }
            if (id == IDC_EDIT_FILTER && code == EN_CHANGE)
            {
                wchar_t buf[128] = {};
                GetWindowTextW (p->hEditFilter, buf, 127);
                p->previewFilter = trimWs (buf);
                // Re-flatten the cached tree (no re-walk of songs) and repaint.
                p->flattenPreviewRows();
                p->uiRefreshPreviewList();
                return 0;
            }
            if (id == IDC_LIST_COMPONENTS && code == LBN_DBLCLK)
            {
                int sel = (int) SendMessageW (p->hListComponents, LB_GETCURSEL, 0, 0);
                p->uiLoadComponentForEdit (sel);
                applyLayout (hwnd, p);
                return 0;
            }
            if (code == BN_CLICKED)
            {
                switch (id)
                {
                    case IDC_BTN_ADD:    doAdd (p); return 0;
                    case IDC_BTN_REMOVE: doRemove (p); return 0;
                    case IDC_BTN_CLEAR:  doClear (p); return 0;
                    case IDC_BTN_SCAN:
                        if (p->op != Op::None)        p->opCancel = true;  // Cancel
                        else if (!p->songs.empty())   doBack (p);          // ← Back
                        else                          p->scanBegin();      // Scan & Preview
                        return 0;
                    case IDC_BTN_BUILD:  doBuild (p); return 0;
                    case IDC_BTN_SETTINGS:
                        p->settingsOpen = !p->settingsOpen;
                        // Drop any song tooltip that might be tracking over a now-hidden list.
                        hideSongTip (p);
                        applyLayout (hwnd, p);   // hides/shows content children for the overlay
                        InvalidateRect (hwnd, nullptr, FALSE);
                        return 0;
                    case IDC_BTN_CLOSE:
                        p->dialogRequestedOpen  = false;
                        p->suppressNextHideSync = true;
                        ShowWindow (hwnd, SW_HIDE);
                        return 0;
                }
            }
            return 0;
        }

        case WM_LBUTTONDOWN:
        {
            POINT pt { GET_X_LPARAM (lParam), GET_Y_LPARAM (lParam) };
            Layout L = computeLayout (hwnd);

            // Settings overlay: its four option checkboxes are painted (not child
            // windows), so they're hit-tested here. A click anywhere else in the
            // panel is swallowed; the title strip still drags the window.
            if (p->settingsOpen)
            {
                // Rebuild the preview after a toggle that changes folder names/structure.
                auto toggleNaming = [&] (bool& flag) {
                    flag = !flag;
                    if (!p->songs.empty()) { p->rebuildPreview(); p->uiRefreshPreviewList(); }
                    p->saveSettings();
                    InvalidateRect (hwnd, nullptr, FALSE);
                };
                if (p->op == Op::None && PtInRect (&L.chkSplit, pt))   { toggleNaming (p->splitMultiSingers); return 0; }
                if (p->op == Op::None && PtInRect (&L.chkSpanish, pt)) { toggleNaming (p->normalizeSpanish);  return 0; }
                if (p->op == Op::None && PtInRect (&L.chkYearPad, pt)) { toggleNaming (p->singleYearRange);   return 0; }
                if (p->op == Op::None && PtInRect (&L.chkSortYear, pt)) { toggleNaming (p->sortByYear);      return 0; }
                if (p->op == Op::None && PtInRect (&L.chkReplace, pt))
                {
                    // Build-mode only: the hidden action buttons relabel on overlay close.
                    p->replaceExisting = !p->replaceExisting;
                    p->saveSettings();
                    InvalidateRect (hwnd, nullptr, FALSE);
                    return 0;
                }
                if (pt.y < TITLE_H)
                {
                    ReleaseCapture();
                    SendMessageW (hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                }
                return 0;
            }

            // "Other" cutoff chips: mode cycles None→Leaf→Any; size cycles 2..10.
            if (p->op == Op::None && PtInRect (&L.cutMode, pt))
            {
                p->folderCutoffMode = (p->folderCutoffMode == CutoffMode::None) ? CutoffMode::Leaf
                                    : (p->folderCutoffMode == CutoffMode::Leaf) ? CutoffMode::Any
                                                                                : CutoffMode::None;
                if (!p->songs.empty()) { p->rebuildPreview(); p->uiRefreshPreviewList(); }
                p->saveSettings();
                InvalidateRect (hwnd, nullptr, FALSE);
                return 0;
            }
            if (p->op == Op::None && p->folderCutoffMode != CutoffMode::None
                && PtInRect (&L.cutSize, pt))
            {
                p->folderCutoffSize = (p->folderCutoffSize >= 10) ? 1 : p->folderCutoffSize + 1;
                if (!p->songs.empty()) { p->rebuildPreview(); p->uiRefreshPreviewList(); }
                p->saveSettings();
                InvalidateRect (hwnd, nullptr, FALSE);
                return 0;
            }

            // Tag-issue chips: toggle the matching problem-song lens (or back to tree).
            if (p->op == Op::None && !p->songs.empty())
            {
                for (const auto& hit : p->issueChipHits)
                    if (PtInRect (&hit.first, pt))
                    {
                        setPreviewLens (p, (PreviewLens) hit.second);
                        return 0;
                    }
            }

            // Preview header: Expand-all / Collapse-all (folder spine, tree view only).
            if (p->op == Op::None && p->previewLens == PreviewLens::Tree
                && !p->previewRows.empty() && PtInRect (&L.prevExpand, pt))
            {
                p->expandAllFolders();
                p->flattenPreviewRows();
                p->uiRefreshPreviewList();
                return 0;
            }
            if (p->op == Op::None && p->previewLens == PreviewLens::Tree
                && !p->previewRows.empty() && PtInRect (&L.prevCollapse, pt))
            {
                p->expandedFolders.clear();
                p->flattenPreviewRows();
                p->uiRefreshPreviewList();
                return 0;
            }

            if (pt.y < TITLE_H)
            {
                ReleaseCapture();
                SendMessageW (hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                return 0;
            }
            return 0;
        }

        case WM_RBUTTONDOWN:
        {
            POINT pt { GET_X_LPARAM (lParam), GET_Y_LPARAM (lParam) };
            Layout L = computeLayout (hwnd);
            if (!p->settingsOpen && p->op == Op::None && p->folderCutoffMode != CutoffMode::None
                && PtInRect (&L.cutSize, pt))
            {
                p->folderCutoffSize = (p->folderCutoffSize <= 1) ? 10 : p->folderCutoffSize - 1;
                if (!p->songs.empty()) { p->rebuildPreview(); p->uiRefreshPreviewList(); }
                p->saveSettings();
                InvalidateRect (hwnd, nullptr, FALSE);
                return 0;
            }
            return 0;
        }

        case WM_CLOSE:
            p->dialogRequestedOpen  = false;
            p->suppressNextHideSync = true;
            ShowWindow (hwnd, SW_HIDE);
            return 0;

        case WM_DESTROY:
            KillTimer (hwnd, TIMER_KEEPALIVE);
            // Flush any pending debounced root-name edit so it isn't lost on close.
            KillTimer (hwnd, TIMER_ROOTEDIT);
            p->saveSettings();
            if (p->op != Op::None)
            {
                KillTimer (hwnd, TIMER_OP);
                KillTimer (hwnd, TIMER_SETTLE);
                p->op = Op::None;
            }
            if (p->hListComponents) RemoveWindowSubclass (p->hListComponents, compListSubclass, 1);
            if (p->hListPreview)    RemoveWindowSubclass (p->hListPreview, previewListSubclass, 2);
            if (p->hEditRoot)       RemoveWindowSubclass (p->hEditRoot, editSubclass, 12);
            if (p->hEditFilter)     RemoveWindowSubclass (p->hEditFilter, editSubclass, 13);
            // Buttons (id 10) and combos (id 11) all hold a refData pointer to the
            // plugin; drop them before the plugin can be freed.
            for (HWND b : { p->hBtnClose, p->hBtnSettings, p->hBtnAdd, p->hBtnRemove,
                            p->hBtnClear, p->hBtnScan, p->hBtnBuild })
                if (b) RemoveWindowSubclass (b, buttonSubclass, 10);
            for (HWND c : { p->hComboField, p->hComboMode, p->hComboRhythm })
                if (c) RemoveWindowSubclass (c, comboSubclass, 11);
            // The rhythm combo's dropdown listbox was subclassed separately (id 20).
            if (p->hComboRhythm)
            {
                COMBOBOXINFO cbi { sizeof (cbi) };
                if (GetComboBoxInfo (p->hComboRhythm, &cbi) && cbi.hwndList)
                    RemoveWindowSubclass (cbi.hwndList, rhythmListSubclass, 20);
            }
            if (p->hSongTip) { DestroyWindow (p->hSongTip); p->hSongTip = nullptr; }
            if (p->hAreaTip) { DestroyWindow (p->hAreaTip); p->hAreaTip = nullptr; }
            p->hDlg = nullptr;
            return 0;
    }
    return DefWindowProcW (hwnd, msg, wParam, lParam);
}
