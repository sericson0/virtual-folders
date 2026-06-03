//==============================================================================
// TigerFolders - Scan
// Chunked, timer-driven scan of the selected browser folder + subfolders. Each
// tick processes a batch of rows; after every async browser_scroll we wait only
// as long as VDJ actually needs (the row's filepath to change) before reading,
// so large libraries scan in seconds instead of one slow row per timer tick.
//==============================================================================

#include "TigerFolders.h"
#include <map>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")

// Split the raw artist tag "Bandleader - Singer" into its two parts and flag
// instrumental tracks (singer == "Instrumental", any case).
static void deriveBandleaderSinger (ScannedSong& s)
{
    s.bandleader = trimWs (s.artist);
    s.singer.clear();

    size_t dash = s.artist.find (L" - ");
    if (dash != std::wstring::npos)
    {
        s.bandleader = trimWs (s.artist.substr (0, dash));
        s.singer     = trimWs (s.artist.substr (dash + 3));
    }
    s.instrumental = wiEqual (s.singer, L"Instrumental");
}

static std::string escapeArg (const std::wstring& s)
{
    std::string u = toUtf8 (s), out;
    for (char c : u) { if (c == '"') out += "\\\""; else out += c; }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Begin / Step / Finish
// ─────────────────────────────────────────────────────────────────────────────

void TigerFoldersPlugin::scanBegin()
{
    if (op != Op::None) return;

    songs.clear();
    cntUnfiled = 0;

    preScanFolder = vdjGetString ("get_browsed_folder_path");
    selectedFolderPath = preScanFolder;

    // Make recursion deterministic. recurse_folder is a toggle on the *selected*
    // folder, so a prior scan can leave it stuck "on". Re-select the folder fresh
    // (which resets the browser to a non-recursive view), THEN toggle it on, so we
    // reliably show the folder AND every subfolder regardless of prior state.
    if (!preScanFolder.empty())
        vdjSend ("browser_gotofolder \"" + escapeArg (preScanFolder) + "\"");
    vdjSend ("browser_window 'folders'");
    vdjSend ("recurse_folder");
    vdjSend ("browser_window 'songs'");
    vdjSend ("browser_scroll 'top'");

    // recurse_folder populates the song list asynchronously — flattening a deep
    // tree can take a while. Enter a settle phase that waits for file_count to
    // stop growing before we trust it and start reading rows.
    op              = Op::Scanning;
    opIndex         = 0;
    opCancel        = false;
    scanSettling    = true;
    scanSettleTicks = 0;
    scanLastCount   = -1;
    scanStableCount = 0;

    uiSetOpRunning (true);
    uiUpdateStatus (L"Preparing…");
    SetTimer (hDlg, TIMER_SETTLE, 50, nullptr);
}

// Poll file_count until it stabilises (the async recurse has finished), then
// hand off to the per-row scan loop. Bail out if nothing ever appears.
void TigerFoldersPlugin::scanSettleStep()
{
    if (opCancel) { KillTimer (hDlg, TIMER_SETTLE); scanSettling = false; scanFinish(); return; }

    int cur = (int) vdjGetValue ("file_count");
    if (cur > 0 && cur == scanLastCount) ++scanStableCount;
    else                                 scanStableCount = 0;
    scanLastCount = cur;
    ++scanSettleTicks;

    // Stable for ~200ms (4 ticks) → done. Hard cap ~4s so a genuinely empty
    // folder, or one VDJ never settles, still finishes instead of hanging.
    bool stable  = (cur > 0 && scanStableCount >= 4);
    bool timeout = (scanSettleTicks >= 80);
    if (!stable && !timeout) return;

    KillTimer (hDlg, TIMER_SETTLE);
    scanSettling = false;

    opTotal = cur;
    if (opTotal <= 0)
    {
        op = Op::None;
        uiUpdateStatus (L"No songs found · select a folder with audio", true);
        // Restore the browser view we disturbed.
        if (!preScanFolder.empty())
            vdjSend ("browser_gotofolder \"" + escapeArg (preScanFolder) + "\"");
        vdjSend ("browser_window 'folders'");
        uiSetOpRunning (false);
        return;
    }
    if (opTotal > 200000) opTotal = 200000;

    vdjSend ("browser_scroll 'top'");
    opIndex = 0;
    // Raise the system timer resolution so the per-row Sleep(1) settle is honored
    // at ~1ms instead of the default ~15ms; paired with timeEndPeriod in scanFinish.
    if (!mmPeriodSet && timeBeginPeriod (1) == TIMERR_NOERROR) mmPeriodSet = true;
    lastStatusTick = 0;
    uiUpdateStatus (L"Scanning… 0/" + std::to_wstring (opTotal));
    PostMessageW (hDlg, WM_APP_SCANSTEP, 0, 0);
}

void TigerFoldersPlugin::scanStep()
{
    if (opCancel || opIndex >= opTotal)
    {
        scanFinish();
        return;
    }

    // Read every tag the components might use into a row. (title is intentionally
    // not read — it never feeds a folder path, only the filepath is referenced.)
    auto readRow = [this] (ScannedSong& s) {
        s.filePath = vdjGetString ("get_browsed_filepath");
        s.artist   = vdjGetString ("get_browsed_song 'artist'");
        s.genre    = vdjGetString ("get_browsed_song 'genre'");
        s.grouping = vdjGetString ("get_browsed_song 'grouping'");
        s.label    = vdjGetString ("get_browsed_song 'label'");
        s.album    = vdjGetString ("get_browsed_song 'album'");
        s.year     = vdjGetString ("get_browsed_song 'year'");
    };

    // Process a batch, then self-post WM_APP_SCANSTEP for the next batch (see the
    // header note: this bypasses the ~15ms WM_TIMER floor). The per-row settle
    // below already guarantees the browsed row has changed before the next read,
    // so no extra mid-read verification read is needed.
    const int kBatch = 16;
    for (int n = 0; n < kBatch && opIndex < opTotal && !opCancel; ++n, ++opIndex)
    {
        ScannedSong s;
        readRow (s);

        if (!s.filePath.empty())
        {
            deriveBandleaderSinger (s);
            songs.push_back (std::move (s));
        }

        if (opIndex + 1 < opTotal)
        {
            // Advance, then wait only until the browsed row actually changes
            // (≤ ~10ms, with 1ms timer resolution raised in scanSettleStep).
            const std::wstring prev = s.filePath;
            vdjSend ("browser_scroll +1");
            for (int w = 0; w < 10; ++w)
            {
                if (vdjGetString ("get_browsed_filepath") != prev) break;
                Sleep (1);
            }
        }
    }

    // Throttle progress repaints to ~20/sec and force them (WM_PAINT is low
    // priority and would otherwise be starved by the self-posted step messages).
    DWORD now = GetTickCount();
    if (opCancel || opIndex >= opTotal || now - lastStatusTick >= 50)
    {
        lastStatusTick = now;
        uiUpdateStatus (L"Scanning… " + std::to_wstring (opIndex) + L"/"
                        + std::to_wstring (opTotal));
        if (hDlg) UpdateWindow (hDlg);
    }

    // Drain pending input before reposting. Posted messages outrank hardware
    // input in GetMessage, so a continuously self-posted WM_APP_SCANSTEP would
    // starve the Cancel click that sets opCancel — pump it here so Cancel works.
    MSG m;
    while (PeekMessageW (&m, nullptr, 0, 0, PM_REMOVE))
    {
        if (m.message == WM_APP_SCANSTEP) continue;   // never re-enter ourselves
        TranslateMessage (&m);
        DispatchMessageW (&m);
    }

    if (opCancel || opIndex >= opTotal) scanFinish();
    else                                PostMessageW (hDlg, WM_APP_SCANSTEP, 0, 0);
}

void TigerFoldersPlugin::scanFinish()
{
    KillTimer (hDlg, TIMER_OP);
    if (mmPeriodSet) { timeEndPeriod (1); mmPeriodSet = false; }
    bool cancelled = opCancel;
    op = Op::None;

    // Restore the user's browser view (un-recurse, back to the folder zone).
    if (!preScanFolder.empty())
        vdjSend ("browser_gotofolder \"" + escapeArg (preScanFolder) + "\"");
    vdjSend ("browser_window 'folders'");
    vdjSend ("browser_scroll 'top'");

    cntUnfiled = 0;
    for (const auto& s : songs)
        if (isUnfiled (s)) ++cntUnfiled;

    rebuildPreview();
    uiRefreshPreviewList();

    std::wstring msg;
    if (cancelled)
        msg = L"Scan cancelled · " + std::to_wstring (songs.size()) + L" songs read";
    else
        msg = L"Scanned " + std::to_wstring (songs.size()) + L" songs → "
            + std::to_wstring (previewFolderCount) + L" folders";
    if (cntUnfiled > 0)
        msg += L" · " + std::to_wstring (cntUnfiled) + L" at root (no matching tags)";

    uiSetOpRunning (false);
    uiUpdateStatus (msg, false);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Preview tree (counts per node), flattened for owner-draw display
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
    struct TreeNode
    {
        std::map<std::wstring, TreeNode> children;   // ordered by name
        int count = 0;
    };

    void flatten (const TreeNode& node, int depth, const std::wstring& parentPath,
                  std::vector<PreviewRow>& rows, int& folderCount)
    {
        for (const auto& kv : node.children)
        {
            ++folderCount;
            std::wstring path = parentPath.empty() ? kv.first : (parentPath + L"/" + kv.first);
            PreviewRow row;
            row.name   = kv.first;
            row.path   = path;
            row.depth  = depth;
            row.count  = kv.second.count;
            row.isLeaf = kv.second.children.empty();
            rows.push_back (row);
            flatten (kv.second, depth + 1, path, rows, folderCount);
        }
    }
}

void TigerFoldersPlugin::rebuildPreview()
{
    previewRows.clear();
    previewFolderCount = 0;

    computeSingerYearRanges();

    TreeNode root;
    for (const auto& s : songs)
    {
        std::wstring path = buildPathFor (s);
        if (path.empty()) continue;

        TreeNode* cur = &root;
        size_t start = 0;
        while (start <= path.size())
        {
            size_t slash = path.find (L'/', start);
            std::wstring part = (slash == std::wstring::npos)
                ? path.substr (start) : path.substr (start, slash - start);
            if (!part.empty()) { cur = &cur->children[part]; cur->count++; }
            if (slash == std::wstring::npos) break;
            start = slash + 1;
        }
    }

    flatten (root, 0, L"", previewRows, previewFolderCount);
    applyExclusionFlags();
}
