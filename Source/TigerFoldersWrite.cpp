//==============================================================================
// TigerFolders - Virtual folder write engine
// Adapted from tigertag-vst's TigerTagVirtualFolder.cpp, but batched:
//   - plan unique folders + per-leaf song groups once
//   - create each folder + register it exactly once
//   - write each leaf .vdjfolder exactly once
// Driven in chunks by the WM_TIMER state machine so the UI never blocks.
//==============================================================================

#include "TigerFolders.h"
#include <fstream>

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::vector<std::wstring> splitPath (const std::wstring& p)
{
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start <= p.size())
    {
        size_t slash = p.find_first_of (L"/\\", start);
        std::wstring part = (slash == std::wstring::npos)
            ? p.substr (start) : p.substr (start, slash - start);
        part = trimWs (part);
        if (!part.empty()) parts.push_back (part);
        if (slash == std::wstring::npos) break;
        start = slash + 1;
    }
    return parts;
}

static std::string escapeArg (const std::wstring& s)
{
    std::string u = toUtf8 (s), out;
    for (char c : u) { if (c == '"') out += "\\\""; else out += c; }
    return out;
}

static std::string xmlEscape (const std::string& s)
{
    std::string out; out.reserve (s.size() + 16);
    for (char c : s)
    {
        switch (c)
        {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out.push_back (c); break;
        }
    }
    return out;
}

static std::string toLowerA (std::string s)
{
    for (auto& c : s) c = (char) tolower ((unsigned char) c);
    return s;
}

// On-disk .vdjfolder path for a "a/b/c" virtual-folder path (nested .subfolders/
// dirs, leaf name + ".vdjfolder"). Same construction ensureVirtualFolderListFileExists
// and appendSongsToLeaf use; shared here for the merge diff.
static fs::path listFilePathFor (const fs::path& root, const std::wstring& listPath)
{
    std::vector<std::wstring> parts = splitPath (listPath);
    if (parts.empty()) return {};
    fs::path dir = root;
    for (size_t i = 0; i + 1 < parts.size(); ++i)
        dir /= fs::path (parts[i] + L".subfolders");
    return dir / fs::path (parts.back() + L".vdjfolder");
}

// ─────────────────────────────────────────────────────────────────────────────
//  MyLists root: only succeeds if an existing MyLists directory is found
// ─────────────────────────────────────────────────────────────────────────────

bool TigerFoldersPlugin::resolveMyListsRoot (fs::path& out) const
{
    std::error_code ec;
    for (const auto& c : getVdjMyListsRootCandidates())
        if (fs::is_directory (c, ec)) { out = c; return true; }
    auto cands = getVdjMyListsRootCandidates();
    out = cands.empty() ? fs::path() : cands.front();
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Create the .vdjfolder list files (+ order entries) for every level of a path
// ─────────────────────────────────────────────────────────────────────────────

bool TigerFoldersPlugin::ensureVirtualFolderListFileExists (const std::wstring& listPath)
{
    auto ensureOrderContains = [] (const fs::path& dir, const std::wstring& name)
    {
        std::error_code ec;
        fs::create_directories (dir, ec);
        fs::path orderPath = dir / L"order";
        bool found = false;
        {
            std::ifstream in (orderPath, std::ios::binary);
            std::string line;
            while (std::getline (in, line))
                if (toWide (line) == name) { found = true; break; }
        }
        if (!found)
        {
            std::ofstream out (orderPath, std::ios::binary | std::ios::app);
            if (out.is_open()) out << toUtf8 (name) << "\n";
        }
    };

    std::vector<std::wstring> parts = splitPath (listPath);
    if (parts.empty()) return false;

    const char* xmlTemplate =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<VirtualFolder />\n";

    fs::path baseRoot;
    if (!resolveMyListsRoot (baseRoot) || baseRoot.empty()) return false;
    std::error_code ec;
    fs::create_directories (baseRoot, ec);

    bool createdAny = false;
    fs::path currentDir = baseRoot;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        const std::wstring& name = parts[i];
        ensureOrderContains (currentDir, name);

        fs::path listFile = currentDir / fs::path (name + L".vdjfolder");
        if (!fs::exists (listFile, ec))
        {
            std::ofstream out (listFile, std::ios::binary);
            if (out.is_open()) { out << xmlTemplate; createdAny = true; }
        }
        if (i + 1 < parts.size())
        {
            currentDir = currentDir / fs::path (name + L".subfolders");
            fs::create_directories (currentDir, ec);
        }
    }
    return createdAny;
}

