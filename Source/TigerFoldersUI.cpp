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
    Field::Genre, Field::Bandleader, Field::Singer,
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
    const int lr = L.leftW - PAD;
    int y = TITLE_H + PAD;

    L.addLabel  = { lx, y, lr, y + 16 }; y += 20;
    L.comboField = { lx, y, lx + 124, y + ROW_H };
    L.comboMode  = { lx + 130, y, lx + 130 + 156, y + ROW_H };
    L.btnAdd     = { lr - 66, y, lr, y + ROW_H };
    y += ROW_H + 10;

    L.compLabel = { lx, y, lr, y + 16 }; y += 18;

    const int bottomBlock = 8 + 22 /*remove/clear*/ + 8 + ROW_H /*root*/ + 6
                          + 16 + 18 /*source*/ + 10 + BTN_H /*scan+build*/ + 6 + 16 /*status*/ + PAD;
    int listBottom = H - bottomBlock;
    if (listBottom < y + 60) listBottom = y + 60;
    L.compList = { lx, y, lr, listBottom };
    y = listBottom + 8;

    L.btnRemove = { lx, y, lx + 84, y + 22 };
    L.btnClear  = { lx + 90, y, lx + 90 + 72, y + 22 };
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

    int ry = TITLE_H + PAD;
    L.prevLabel = { L.rightX, ry, L.rightX + L.rightW, ry + 16 }; ry += 20;
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
        case Field::Bandleader:
        case Field::Singer:   return (int) c.nameMode;
        case Field::Grouping: return (c.groupScope == GroupScope::Instrumental ? 2 : 0)
                                   + (c.groupValue == GroupValue::Normalize ? 1 : 0);
        case Field::Year:     return (c.yearMode == YearMode::Y2) ? 0
                                   : (c.yearMode == YearMode::Y5) ? 1 : 2;
        default:              return 0;
    }
}

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
            SendMessageW (hComboMode, CB_SETCURSEL, 1, 0);   // Last, First default
            pendingNameMode = NameMode::LastFirst;
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
            SendMessageW (hComboMode, CB_SETCURSEL, 2, 0);
            pendingYearMode = YearMode::Y10;
            break;
        default:
            break;
    }
    ShowWindow (hComboMode, (!showSettings && fieldHasSubmode (pendingField)) ? SW_SHOW : SW_HIDE);
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
        case Field::Bandleader:
        case Field::Singer:   p->pendingNameMode = (NameMode) sel; break;
        case Field::Grouping:
            p->pendingScope = (sel >= 2) ? GroupScope::Instrumental : GroupScope::All;
            p->pendingValue = (sel % 2 == 1) ? GroupValue::Normalize : GroupValue::Exact;
            break;
        case Field::Year:
            p->pendingYearMode = (sel == 0) ? YearMode::Y2 : (sel == 1) ? YearMode::Y5 : YearMode::Y10;
            break;
        default: break;
    }
}

void TigerFoldersPlugin::uiResetAddRow()
{
    editingIndex = -1;
    fieldChosen  = false;
    pendingField = Field::Genre;
    SendMessageW (hComboField, CB_SETCURSEL, 0, 0);   // placeholder
    uiRepopulateModeCombo();
    SetWindowTextW (hBtnAdd, L"ADD →");
    uiSyncAddButton();
}

