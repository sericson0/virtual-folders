//==============================================================================
// TigerFolders - Scan
// Chunked, timer-driven scan of the selected browser folder + subfolders so the
// UI thread never blocks. One browser row per tick gives VDJ's async scroll the
// settle time it needs (the gap until the next tick) before reading tags.
//==============================================================================

#include "TigerFolders.h"
#include <map>

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

    // Flatten selected folder + subfolders into the song list, focus the song
    // zone, and park at the top.
    vdjSend ("recurse_folder");
    vdjSend ("browser_window 'songs'");

    opTotal = (int) vdjGetValue ("file_count");
    if (opTotal <= 0)
    {
        uiUpdateStatus (L"No songs found — select a folder with audio in VirtualDJ", true);
        // Restore the browser view we disturbed.
        if (!preScanFolder.empty())
            vdjSend ("browser_gotofolder \"" + escapeArg (preScanFolder) + "\"");
        vdjSend ("browser_window 'folders'");
        return;
    }
    if (opTotal > 200000) opTotal = 200000;

    vdjSend ("browser_scroll 'top'");

    op       = Op::Scanning;
    opIndex  = 0;
    opCancel = false;

    uiSetOpRunning (true);
    uiUpdateStatus (L"Scanning… 0 / " + std::to_wstring (opTotal));
    SetTimer (hDlg, TIMER_OP, 1, nullptr);
}

void TigerFoldersPlugin::scanStep()
{
    if (opCancel || opIndex >= opTotal)
    {
        scanFinish();
        return;
    }

    ScannedSong s;
    s.filePath = vdjGetString ("get_browsed_filepath");
    s.title    = vdjGetString ("get_browsed_song 'title'");
    s.artist   = vdjGetString ("get_browsed_song 'artist'");
    s.genre    = vdjGetString ("get_browsed_song 'genre'");
    s.grouping = vdjGetString ("get_browsed_song 'grouping'");
    s.label    = vdjGetString ("get_browsed_song 'label'");
    s.album    = vdjGetString ("get_browsed_song 'album'");
    s.year     = vdjGetString ("get_browsed_song 'year'");

    if (!s.filePath.empty())
    {
        deriveBandleaderSinger (s);
        songs.push_back (std::move (s));
    }

    vdjSend ("browser_scroll +1");
    ++opIndex;

    if ((opIndex % 25) == 0 || opIndex == opTotal)
        uiUpdateStatus (L"Scanning… " + std::to_wstring (opIndex) + L" / "
                        + std::to_wstring (opTotal));
}

void TigerFoldersPlugin::scanFinish()
{
    KillTimer (hDlg, TIMER_OP);
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
        msg = L"Scan cancelled — " + std::to_wstring (songs.size()) + L" songs read";
    else
        msg = L"Scanned " + std::to_wstring (songs.size()) + L" songs → "
            + std::to_wstring (previewFolderCount) + L" folders";
    if (cntUnfiled > 0)
        msg += L"  ·  " + std::to_wstring (cntUnfiled) + L" land at root (no matching tags)";

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

    void flatten (const TreeNode& node, int depth,
                  std::vector<PreviewRow>& rows, int& folderCount)
    {
        for (const auto& kv : node.children)
        {
            ++folderCount;
            PreviewRow row;
            row.name   = kv.first;
            row.depth  = depth;
            row.count  = kv.second.count;
            row.isLeaf = kv.second.children.empty();
            rows.push_back (row);
            flatten (kv.second, depth + 1, rows, folderCount);
        }
    }
}

void TigerFoldersPlugin::rebuildPreview()
{
    previewRows.clear();
    previewFolderCount = 0;

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

    flatten (root, 0, previewRows, previewFolderCount);
}