void TigerFoldersPlugin::registerVirtualFolder (const std::wstring& path)
{
    std::wstring backslash = path;
    std::replace (backslash.begin(), backslash.end(), L'/', L'\\');
    std::string slashArg = escapeArg (path);
    std::string backArg  = escapeArg (backslash);
    vdjSend ("add_virtualfolder \"" + slashArg + "\"");
    if (backArg != slashArg)
        vdjSend ("add_virtualfolder \"" + backArg + "\"");
}

// ─────────────────────────────────────────────────────────────────────────────
//  Append a whole batch of songs to a leaf .vdjfolder in a single rewrite
// ─────────────────────────────────────────────────────────────────────────────

bool TigerFoldersPlugin::appendSongsToLeaf (const std::wstring& leafPath,
                                            const std::vector<std::wstring>& songPaths) const
{
    if (leafPath.empty() || songPaths.empty()) return false;

    std::vector<std::wstring> parts = splitPath (leafPath);
    if (parts.empty()) return false;

    fs::path root;
    std::error_code ec;
    const_cast<TigerFoldersPlugin*> (this)->resolveMyListsRoot (root);
    if (root.empty()) return false;

    fs::path dir = root;
    for (size_t i = 0; i + 1 < parts.size(); ++i)
        dir /= fs::path (parts[i] + L".subfolders");
    fs::path filePath = dir / fs::path (parts.back() + L".vdjfolder");

    std::string prefix =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<VirtualFolder noDuplicates=\"yes\" ordered=\"yes\">\n";
    std::string suffix = "</VirtualFolder>\n";

    std::string content;
    if (fs::exists (filePath, ec))
    {
        std::ifstream in (filePath, std::ios::binary);
        content.assign ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    }
    if (content.find ("</VirtualFolder>") == std::string::npos)
        content = prefix + suffix;

    std::string lowerContent = toLowerA (content);

    // Build the block of new <song> lines, deduped (case-insensitive — Windows
    // paths are case-insensitive) against existing entries and within the batch.
    std::string additions;
    for (const auto& wpath : songPaths)
    {
        std::string esc   = xmlEscape (toUtf8 (wpath));
        std::string token = "path=\"" + esc + "\"";          // anchored by quotes
        std::string lower = toLowerA (token);
        if (lowerContent.find (lower) != std::string::npos) continue;
        lowerContent += lower;                                // dedupe within batch
        additions += "    <song path=\"" + esc + "\" />\n";
    }
    if (additions.empty()) return true;

    size_t closePos = content.find ("</VirtualFolder>");
    content.insert (closePos, additions);

    std::ofstream out (filePath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out << content;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Managed-root existence / removal (Rebuild option)
// ─────────────────────────────────────────────────────────────────────────────

bool TigerFoldersPlugin::managedRootExists() const
{
    std::wstring root = sanitizeSegment (rootName);
    if (root.empty()) return false;

    std::error_code ec;
    for (const auto& base : getVdjMyListsRootCandidates())
    {
        if (base.empty()) continue;
        if (fs::exists (base / fs::path (root + L".vdjfolder"), ec)) return true;
        if (fs::is_directory (base / fs::path (root + L".subfolders"), ec)) return true;
    }
    return false;
}

void TigerFoldersPlugin::removeManagedRoot()
{
    std::wstring root = sanitizeSegment (rootName);
    if (root.empty()) return;

    std::error_code ec;
    for (const auto& base : getVdjMyListsRootCandidates())
    {
        if (base.empty() || !fs::is_directory (base, ec)) continue;

        fs::remove (base / fs::path (root + L".vdjfolder"), ec);
        fs::remove_all (base / fs::path (root + L".subfolders"), ec);

        fs::path orderPath = base / L"order";
        if (fs::exists (orderPath, ec))
        {
            std::vector<std::string> kept;
            {
                std::ifstream in (orderPath, std::ios::binary);
                std::string line;
                while (std::getline (in, line))
                    if (toWide (line) != root) kept.push_back (line);
            }
            std::ofstream out (orderPath, std::ios::binary | std::ios::trunc);
            for (const auto& l : kept) out << l << "\n";
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Plan: unique folders (parent-first) + per-leaf song groups
// ─────────────────────────────────────────────────────────────────────────────

void TigerFoldersPlugin::buildPlan()
{
    planFolders.clear();
    planLeaves.clear();
    cntFolders = cntFiled = cntUnfiled = cntErrors = 0;

    computeSingerYearRanges();

    auto fold = computeOtherFolding();

    std::set<std::wstring> folderSet;
    std::map<std::wstring, std::vector<int>> leafMap;   // leaf path → song indices

    for (size_t si = 0; si < songs.size(); ++si)
    {
        const ScannedSong& s = songs[si];
        bool filed = false, atRoot = false;

        // A multi-singer song under splitMultiSingers is filed under each singer.
        for (const auto& raw : buildPathsFor (s))
        {
            // Apply the "Other" small-folder cutoff, then honor unchecked folders:
            // a song bound for an excluded folder falls back to its nearest
            // checked ancestor.
            auto fit = fold.find (raw);
            std::wstring path = effectivePath ((fit == fold.end()) ? raw : fit->second);
            if (path.empty()) continue;

            // All ancestor prefixes are folders that must exist.
            std::wstring accum;
            size_t start = 0;
            while (start <= path.size())
            {
                size_t slash = path.find (L'/', start);
                std::wstring part = (slash == std::wstring::npos)
                    ? path.substr (start) : path.substr (start, slash - start);
                if (!part.empty())
                {
                    accum = accum.empty() ? part : (accum + L"/" + part);
                    folderSet.insert (accum);
                }
                if (slash == std::wstring::npos) break;
                start = slash + 1;
            }

            leafMap[path].push_back ((int) si);
            filed = true;
            if (path.find (L'/') == std::wstring::npos) atRoot = true;
        }

        if (filed)  ++cntFiled;     // count each song once, even if filed in several folders
        if (atRoot) ++cntUnfiled;
    }

    // std::set iterates lexicographically → parents sort before their children.
    planFolders.assign (folderSet.begin(), folderSet.end());
    for (auto& kv : leafMap)
    {
        std::vector<int>& idxs = kv.second;
        // Optionally order each folder's tracks chronologically (undated last),
        // tie-broken by title; otherwise keep the scan/browser order.
        if (sortByYear)
            std::stable_sort (idxs.begin(), idxs.end(),
                [this] (int a, int b) {
                    int ya = yearToInt (songs[a].year), yb = yearToInt (songs[b].year);
                    int ka = ya ? ya : 1000000, kb = yb ? yb : 1000000;
                    if (ka != kb) return ka < kb;
                    return toLowerW (songs[a].title) < toLowerW (songs[b].title);
                });
        std::vector<std::wstring> paths;
        paths.reserve (idxs.size());
        for (int i : idxs) paths.push_back (songs[i].filePath);
        planLeaves.emplace_back (kv.first, std::move (paths));
    }
    cntFolders = (int) planFolders.size();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Merge diff — what a Build would add vs. what already exists on disk. Call
//  after buildPlan() so planFolders / planLeaves are populated.
// ─────────────────────────────────────────────────────────────────────────────

TigerFoldersPlugin::BuildDiff TigerFoldersPlugin::computeBuildDiff() const
{
    BuildDiff d;
    fs::path root;
    std::error_code ec;
    const_cast<TigerFoldersPlugin*> (this)->resolveMyListsRoot (root);
    if (root.empty()) return d;

    for (const auto& fp : planFolders)
    {
        fs::path file = listFilePathFor (root, fp);
        if (!file.empty() && !fs::exists (file, ec)) ++d.newFolders;
    }

    for (const auto& leaf : planLeaves)
    {
        fs::path file = listFilePathFor (root, leaf.first);
        std::string seen;   // existing file content (lowercased) + within-batch dedupe
        if (!file.empty() && fs::exists (file, ec))
        {
            std::ifstream in (file, std::ios::binary);
            std::string content ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
            seen = toLowerA (content);
        }
        for (const auto& wpath : leaf.second)
        {
            std::string token = toLowerA ("path=\"" + xmlEscape (toUtf8 (wpath)) + "\"");
            if (seen.find (token) != std::string::npos) ++d.dupTracks;
            else { seen += token; ++d.newTracks; }
        }
    }
    return d;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Begin / Step / Finish
// ─────────────────────────────────────────────────────────────────────────────

void TigerFoldersPlugin::buildBegin (bool wipeFirst)
{
    if (op != Op::None) return;

    fs::path root;
    if (!resolveMyListsRoot (root))
    {
        uiUpdateStatus (L"VirtualDJ MyLists folder not found", true);
        return;
    }

    buildPlan();
    if (planFolders.empty())
    {
        uiUpdateStatus (L"Nothing to build · scan a folder and add components", true);
        return;
    }

    if (wipeFirst) removeManagedRoot();

    op               = Op::Building;
    opCancel         = false;
    buildPhaseFolders = true;
    opIndex          = 0;
    planLeafIdx      = 0;
    opTotal          = (int) (planFolders.size() + planLeaves.size());

    uiSetOpRunning (true);
    uiUpdateStatus (L"Building… 0/" + std::to_wstring (opTotal));
    SetTimer (hDlg, TIMER_OP, 1, nullptr);
}

void TigerFoldersPlugin::buildStep()
{
    if (opCancel) { buildFinish(); return; }

    if (buildPhaseFolders)
    {
        const int kPerTick = 40;
        for (int n = 0; n < kPerTick && opIndex < (int) planFolders.size(); ++n, ++opIndex)
        {
            ensureVirtualFolderListFileExists (planFolders[opIndex]);
            registerVirtualFolder (planFolders[opIndex]);
        }
        if (opIndex >= (int) planFolders.size())
            buildPhaseFolders = false;
    }
    else
    {
        const int kPerTick = 8;
        for (int n = 0; n < kPerTick && planLeafIdx < planLeaves.size(); ++n, ++planLeafIdx)
        {
            if (!appendSongsToLeaf (planLeaves[planLeafIdx].first, planLeaves[planLeafIdx].second))
                ++cntErrors;
        }
        if (planLeafIdx >= planLeaves.size()) { buildFinish(); return; }
    }

    int done = buildPhaseFolders ? opIndex : (int) (planFolders.size() + planLeafIdx);
    uiUpdateStatus (L"Building… " + std::to_wstring (done) + L"/" + std::to_wstring (opTotal));
}

void TigerFoldersPlugin::buildFinish()
{
    KillTimer (hDlg, TIMER_OP);
    bool cancelled = opCancel;
    op = Op::None;

    saveSettings();

    std::wstring msg;
    if (cancelled)
        msg = L"Build cancelled";
    else
        msg = L"Built " + std::to_wstring (cntFolders) + L" folders · filed "
            + std::to_wstring (cntFiled) + L" songs";
    if (cntUnfiled > 0)
        msg += L" · " + std::to_wstring (cntUnfiled) + L" at root";
    if (cntErrors > 0)
        msg += L" · " + std::to_wstring (cntErrors) + L" write errors";
    if (!cancelled && cntFolders > 0)
        msg += L" · in VirtualDJ ▸ Folders ▸ " + sanitizeSegment (rootName);

    // On a successful build, return to the starting menu so the user can
    // immediately scan a new folder. The status message persists to confirm what was built.
    if (!cancelled)
    {
        songs.clear();
        previewRows.clear();
        previewFolderCount = 0;
        expandedFolders.clear();
        cntUnfiled = 0;
        previewLens = PreviewLens::Tree;
        previewFilter.clear();
        if (hEditFilter) SetWindowTextW (hEditFilter, L"");
    }

    uiSetOpRunning (false);   // songs is empty → uiRefreshActionButtons shows "Scan & Preview"
    if (!cancelled)
    {
        uiRelayout();             // filter/issue rows go away now that the scan is gone
        uiRefreshPreviewList();   // hides the preview listbox now that previewRows is empty
    }
    uiUpdateStatus (msg, cntErrors > 0);
}
