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
    expandedFolders.clear();
    cntUnfiled = 0;
    // Start a fresh scan in the plain folder-tree view (drop any filter/lens).
    previewLens = PreviewLens::Tree;
    previewFilter.clear();
    if (hEditFilter) SetWindowTextW (hEditFilter, L"");

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

    // Read every tag the components might use into a row. Title doesn't feed a
    // folder path, but it labels the songs shown when a preview folder is expanded.
    auto readRow = [this] (ScannedSong& s) {
        s.filePath = vdjGetString ("get_browsed_filepath");
        s.artist   = vdjGetString ("get_browsed_song 'artist'");
        s.title    = vdjGetString ("get_browsed_song 'title'");
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
    // The scan is driven by TIMER_SETTLE then WM_APP_SCANSTEP — never TIMER_OP
    // (that's the build timer). Clear the settle timer in case we arrive here
    // mid-settle (e.g. cancelled before the row phase started).
    KillTimer (hDlg, TIMER_SETTLE);
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
    uiRelayout();            // the preview now has rows → position filter/issue rows
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
    // Collect the paths of every non-leaf folder (one with child folders) under
    // `node`. Seeds the default "all folders expanded" view and drives Expand-all;
    // leaf (song-only) folders are excluded so their song lists stay collapsed.
    void collectNonLeafPaths (const PreviewNode& node, const std::wstring& parentPath,
                              std::set<std::wstring>& out)
    {
        for (const auto& kv : node.children)
        {
            std::wstring path = parentPath.empty() ? kv.first : (parentPath + L"/" + kv.first);
            if (!kv.second.children.empty()) out.insert (path);
            collectNonLeafPaths (kv.second, path, out);
        }
    }

    // Total folder nodes in the tree, independent of which are expanded — so the
    // "→ N folders" header reflects the whole structure, not just visible rows.
    int countAllFolders (const PreviewNode& node)
    {
        int c = 0;
        for (const auto& kv : node.children) c += 1 + countAllFolders (kv.second);
        return c;
    }

    // Folder-name filter: insert into `out` every folder path that matches
    // `filterLower` (case-insensitive substring) or is an ancestor of a match, so
    // the matched folders show with the spine needed to reach them. Returns whether
    // this subtree contains any match.
    bool collectFilterVisible (const PreviewNode& node, const std::wstring& parentPath,
                               const std::wstring& filterLower, std::set<std::wstring>& out)
    {
        bool anyVisible = false;
        for (const auto& kv : node.children)
        {
            std::wstring path = parentPath.empty() ? kv.first : (parentPath + L"/" + kv.first);
            bool selfMatch    = toLowerW (kv.first).find (filterLower) != std::wstring::npos;
            bool childVisible = collectFilterVisible (kv.second, path, filterLower, out);
            if (selfMatch || childVisible) { out.insert (path); anyVisible = true; }
        }
        return anyVisible;
    }

    void flattenNode (const PreviewNode& node, int depth, const std::wstring& parentPath,
                      std::vector<PreviewRow>& rows, int& folderCount,
                      const std::set<std::wstring>& expanded,
                      const std::vector<ScannedSong>& songs, bool sortByYear,
                      const std::set<std::wstring>* filterVisible)
    {
        const bool filtering = (filterVisible != nullptr);
        for (const auto& kv : node.children)
        {
            std::wstring path = parentPath.empty() ? kv.first : (parentPath + L"/" + kv.first);
            if (filtering && !filterVisible->count (path)) continue;   // filtered out
            ++folderCount;
            // While filtering, force the matched spine open (child folders shown);
            // otherwise honor the user's expand/collapse state.
            bool isExpanded = filtering ? true : (expanded.count (path) > 0);

            PreviewRow row;
            row.name     = kv.first;
            row.path     = path;
            row.depth    = depth;
            row.count    = kv.second.count;
            row.isLeaf   = kv.second.children.empty();
            row.hasSongs = !kv.second.songEntries.empty();
            rows.push_back (row);

            if (!isExpanded) continue;   // collapsed: hide children + songs

            // List the song titles filed directly in this folder (alphabetical,
            // tie-broken on songIndex for a stable order), then recurse subfolders.
            // While filtering we keep the view to folders only (no song rows).
            if (!filtering && row.hasSongs)
            {
                struct SortEntry { std::wstring key; std::wstring name; int idx; int year; };
                std::vector<SortEntry> entries;
                entries.reserve (kv.second.songEntries.size());
                for (const auto& e : kv.second.songEntries)
                {
                    int yr = (e.second >= 0 && e.second < (int) songs.size())
                             ? yearToInt (songs[e.second].year) : 0;
                    entries.push_back ({ toLowerW (e.first), e.first, e.second, yr });
                }
                std::sort (entries.begin(), entries.end(),
                           [sortByYear] (const SortEntry& a, const SortEntry& b) {
                               if (sortByYear)
                               {
                                   int ka = a.year ? a.year : 1000000;
                                   int kb = b.year ? b.year : 1000000;
                                   if (ka != kb) return ka < kb;
                               }
                               if (a.key != b.key) return a.key < b.key;
                               return a.idx < b.idx;
                           });
                for (const auto& e : entries)
                {
                    PreviewRow sr;
                    sr.name      = e.name;
                    sr.path      = path;     // shares the folder's exclusion identity
                    sr.depth     = depth + 1;
                    sr.isSong    = true;
                    sr.songIndex = e.idx;
                    rows.push_back (sr);
                }
            }

            flattenNode (kv.second, depth + 1, path, rows, folderCount, expanded,
                         songs, sortByYear, filterVisible);
        }
    }
}

