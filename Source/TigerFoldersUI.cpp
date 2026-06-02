//==============================================================================
// TigerFolders - Main window UI
// FoldersWndProc: WM_CREATE, WM_PAINT, WM_DRAWITEM, WM_COMMAND, drag-reorder
//==============================================================================

#include "TigerFolders.h"
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

// ─────────────────────────────────────────────────────────────────────────────
//  Field ordering for the field combo (index 0 is a placeholder)
// ─────────────────────────────────────────────────────────────────────────────

static const Field kFieldOrder[] = {
    Field::Genre, Field::Bandleader, Field::Singer,
    Field::Grouping, Field::Label, Field::Year, Field::Album
};
static constexpr int kFieldCount = (int) (sizeof (kFieldOrder) / sizeof (kFieldOrder[0]));

static TigerFoldersPlugin* getPlugin (HWND hwnd)
{
    return reinterpret_cast<TigerFoldersPlugin*> (GetWindowLongPtrW (hwnd, GWLP_USERDATA));
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
    RECT addLabel, comboField, comboMode, btnAdd;
    RECT compLabel, compList, btnRemove, btnClear;
    RECT rootLabel, editRoot, sourceLabel, source;
    RECT btnScan, btnBuild, status;
    RECT prevLabel, prevList;
    int  leftW = 0, rightX = 0, rightW = 0;
};

static Layout computeLayout (HWND hwnd)
{
    RECT cr; GetClientRect (hwnd, &cr);
    const int W = cr.right, H = cr.bottom;
    Layout L {};

    L.title    = { 0, 0, W, TITLE_H };
    L.close    = { W - 30, 4, W - 6, TITLE_H - 4 };
    L.settings = { W - 60, 4, W - 34, TITLE_H - 4 };
    L.brand    = { PAD, 0, W - 64, TITLE_H };

    L.leftW  = W * LEFT_COL_PCT / 100;
    L.rightX = L.leftW + PAD;
    L.rightW = W - L.rightX - PAD;

    const int lx = PAD;
    const int lr = L.leftW - PAD;     // left column right edge
    int y = TITLE_H + PAD;

    L.addLabel  = { lx, y, lr, y + 16 }; y += 18;
    L.comboField = { lx, y, lx + 116, y + ROW_H };
    L.comboMode  = { lx + 122, y, lx + 122 + 150, y + ROW_H };
    L.btnAdd     = { lr - 62, y, lr, y + ROW_H };
    y += ROW_H + 8;

    L.compLabel = { lx, y, lr, y + 16 }; y += 18;

    // Component list grows to leave room for the controls below it.
    const int bottomBlock = 8 + 16 + ROW_H /*root*/ + 6 + 16 + 18 /*source*/
                          + 10 + BTN_H /*scan+build*/ + 6 + 16 /*status*/ + PAD;
    int listBottom = H - bottomBlock;
    if (listBottom < y + 60) listBottom = y + 60;
    L.compList = { lx, y, lr, listBottom };
    y = listBottom + 6;

    L.btnRemove = { lx, y, lx + 80, y + 22 };
    L.btnClear  = { lx + 86, y, lx + 86 + 70, y + 22 };
    y += 22 + 8;

    L.rootLabel = { lx, y, lx + 44, y + ROW_H };
    L.editRoot  = { lx + 48, y, lr, y + ROW_H };
    y += ROW_H + 6;

    L.sourceLabel = { lx, y, lr, y + 16 }; y += 16;
    L.source      = { lx, y, lr, y + 18 }; y += 18 + 10;

    L.btnScan  = { lx, y, lx + 130, y + BTN_H };
    L.btnBuild = { lx + 138, y, lr, y + BTN_H };
    y += BTN_H + 6;

    L.status = { lx, y, lr, y + 16 };

    // Right column
    int ry = TITLE_H + PAD;
    L.prevLabel = { L.rightX, ry, L.rightX + L.rightW, ry + 16 }; ry += 18;
    L.prevList  = { L.rightX, ry, L.rightX + L.rightW, H - PAD };

    return L;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Combos
// ─────────────────────────────────────────────────────────────────────────────

void TigerFoldersPlugin::uiRepopulateModeCombo()
{
    if (!hComboMode) return;
    SendMessageW (hComboMode, CB_RESETCONTENT, 0, 0);

    switch (pendingField)
    {
        case Field::Bandleader:
        case Field::Singer:
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"First Last");
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"Last, First");
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"Last");
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"LAST");
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"LAST, First");
            SendMessageW (hComboMode, CB_SETCURSEL, 0, 0);
            pendingNameMode = NameMode::FirstLast;
            break;
        case Field::Grouping:
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"All · Exact");
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"All · Normalize");
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"Instrumental · Exact");
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"Instrumental · Normalize");
            SendMessageW (hComboMode, CB_SETCURSEL, 0, 0);
            pendingScope = GroupScope::All;
            pendingValue = GroupValue::Exact;
            break;
        case Field::Year:
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"2 years");
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"5 years");
            SendMessageW (hComboMode, CB_ADDSTRING, 0, (LPARAM) L"10 years (decade)");
            SendMessageW (hComboMode, CB_SETCURSEL, 2, 0);   // default decade
            pendingYearMode = YearMode::Y10;
            break;
        default:
            break;   // no submode
    }

    ShowWindow (hComboMode, fieldHasSubmode (pendingField) ? SW_SHOW : SW_HIDE);
}