void TigerFoldersPlugin::uiLoadComponentForEdit (int idx)
{
    if (idx < 0 || idx >= (int) components.size() || op != Op::None) return;
    const Component& c = components[idx];
    editingIndex = idx;
    fieldChosen  = true;
    pendingField = c.field;

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
    InvalidateRect (hListComponents, nullptr, FALSE);
    if (hDlg) InvalidateRect (hDlg, nullptr, FALSE);
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
    EnableWindow (hBtnRemove, en);
    EnableWindow (hBtnClear, en);
    EnableWindow (hEditRoot, en);
    EnableWindow (hBtnBuild, en);
    EnableWindow (hBtnSettings, en);

    SetWindowTextW (hBtnScan, running ? L"Cancel" : L"Scan & Preview");
    if (running) EnableWindow (hBtnAdd, FALSE); else uiSyncAddButton();

    if (hDlg) InvalidateRect (hDlg, nullptr, FALSE);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Visibility (Settings toggle + empty-state placeholders)
// ─────────────────────────────────────────────────────────────────────────────

static void applyVisibility (TigerFoldersPlugin* p)
{
    bool main = !p->showSettings;
    int s = main ? SW_SHOW : SW_HIDE;
    HWND always[] = { p->hComboField, p->hBtnAdd, p->hBtnRemove, p->hBtnClear,
                      p->hEditRoot, p->hBtnScan, p->hBtnBuild };
    for (HWND h : always) if (h) ShowWindow (h, s);

    if (p->hComboMode)
        ShowWindow (p->hComboMode, (main && fieldHasSubmode (p->pendingField)) ? SW_SHOW : SW_HIDE);

    // Lists hide when empty so a placeholder can be painted in their place.
    if (p->hListComponents)
        ShowWindow (p->hListComponents, (main && !p->components.empty()) ? SW_SHOW : SW_HIDE);
    if (p->hListPreview)
        ShowWindow (p->hListPreview, (main && !p->previewRows.empty()) ? SW_SHOW : SW_HIDE);
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
            if (wParam == VK_DELETE) { doRemove (p); return 0; }
            if (wParam == VK_ESCAPE) { ShowWindow (p->hDlg, SW_HIDE); return 0; }
            break;
    }
    return DefSubclassProc (hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK comboSubclass (HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                       UINT_PTR, DWORD_PTR refData)
{
    auto* p = reinterpret_cast<TigerFoldersPlugin*> (refData);
    if (msg == WM_KEYDOWN)
    {
        if (wParam == VK_RETURN) { if (p->fieldChosen && p->op == Op::None) doAdd (p); return 0; }
        if (wParam == VK_ESCAPE) { ShowWindow (p->hDlg, SW_HIDE); return 0; }
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
        drawText (dc, ar, L"▾", TCol::accentBrt, p->fontSmall,
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

    // Title-bar icon buttons: flat, no border, sit on the title panel.
    if (id == IDC_BTN_CLOSE || id == IDC_BTN_SETTINGS)
    {
        fillRect (hdc, rc, hover ? TCol::buttonHover : TCol::panel);
        COLORREF ic = (id == IDC_BTN_CLOSE && hover) ? RGB (240, 90, 90)
                    : hover ? TCol::textBright : TCol::textNormal;
        drawText (hdc, rc, text, ic, p->fontNormal, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    bool primary = (id == IDC_BTN_ADD || id == IDC_BTN_SCAN || id == IDC_BTN_BUILD);
    // While an op runs the Scan button is a "Cancel" — treat as danger-ghost.
    bool cancel  = (id == IDC_BTN_SCAN && p->op != Op::None);

    COLORREF bg, border, tc;
    if (disabled)        { bg = TCol::buttonDisabled; border = TCol::cardBorder; tc = TCol::textDim; }
    else if (cancel)     { bg = pressed ? RGB (150,50,50) : hover ? RGB (90,40,40) : TCol::buttonBg;
                           border = RGB (150,70,70); tc = RGB (240,150,150); }
    else if (primary)    { bg = pressed ? TCol::accent : hover ? TCol::accentBrt : TCol::accent;
                           border = TCol::accent; tc = RGB (20,22,30); }
    else                 { bg = pressed ? TCol::buttonHover : hover ? TCol::buttonHover : TCol::buttonBg;
                           border = TCol::cardBorder; tc = TCol::textBright; }

    fillRect (hdc, rc, bg);
    frameRect (hdc, rc, border);
    drawText (hdc, rc, text, tc, primary ? p->fontBold : p->fontNormal,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void drawComboItem (TigerFoldersPlugin* p, DRAWITEMSTRUCT* dis)
{
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    bool sel = (dis->itemState & ODS_SELECTED) != 0;
    bool disabled = (dis->itemState & ODS_DISABLED) != 0;
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

    RECT grip = { rc.left + 6, rc.top, rc.left + 20, rc.bottom };
    drawText (hdc, grip, L"≡", TCol::textDim, p->fontNormal, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT num = { rc.left + 22, rc.top, rc.left + 42, rc.bottom };
    drawText (hdc, num, std::to_wstring (idx + 1), TCol::accentBrt, p->fontBold,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT tr = { rc.left + 44, rc.top, rc.right - 8, rc.bottom };
    std::wstring line = fieldLabel (c.field) + L"  ·  " + componentModeLabel (c);
    drawText (hdc, tr, line, TCol::textBright, p->fontNormal,
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
    int indent = 8 + row.depth * 16;
    RECT tr = { rc.left + indent, rc.top, rc.right - 46, rc.bottom };
    std::wstring name = (row.isLeaf ? L"• " : L"") + row.name;
    drawText (hdc, tr, name, row.isLeaf ? TCol::textNormal : TCol::textBright,
              p->fontSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT cnt = { rc.right - 46, rc.top, rc.right - 6, rc.bottom };
    drawText (hdc, cnt, L"(" + std::to_wstring (row.count) + L")", TCol::textDim,
              p->fontSmall, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Painting
// ─────────────────────────────────────────────────────────────────────────────

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

    if (p->showSettings)
    {
        RECT help = { PAD, TITLE_H + PAD, cr.right - PAD, cr.bottom - PAD };
        std::wstring text =
            L"TigerFolders builds VirtualDJ virtual folders from the tags of the "
            L"songs under the folder you've selected in the browser (recursively).\n\n"
            L"1.  Build a structure on the left: pick a field, pick its mode, click ADD →.\n"
            L"     Drag rows to reorder (top = root, bottom = leaf). Double-click a row to edit it.\n"
            L"2.  Set the root folder name.\n"
            L"3.  Select a folder in VirtualDJ's browser, then click Scan & Preview.\n"
            L"4.  Click Build. If folders already exist you'll be asked to Merge or Rebuild.\n\n"
            L"Bandleader/Singer are read from the artist tag split on \" - \". "
            L"Grouping can be limited to instrumental tracks and/or normalized to "
            L"Tango / Vals / Milonga / Cortina / Other. Year buckets are grid-aligned.";
        drawText (mem, help, text, TCol::textNormal, p->fontNormal, DT_LEFT | DT_TOP | DT_WORDBREAK);
    }
    else
    {
        // Right-column panel surface + frame to separate the two columns
        RECT rPanel = { L.rightX - 2, TITLE_H + 4, cr.right - 4, cr.bottom - 4 };
        fillRect (mem, rPanel, TCol::panel);
        frameRect (mem, rPanel, TCol::cardBorder);

        drawText (mem, L.addLabel, L"Add component", TCol::accentBrt, p->fontHeader,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        drawText (mem, L.compLabel, L"Structure  (root → leaf · drag to reorder · double-click to edit)",
                  TCol::textNormal, p->fontSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Component-list frame + empty hint
        frameRect (mem, L.compList, TCol::cardBorder);
        if (p->components.empty())
            drawText (mem, L.compList, L"No components yet — pick a field above and click ADD →",
                      TCol::textDim, p->fontSmall, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        drawText (mem, L.rootLabel, L"Root:", TCol::textNormal, p->fontNormal,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        frameRect (mem, L.editRoot, TCol::cardBorder);

        // Source folder OR progress bar while an op runs
        if (p->op != Op::None)
        {
            drawText (mem, L.sourceLabel,
                      p->op == Op::Scanning ? L"Scanning…" : L"Building…",
                      TCol::accentBrt, p->fontSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
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
        else
        {
            drawText (mem, L.sourceLabel, L"Source folder (+ subfolders):", TCol::textNormal,
                      p->fontSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            std::wstring src = p->selectedFolderPath.empty()
                ? L"— select a folder in VirtualDJ, then Scan —"
                : p->selectedFolderPath;
            drawText (mem, L.source, src,
                      p->selectedFolderPath.empty() ? TCol::textDim : TCol::textBright,
                      p->fontSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
        }

        if (!p->statusText.empty())
            drawText (mem, L.status, p->statusText,
                      p->statusError ? RGB (240, 120, 110) : TCol::good,
                      p->fontSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        // Right-column header + preview placeholder
        std::wstring head = L"Preview";
        if (!p->songs.empty())
            head += L"  —  " + std::to_wstring (p->songs.size()) + L" songs → "
                  + std::to_wstring (p->previewFolderCount) + L" folders";
        drawText (mem, L.prevLabel, head, TCol::accentBrt, p->fontHeader,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        frameRect (mem, L.prevList, TCol::cardBorder);
        if (p->previewRows.empty())
            drawText (mem, L.prevList,
                      L"Select a folder in VirtualDJ\nand click Scan & Preview",
                      TCol::textDim, p->fontSmall, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
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
    c.groupValue = p->pendingValue;
    c.yearMode   = p->pendingYearMode;

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

    bool wipe = false;
    if (p->managedRootExists())
    {
        std::wstring root = sanitizeSegment (p->rootName);
        std::wstring text =
            L"Virtual folders under \"" + root + L"\" already exist.\n\n"
            L"• Yes — MERGE: add these songs to the existing structure (nothing deleted).\n"
            L"• No — REBUILD: DELETE the entire \"" + root + L"\" tree first, then rebuild.\n"
            L"• Cancel — do nothing.";
        int r = MessageBoxW (p->hDlg, text.c_str(), L"TigerFolders — Build",
                             MB_YESNOCANCEL | MB_ICONWARNING | MB_DEFBUTTON1);
        if (r == IDCANCEL) { p->uiUpdateStatus (L"Build cancelled"); return; }
        wipe = (r == IDNO);
    }
    p->buildBegin (wipe);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Control creation
// ─────────────────────────────────────────────────────────────────────────────

static void createControls (HWND hwnd, TigerFoldersPlugin* p, HINSTANCE hInst)
{
    auto mkButton = [&] (int id, const wchar_t* text) -> HWND {
        HWND h = CreateWindowExW (0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 10, 10, hwnd, (HMENU) (intptr_t) id, hInst, nullptr);
        SetWindowSubclass (h, buttonSubclass, 10, (DWORD_PTR) p);
        return h;
    };

    p->hBtnSettings = mkButton (IDC_BTN_SETTINGS, L"⚙");
    p->hBtnClose    = mkButton (IDC_BTN_CLOSE, L"✕");

    p->hComboField = CreateWindowExW (0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL,
        0, 0, 10, 200, hwnd, (HMENU) IDC_COMBO_FIELD, hInst, nullptr);
    p->hComboMode = CreateWindowExW (0, L"COMBOBOX", L"",
        WS_CHILD | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL,
        0, 0, 10, 200, hwnd, (HMENU) IDC_COMBO_MODE, hInst, nullptr);
    SetWindowTheme (p->hComboField, L"", L"");
    SetWindowTheme (p->hComboMode, L"", L"");
    SetWindowSubclass (p->hComboField, comboSubclass, 11, (DWORD_PTR) p);
    SetWindowSubclass (p->hComboMode, comboSubclass, 11, (DWORD_PTR) p);

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
        case WM_TIMER:
            if (wParam == TIMER_OP)
            {
                if (p->op == Op::Scanning) p->scanStep();
                else if (p->op == Op::Building) p->buildStep();
            }
            return 0;

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
                if (p->editingIndex >= 0) { p->editingIndex = -1; SetWindowTextW (p->hBtnAdd, L"ADD →"); }
                p->uiRepopulateModeCombo();
                applyLayout (hwnd, p);
                p->uiSyncAddButton();
                return 0;
            }
            if (id == IDC_COMBO_MODE && code == CBN_SELCHANGE) { readPendingMode (p); return 0; }
            if (id == IDC_EDIT_ROOT && code == EN_CHANGE)
            {
                wchar_t buf[256] = {};
                GetWindowTextW (p->hEditRoot, buf, 255);
                p->rootName = trimWs (buf);
                if (!p->songs.empty()) { p->rebuildPreview(); p->uiRefreshPreviewList(); }
                p->saveSettings();
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
                        if (p->op != Op::None) p->opCancel = true;
                        else                   p->scanBegin();
                        return 0;
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
            if (p->op != Op::None) { KillTimer (hwnd, TIMER_OP); p->op = Op::None; }
            if (p->hListComponents) RemoveWindowSubclass (p->hListComponents, compListSubclass, 1);
            p->hDlg = nullptr;
            return 0;
    }
    return DefWindowProcW (hwnd, msg, wParam, lParam);
}