void TigerFoldersPlugin::rebuildPreview (bool preserveExpansion)
{
    computeSingerYearRanges();

    auto fold = computeOtherFolding();

    previewTree = PreviewNode {};
    for (size_t si = 0; si < songs.size(); ++si)
    {
        const ScannedSong& s = songs[si];
        std::wstring disp = trimWs (s.title);
        if (disp.empty()) disp = fs::path (s.filePath).stem().wstring();

        for (const auto& rawPath : buildPathsFor (s))
        {
            if (rawPath.empty()) continue;
            auto fit = fold.find (rawPath);
            const std::wstring& path = (fit == fold.end()) ? rawPath : fit->second;

            PreviewNode* cur = &previewTree;
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
            // `cur` now points at the deepest folder this song is filed in.
            cur->songEntries.push_back ({ disp, (int) si });
        }
    }

    if (!preserveExpansion)
    {
        // Default view: the whole folder hierarchy open, individual song lists
        // collapsed. Expanding a folder shows its child folders AND its direct
        // songs; leaf (song-only) folders therefore start with their songs hidden.
        expandedFolders.clear();
        collectNonLeafPaths (previewTree, L"", expandedFolders);
    }

    computeQualityCounts();
    flattenPreviewRows();
}

// Tally missing-tag counts (+ unfiled) so the preview's issue chips stay accurate
// as the structure is edited. Cheap relative to the path build already done above.
void TigerFoldersPlugin::computeQualityCounts()
{
    qNoYear = qNoGenre = qNoArtist = cntUnfiled = 0;
    for (const auto& s : songs)
    {
        if (yearToInt (s.year) == 0)     ++qNoYear;
        if (trimWs (s.genre).empty())    ++qNoGenre;
        if (trimWs (s.artist).empty())   ++qNoArtist;
        if (isUnfiled (s))               ++cntUnfiled;
    }
}

void TigerFoldersPlugin::flattenPreviewRows()
{
    previewRows.clear();

    // Problem-song lens: a flat, alphabetical list of the matching songs (as song
    // rows, so the metadata tooltip works) instead of the folder tree.
    if (previewLens != PreviewLens::Tree)
    {
        auto matches = [&] (const ScannedSong& s) -> bool {
            switch (previewLens)
            {
                case PreviewLens::Unfiled:  return isUnfiled (s);
                case PreviewLens::NoYear:   return yearToInt (s.year) == 0;
                case PreviewLens::NoGenre:  return trimWs (s.genre).empty();
                case PreviewLens::NoArtist: return trimWs (s.artist).empty();
                default:                    return false;
            }
        };
        struct E { std::wstring key; std::wstring disp; int idx; };
        std::vector<E> es;
        for (size_t i = 0; i < songs.size(); ++i)
        {
            if (!matches (songs[i])) continue;
            std::wstring disp = trimWs (songs[i].title);
            if (disp.empty()) disp = fs::path (songs[i].filePath).stem().wstring();
            es.push_back ({ toLowerW (disp), disp, (int) i });
        }
        std::sort (es.begin(), es.end(),
                   [] (const E& a, const E& b) { return a.key != b.key ? a.key < b.key : a.idx < b.idx; });
        for (const auto& e : es)
        {
            PreviewRow r;
            r.name      = e.disp;
            r.isSong    = true;
            r.songIndex = e.idx;
            r.depth     = 0;
            previewRows.push_back (r);
        }
        previewFolderCount = countAllFolders (previewTree);
        applyExclusionFlags();
        return;
    }

    int visibleFolders = 0;   // flattenNode counts only emitted rows (discarded)
    std::set<std::wstring> filterVisible;
    const std::set<std::wstring>* fv = nullptr;
    std::wstring f = trimWs (previewFilter);
    if (!f.empty())
    {
        collectFilterVisible (previewTree, L"", toLowerW (f), filterVisible);
        fv = &filterVisible;
    }
    flattenNode (previewTree, 0, L"", previewRows, visibleFolders, expandedFolders,
                 songs, sortByYear, fv);
    previewFolderCount = countAllFolders (previewTree);   // total, not just visible
    applyExclusionFlags();
}

void TigerFoldersPlugin::expandAllFolders()
{
    // Expand every non-leaf folder (reveals the full folder spine). Leaf song lists
    // remain per-folder opt-in to avoid inserting a row per song for huge libraries.
    expandedFolders.clear();
    collectNonLeafPaths (previewTree, L"", expandedFolders);
}