void TigerFoldersPlugin::uiSyncAddButton()
{
    EnableWindow (hBtnAdd, fieldChosen ? TRUE : FALSE);
    InvalidateRect (hBtnAdd, nullptr, FALSE);
}

// Pull the pending mode from the mode combo's current selection.
static void readPendingMode (TigerFoldersPlugin* p)
{
    int sel = (int) SendMessageW (p->hComboMode, CB_GETCURSEL, 0, 0);
    if (sel < 0) sel = 0;
    switch (p->pendingField)
    {
        case Field::Bandleader:
        case Field::Singer:
            p->pendingNameMode = (NameMode) sel;
            break;
        case Field::Grouping:
            p->pendingScope = (sel >= 2) ? GroupScope::Instrumental : GroupScope::All;
            p->pendingValue = (sel % 2 == 1) ? GroupValue::Normalize : GroupValue::Exact;
            break;
        case Field::Year:
            p->pendingYearMode = (sel == 0) ? YearMode::Y2 : (sel == 1) ? YearMode::Y5 : YearMode::Y10;
            break;
        default:
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  List refresh
// ─────────────────────────────────────────────────────────────────────────────

void TigerFoldersPlugin::uiRefreshComponentList()
{
    if (!hListComponents) return;
    int top = (int) SendMessageW (hListComponents, LB_GETTOPINDEX, 0, 0);
    SendMessageW (hListComponents, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < components.size(); ++i)
        SendMessageW (hListComponents, LB_ADDSTRING, 0, (LPARAM) L"");   // owner-draw
    SendMessageW (hListComponents, LB_SETTOPINDEX, top, 0);
    InvalidateRect (hListComponents, nullptr, FALSE);
}

void TigerFoldersPlugin::uiRefreshPreviewList()
{
    if (!hListPreview) return;
    SendMessageW (hListPreview, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < previewRows.size(); ++i)
        SendMessageW (hListPreview, LB_ADDSTRING, 0, (LPARAM) L"");
    InvalidateRect (hListPreview, nullptr, FALSE);
    if (hDlg) InvalidateRect (hDlg, nullptr, FALSE);
}

void TigerFoldersPlugin::uiUpdateStatus (const std::wstring& msg)
{
    statusText = msg;
    if (hDlg) InvalidateRect (hDlg, nullptr, FALSE);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Visibility for Settings toggle
// ─────────────────────────────────────────────────────────────────────────────

static void applyVisibility (TigerFoldersPlugin* p)
{
    int s = p->showSettings ? SW_HIDE : SW_SHOW;
    HWND ctrls[] = { p->hComboField, p->hComboMode, p->hBtnAdd, p->hListComponents,
                     p->hBtnRemove, p->hBtnClear, p->hEditRoot, p->hBtnScan,
                     p->hBtnBuild, p->hListPreview };
    for (HWND h : ctrls) if (h) ShowWindow (h, s);
    if (!p->showSettings && p->hComboMode)
        ShowWindow (p->hComboMode, fieldHasSubmode (p->pendingField) ? SW_SHOW : SW_HIDE);
}

static void applyLayout (HWND hwnd, TigerFoldersPlugin* p)
{
    Layout L = computeLayout (hwnd);
    auto mv = [] (HWND h, const RECT& r) {
        if (h) MoveWindow (h, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
    };
    // Combos want extra height for the dropdown portion.
    auto mvCombo = [] (HWND h, const RECT& r) {
        if (h) MoveWindow (h, r.left, r.top, r.right - r.left, (r.bottom - r.top) + 180, TRUE);
    };

    mv (p->hBtnSettings, L.settings);
    mv (p->hBtnClose, L.close);
    mvCombo (p->hComboField, L.comboField);
    mvCombo (p->hComboMode, L.comboMode);
    mv (p->hBtnAdd, L.btnAdd);
    mv (p->hListComponents, L.compList);
    mv (p->hBtnRemove, L.btnRemove);
    mv (p->hBtnClear, L.btnClear);
    mv (p->hEditRoot, L.editRoot);
    mv (p->hBtnScan, L.btnScan);
    mv (p->hBtnBuild, L.btnBuild);
    mv (p->hListPreview, L.prevList);

    applyVisibility (p);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Drag-reorder subclass for the component list
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
                p->dragFrom = LOWORD (r);
                SetCapture (hwnd);
            }
            break;
        }
        case WM_LBUTTONUP:
        {
            if (p->dragFrom >= 0)
            {
                ReleaseCapture();
                POINT pt { GET_X_LPARAM (lParam), GET_Y_LPARAM (lParam) };
                DWORD r = (DWORD) SendMessageW (hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM (pt.x, pt.y));
                int to = (HIWORD (r) == 0) ? LOWORD (r) : (int) p->components.size() - 1;
                int from = p->dragFrom;
                p->dragFrom = -1;
                if (from >= 0 && to >= 0 && from < (int) p->components.size()
                    && to < (int) p->components.size() && from != to)
                {
                    Component c = p->components[from];
                    p->components.erase (p->components.begin() + from);
                    p->components.insert (p->components.begin() + to, c);
                    p->uiRefreshComponentList();
                    SendMessageW (hwnd, LB_SETCURSEL, to, 0);
                    p->rebuildPreview();
                    p->uiRefreshPreviewList();
                    p->saveSettings();
                }
            }
            break;
        }
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
    bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    bool pressed  = (dis->itemState & ODS_SELECTED) != 0;
    int id = (int) dis->CtlID;

    bool primary = (id == IDC_BTN_ADD || id == IDC_BTN_SCAN || id == IDC_BTN_BUILD);
    COLORREF bg = primary ? TCol::accent : TCol::buttonBg;
    if (disabled) bg = TCol::buttonDisabled;
    else if (pressed) bg = primary ? TCol::accentBrt : TCol::buttonHover;

    fillRect (hdc, rc, bg);
    frameRect (hdc, rc, TCol::cardBorder);

    wchar_t text[128] = {};
    GetWindowTextW (dis->hwndItem, text, 127);
    COLORREF tc = disabled ? TCol::textDim
                 : primary ? RGB (20, 22, 30) : TCol::textBright;
    drawText (hdc, rc, text, tc, primary ? p->fontBold : p->fontNormal,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void drawComboItem (TigerFoldersPlugin* p, DRAWITEMSTRUCT* dis)
{
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    bool sel = (dis->itemState & ODS_SELECTED) != 0;
    fillRect (hdc, rc, sel ? TCol::selSubtle : TCol::inputBg);

    std::wstring text;
    bool placeholder = false;
    if (dis->itemID == (UINT) -1)
    {
        text = L"";
    }
    else
    {
        wchar_t buf[128] = {};
        SendMessageW (dis->hwndItem, CB_GETLBTEXT, dis->itemID, (LPARAM) buf);
        text = buf;
        if (dis->hwndItem == p->hComboField && dis->itemID == 0)
            placeholder = true;
    }
    RECT tr = rc; tr.left += 6;
    drawText (hdc, tr, text, placeholder ? TCol::textDim : TCol::textBright,
              p->fontNormal, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void drawComponentItem (TigerFoldersPlugin* p, DRAWITEMSTRUCT* dis)
{
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    int idx = (int) dis->itemID;
    bool sel = (dis->itemState & ODS_SELECTED) != 0;

    fillRect (hdc, rc, sel ? TCol::selSubtle : TCol::card);
    if (idx < 0 || idx >= (int) p->components.size()) return;
    const Component& c = p->components[idx];

    // number badge
    RECT num = { rc.left + 6, rc.top + 3, rc.left + 26, rc.bottom - 3 };
    drawText (hdc, num, std::to_wstring (idx + 1), TCol::accentBrt, p->fontBold,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT tr = { rc.left + 30, rc.top, rc.right - 8, rc.bottom };
    std::wstring line = fieldLabel (c.field) + L"  ·  " + componentModeLabel (c);
    drawText (hdc, tr, line, TCol::textBright, p->fontNormal,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT bottom = { rc.left, rc.bottom - 1, rc.right, rc.bottom };
    fillRect (hdc, bottom, TCol::bg);
}

static void drawPreviewItem (TigerFoldersPlugin* p, DRAWITEMSTRUCT* dis)
{
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    int idx = (int) dis->itemID;
    fillRect (hdc, rc, TCol::panel);
    if (idx < 0 || idx >= (int) p->previewRows.size()) return;

    const PreviewRow& row = p->previewRows[idx];
    int indent = 8 + row.depth * 16;
    RECT tr = { rc.left + indent, rc.top, rc.right - 44, rc.bottom };
    std::wstring name = (row.isLeaf ? L"• " : L"▸ ") + row.name;
    drawText (hdc, tr, name, row.isLeaf ? TCol::textNormal : TCol::textBright,
              p->fontSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT cnt = { rc.right - 44, rc.top, rc.right - 6, rc.bottom };
    drawText (hdc, cnt, L"(" + std::to_wstring (row.count) + L")", TCol::textDim,
              p->fontSmall, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Painting (background, panels, labels, source, status, settings help)
// ─────────────────────────────────────────────────────────────────────────────

static void paintWindow (HWND hwnd, TigerFoldersPlugin* p)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint (hwnd, &ps);
    RECT cr; GetClientRect (hwnd, &cr);
    Layout L = computeLayout (hwnd);

    // Double buffer
    HDC mem = CreateCompatibleDC (hdc);
    HBITMAP bmp = CreateCompatibleBitmap (hdc, cr.right, cr.bottom);
    HBITMAP oldBmp = (HBITMAP) SelectObject (mem, bmp);

    fillRect (mem, cr, TCol::bg);

    // Title bar
    fillRect (mem, L.title, TCol::panel);
    drawText (mem, L.brand, L"TigerFolders", TCol::accentBrt, p->fontTitle,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (p->showSettings)
    {
        RECT help = { PAD, TITLE_H + PAD, cr.right - PAD, cr.bottom - PAD };
        std::wstring text =
            L"TigerFolders builds VirtualDJ virtual folders from the tags of the "
            L"songs under the folder you have selected in the browser (recursively).\n\n"
            L"1. Build a structure on the left: pick a field, pick its mode, click ADD. "
            L"Drag rows to reorder (top = root, bottom = leaf).\n"
            L"2. Set the root folder name.\n"
            L"3. Select a folder in VirtualDJ's browser, then click Scan & Preview.\n"
            L"4. Click Build. If folders already exist you'll be asked to Merge or Rebuild.\n\n"
            L"Bandleader/Singer are read from the artist tag split on \" - \". "
            L"Grouping can be limited to instrumental tracks and/or normalized to "
            L"Tango / Vals / Milonga / Cortina / Other. Year buckets are grid-aligned.";
        drawText (mem, help, text, TCol::textNormal, p->fontNormal,
                  DT_LEFT | DT_TOP | DT_WORDBREAK);
    }
    else
    {
        // Left column panel
        RECT leftPanel = { PAD - 2, TITLE_H + 4, L.leftW - 4, cr.bottom - 4 };
        // (kept subtle — background already dark)

        drawText (mem, L.addLabel, L"Add component:", TCol::accentBrt, p->fontBold,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        drawText (mem, L.compLabel, L"Structure  (root → leaf, drag to reorder)",
                  TCol::textDim, p->fontSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        drawText (mem, L.rootLabel, L"Root:", TCol::textNormal, p->fontNormal,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Source folder
        drawText (mem, L.sourceLabel, L"Source folder (+ subfolders):", TCol::textDim,
                  p->fontSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        std::wstring src = p->selectedFolderPath.empty()
            ? L"— select a folder in VirtualDJ, then Scan —"
            : p->selectedFolderPath;
        drawText (mem, L.source, src,
                  p->selectedFolderPath.empty() ? TCol::textDim : TCol::textBright,
                  p->fontSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);

        // Status
        if (!p->statusText.empty())
            drawText (mem, L.status, p->statusText, TCol::good, p->fontSmall,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        // Right column header
        RECT rPanel = { L.rightX - 2, TITLE_H + 4, cr.right - 4, cr.bottom - 4 };
        std::wstring head = L"Preview";
        if (!p->songs.empty())
            head += L"  —  " + std::to_wstring (p->songs.size()) + L" songs → "
                  + std::to_wstring (p->previewFolderCount) + L" folders";
        drawText (mem, L.prevLabel, head, TCol::accentBrt, p->fontBold,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        frameRect (mem, L.prevList, TCol::cardBorder);
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
    if (!p->fieldChosen) return;
    readPendingMode (p);

    Component c;
    c.field = p->pendingField;
    c.nameMode = p->pendingNameMode;
    c.groupScope = p->pendingScope;
    c.groupValue = p->pendingValue;
    c.yearMode = p->pendingYearMode;
    p->components.push_back (c);

    p->uiRefreshComponentList();
    if (!p->songs.empty()) { p->rebuildPreview(); p->uiRefreshPreviewList(); }
    p->saveSettings();
}

static void doRemove (TigerFoldersPlugin* p)
{
    int sel = (int) SendMessageW (p->hListComponents, LB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int) p->components.size()) return;
    p->components.erase (p->components.begin() + sel);
    p->uiRefreshComponentList();
    if (!p->songs.empty()) { p->rebuildPreview(); p->uiRefreshPreviewList(); }
    p->saveSettings();
}

static void doScan (TigerFoldersPlugin* p)
{
    p->uiUpdateStatus (L"Scanning…");
    p->scanSelectedFolder();
    p->rebuildPreview();
    p->uiRefreshPreviewList();
    p->uiUpdateStatus (L"Scanned " + std::to_wstring (p->songs.size())
                       + L" songs → " + std::to_wstring (p->previewFolderCount)
                       + L" folders");
}

static void doBuild (TigerFoldersPlugin* p)
{
    if (p->components.empty()) { p->uiUpdateStatus (L"Add at least one component first"); return; }
    if (p->songs.empty())      { p->uiUpdateStatus (L"Scan a folder first"); return; }

    if (p->managedRootExists())
    {
        int r = MessageBoxW (p->hDlg,
            L"Virtual folders under this root already exist.\n\n"
            L"Yes  = Merge (add songs, keep existing)\n"
            L"No   = Rebuild (wipe this root first)\n"
            L"Cancel = do nothing",
            L"TigerFolders", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (r == IDCANCEL) { p->uiUpdateStatus (L"Build cancelled"); return; }
        if (r == IDNO)     p->removeManagedRoot();
    }

    p->buildVirtualFolders();
    p->uiUpdateStatus (L"Built " + std::to_wstring (p->previewFolderCount)
                       + L" folders for " + std::to_wstring (p->songs.size()) + L" songs");
    p->saveSettings();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Control creation
// ─────────────────────────────────────────────────────────────────────────────

static void createControls (HWND hwnd, TigerFoldersPlugin* p, HINSTANCE hInst)
{
    auto mkButton = [&] (int id, const wchar_t* text) -> HWND {
        return CreateWindowExW (0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 10, 10, hwnd, (HMENU) (intptr_t) id, hInst, nullptr);
    };

    p->hBtnSettings = mkButton (IDC_BTN_SETTINGS, L"⚙");
    p->hBtnClose    = mkButton (IDC_BTN_CLOSE, L"✕");

    p->hComboField = CreateWindowExW (0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL,
        0, 0, 10, 200, hwnd, (HMENU) IDC_COMBO_FIELD, hInst, nullptr);
    p->hComboMode = CreateWindowExW (0, L"COMBOBOX", L"",
        WS_CHILD | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL,
        0, 0, 10, 200, hwnd, (HMENU) IDC_COMBO_MODE, hInst, nullptr);

    // Field combo: placeholder + 7 fields
    SendMessageW (p->hComboField, CB_ADDSTRING, 0, (LPARAM) L"— field —");
    for (int i = 0; i < kFieldCount; ++i)
        SendMessageW (p->hComboField, CB_ADDSTRING, 0, (LPARAM) fieldLabel (kFieldOrder[i]).c_str());
    SendMessageW (p->hComboField, CB_SETCURSEL, 0, 0);

    p->hBtnAdd = mkButton (IDC_BTN_ADD, L"ADD →");
    EnableWindow (p->hBtnAdd, FALSE);

    p->hListComponents = CreateWindowExW (0, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_OWNERDRAWFIXED | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        0, 0, 10, 10, hwnd, (HMENU) IDC_LIST_COMPONENTS, hInst, nullptr);
    SetWindowSubclass (p->hListComponents, compListSubclass, 1, (DWORD_PTR) p);

    p->hBtnRemove = mkButton (IDC_BTN_REMOVE, L"Remove");
    p->hBtnClear  = mkButton (IDC_BTN_CLEAR, L"Clear");

    p->hEditRoot = CreateWindowExW (0, L"EDIT", p->rootName.c_str(),
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0, 0, 10, 10, hwnd, (HMENU) IDC_EDIT_ROOT, hInst, nullptr);
    SendMessageW (p->hEditRoot, WM_SETFONT, (WPARAM) p->fontNormal, TRUE);

    p->hBtnScan  = mkButton (IDC_BTN_SCAN, L"Scan & Preview");
    p->hBtnBuild = mkButton (IDC_BTN_BUILD, L"Build");

    p->hListPreview = CreateWindowExW (0, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_OWNERDRAWFIXED | LBS_NOINTEGRALHEIGHT,
        0, 0, 10, 10, hwnd, (HMENU) IDC_LIST_PREVIEW, hInst, nullptr);

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
        return 0;
    }

    auto* p = getPlugin (hwnd);
    if (!p) return DefWindowProcW (hwnd, msg, wParam, lParam);

    switch (msg)
    {
        case WM_SIZE:
            applyLayout (hwnd, p);
            InvalidateRect (hwnd, nullptr, FALSE);
            return 0;

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
            if (dis->CtlType == ODT_BUTTON)      drawButton (p, dis);
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
                p->uiRepopulateModeCombo();
                applyLayout (hwnd, p);
                p->uiSyncAddButton();
                return 0;
            }
            if (id == IDC_COMBO_MODE && code == CBN_SELCHANGE)
            {
                readPendingMode (p);
                return 0;
            }
            if (id == IDC_EDIT_ROOT && code == EN_CHANGE)
            {
                wchar_t buf[256] = {};
                GetWindowTextW (p->hEditRoot, buf, 255);
                p->rootName = trimWs (buf);
                if (!p->songs.empty()) { p->rebuildPreview(); p->uiRefreshPreviewList(); }
                p->saveSettings();
                return 0;
            }
            if (code == BN_CLICKED)
            {
                switch (id)
                {
                    case IDC_BTN_ADD:    doAdd (p); return 0;
                    case IDC_BTN_REMOVE: doRemove (p); return 0;
                    case IDC_BTN_CLEAR:
                        p->components.clear();
                        p->uiRefreshComponentList();
                        if (!p->songs.empty()) { p->rebuildPreview(); p->uiRefreshPreviewList(); }
                        p->saveSettings();
                        return 0;
                    case IDC_BTN_SCAN:   doScan (p); return 0;
                    case IDC_BTN_BUILD:  doBuild (p); return 0;
                    case IDC_BTN_SETTINGS:
                        p->showSettings = !p->showSettings;
                        applyLayout (hwnd, p);
                        InvalidateRect (hwnd, nullptr, TRUE);
                        return 0;
                    case IDC_BTN_CLOSE:
                        ShowWindow (hwnd, SW_HIDE);
                        return 0;
                }
            }
            return 0;
        }

        // Allow dragging the window by its title bar.
        case WM_LBUTTONDOWN:
        {
            POINT pt { GET_X_LPARAM (lParam), GET_Y_LPARAM (lParam) };
            if (pt.y < TITLE_H)
            {
                ReleaseCapture();
                SendMessageW (hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            }
            return 0;
        }

        case WM_CLOSE:
            ShowWindow (hwnd, SW_HIDE);
            return 0;

        case WM_DESTROY:
            if (p->hListComponents)
                RemoveWindowSubclass (p->hListComponents, compListSubclass, 1);
            p->hDlg = nullptr;
            return 0;
    }
    return DefWindowProcW (hwnd, msg, wParam, lParam);
}
