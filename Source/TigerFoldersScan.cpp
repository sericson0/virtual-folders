//==============================================================================
// TigerFolders - Scan
// Recursively read the selected browser folder's song tags, build preview tree
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

// ─────────────────────────────────────────────────────────────────────────────
//  Scan the currently selected VDJ browser folder + all subfolders
// ─────────────────────────────────────────────────────────────────────────────

void TigerFoldersPlugin::scanSelectedFolder()
{
    songs.clear();

    selectedFolderPath = vdjGetString ("get_browsed_folder_path");

    // Flatten the selected folder + subfolders into the song list, then focus
    // the song zone so get_browsed_song reads file rows.
    vdjSend ("recurse_folder");
    vdjSend ("browser_window 'songs'");

    int count = (int) vdjGetValue ("file_count");
    if (count <= 0)
        return;
    if (count > 100000) count = 100000;   // safety cap

    vdjSend ("browser_scroll 'top'");

    for (int i = 0; i < count; ++i)
    {
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
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Build the preview tree (counts per node), flattened for owner-draw display
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
            if (!part.empty())
            {
                cur = &cur->children[part];
                cur->count++;
            }
            if (slash == std::wstring::npos) break;
            start = slash + 1;
        }
    }

    flatten (root, 0, previewRows, previewFolderCount);
}
