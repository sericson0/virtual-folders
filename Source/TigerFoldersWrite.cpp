//==============================================================================
// TigerFolders - Virtual folder write engine
// Adapted from tigertag-vst's TigerTagVirtualFolder.cpp:
//   create .vdjfolder / .subfolders / order, append song refs, register in VDJ.
//==============================================================================

#include "TigerFolders.h"
#include <fstream>

// ─────────────────────────────────────────────────────────────────────────────
//  Path-splitting helper shared by the routines below
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

// ─────────────────────────────────────────────────────────────────────────────
//  Ensure the .vdjfolder list files (+ order entries) exist for every level
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

    fs::path baseRoot = getPreferredVdjMyListsRoot();
    std::error_code ec;
    if (baseRoot.empty()) return false;
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

void TigerFoldersPlugin::ensureVirtualFolderPathExists (const std::wstring& fullPath,
                                                        int& createdLevels, int& existingLevels)
{
    createdLevels = 0;
    existingLevels = 0;

    auto escape = [] (const std::wstring& s) -> std::string
    {
        std::string u = toUtf8 (s), result;
        for (char c : u) { if (c == '"') result += "\\\""; else result += c; }
        return result;
    };

    std::wstring current;
    size_t start = 0;
    while (start <= fullPath.size())
    {
        size_t slash = fullPath.find (L'/', start);
        std::wstring part = (slash == std::wstring::npos)
            ? fullPath.substr (start) : fullPath.substr (start, slash - start);

        if (!part.empty())
        {
            if (!current.empty()) current += L"/";
            current += part;
            bool created = ensureVirtualFolderListFileExists (current);
            if (created) createdLevels++;
            else         existingLevels++;

            std::wstring currentBackslash = current;
            std::replace (currentBackslash.begin(), currentBackslash.end(), L'/', L'\\');

            const std::string slashPath = escape (current);
            const std::string backslashPath = escape (currentBackslash);

            vdjSend ("add_virtualfolder \"" + slashPath + "\"");
            if (backslashPath != slashPath)
                vdjSend ("add_virtualfolder \"" + backslashPath + "\"");
        }

        if (slash == std::wstring::npos) break;
        start = slash + 1;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Append a song reference to the leaf .vdjfolder (deduped, XML-escaped)
// ─────────────────────────────────────────────────────────────────────────────

bool TigerFoldersPlugin::appendSongToVirtualFolderListFile (const std::wstring& listPath,
                                                            const std::wstring& songPath) const
{
    if (listPath.empty() || songPath.empty()) return false;

    auto xmlEscape = [] (const std::string& s) -> std::string
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
    };

    std::vector<std::wstring> parts = splitPath (listPath);
    if (parts.empty()) return false;

    fs::path root = getPreferredVdjMyListsRoot();
    std::error_code ec;
    if (root.empty()) return false;

    std::string escapedPath = xmlEscape (toUtf8 (songPath));
    std::string token   = "path=\"" + escapedPath + "\"";
    std::string songLine = "    <song path=\"" + escapedPath + "\" />\n";
    std::string prefix =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<VirtualFolder noDuplicates=\"yes\" ordered=\"yes\">\n";
    std::string suffix = "</VirtualFolder>\n";

    fs::path dir = root;
    for (size_t i = 0; i + 1 < parts.size(); ++i)
        dir /= fs::path (parts[i] + L".subfolders");
    fs::path filePath = dir / fs::path (parts.back() + L".vdjfolder");
    if (!fs::exists (filePath, ec))
        return false;

    std::ifstream in (filePath, std::ios::binary);
    std::string content ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    in.close();

    if (content.find (token) != std::string::npos)
        return true;   // already present

    size_t closePos = content.find ("</VirtualFolder>");
    if (closePos == std::string::npos)
        content = prefix + songLine + suffix;
    else
        content.insert (closePos, songLine);

    std::ofstream out (filePath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out << content;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Managed-root existence / removal (for the rebuild option)
// ─────────────────────────────────────────────────────────────────────────────

bool TigerFoldersPlugin::managedRootExists() const
{
    std::wstring root = sanitizeSegment (rootName);
    if (root.empty()) return false;

    std::error_code ec;
    auto candidates = getVdjMyListsRootCandidates();
    for (const auto& base : candidates)
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

        // Drop the root entry from this directory's order file.
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
//  Build everything for the scanned songs
// ─────────────────────────────────────────────────────────────────────────────

void TigerFoldersPlugin::buildVirtualFolders()
{
    for (const auto& s : songs)
    {
        std::wstring path = buildPathFor (s);
        if (path.empty()) continue;

        int created = 0, existing = 0;
        ensureVirtualFolderPathExists (path, created, existing);
        appendSongToVirtualFolderListFile (path, s.filePath);
    }
}
