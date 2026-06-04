//==============================================================================
// TigerFolders - Path logic
// Turn a component + a scanned song into a folder segment, then a full path.
//==============================================================================

#include "TigerFolders.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Name formatting (Bandleader / Singer)
// ─────────────────────────────────────────────────────────────────────────────

static std::wstring formatOneName (const std::wstring& raw, NameMode mode)
{
    std::wstring name = trimWs (raw);
    if (name.empty()) return {};

    std::wstring last  = nameLast (name);
    std::wstring first = nameFirst (name);

    switch (mode)
    {
        case NameMode::FirstLast:
            // Normalize "Last, First" → "First Last"; pass through otherwise.
            if (name.find (L',') != std::wstring::npos)
                return trimWs (first.empty() ? last : (first + L" " + last));
            return name;

        case NameMode::LastFirst:
            return first.empty() ? last : (last + L", " + first);

        case NameMode::Last:
        // Year-range modes resolve to the bare Last name here; buildPathFor
        // prepends the computed "41-43 " / "1941-1943 " range.
        case NameMode::YearRangeShort:
        case NameMode::YearRangeLong:
            return last;

        case NameMode::LastUpper:
            return toUpperW (last);

        case NameMode::LastUpperFirst:
            return first.empty() ? toUpperW (last)
                                 : (toUpperW (last) + L", " + first);
    }
    return name;
}

// Split a possibly-multi-person field into its individual raw names, on the
// connectors " and " / " y " (case-insensitive).
static std::vector<std::wstring> splitPeople (const std::wstring& field)
{
    std::vector<std::wstring> people;
    std::wstring f = trimWs (field);
    if (f.empty()) return people;

    std::wstring lower = toLowerW (f);
    size_t pos = 0;
    size_t cursor = 0;
    while (pos < lower.size())
    {
        size_t aPos = lower.find (L" and ", pos);
        size_t yPos = lower.find (L" y ", pos);
        size_t aHit = (aPos == std::wstring::npos) ? lower.size() : aPos;
        size_t yHit = (yPos == std::wstring::npos) ? lower.size() : yPos;
        size_t hit = (aHit < yHit) ? aHit : yHit;
        if (hit >= lower.size()) break;
        size_t connLen = (hit == aPos) ? 5 : 3;   // " and " vs " y "
        people.push_back (trimWs (f.substr (cursor, hit - cursor)));
        cursor = hit + connLen;
        pos = cursor;
    }
    people.push_back (trimWs (f.substr (cursor)));
    return people;
}

// Each person of a multi-person field, formatted (non-empty only).
static std::vector<std::wstring> formatNamesList (const std::wstring& field, NameMode mode)
{
    std::vector<std::wstring> out;
    for (const auto& person : splitPeople (field))
    {
        std::wstring formatted = formatOneName (person, mode);
        if (!formatted.empty()) out.push_back (formatted);
    }
    return out;
}

// Apply a name format across a possibly-multi-person field
// (joined by " and " / " y "), preserving the joiner as ", ".
static std::wstring formatNameField (const std::wstring& field, NameMode mode)
{
    std::wstring out;
    for (const auto& formatted : formatNamesList (field, mode))
    {
        if (!out.empty()) out += L", ";
        out += formatted;
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Rhythm normalize — collapse a free-text tag to a canonical tango rhythm.
//  Applied to the Genre field (Exact / Normalize).
// ─────────────────────────────────────────────────────────────────────────────

static std::wstring normalizeRhythm (const std::wstring& raw)
{
    std::wstring g = toLowerW (raw);
    // Word-ish substring match, any case.
    if (g.find (L"cortina")  != std::wstring::npos) return L"Cortina";
    if (g.find (L"vals")     != std::wstring::npos) return L"Vals";
    if (g.find (L"milonga")  != std::wstring::npos) return L"Milonga";
    if (g.find (L"tango")    != std::wstring::npos) return L"Tango";
    return L"Other";
}

// ─────────────────────────────────────────────────────────────────────────────
//  Year buckets (grid-aligned)
// ─────────────────────────────────────────────────────────────────────────────

static std::wstring yearBucket (const std::wstring& yearStr, YearMode mode)
{
    std::wstring y = trimWs (yearStr);
    // Take leading 4 digits if present.
    int year = 0;
    int digits = 0;
    for (wchar_t c : y)
    {
        if (c >= L'0' && c <= L'9') { year = year * 10 + (c - L'0'); if (++digits == 4) break; }
        else if (digits > 0) break;
    }
    if (digits < 4 || year < 1000) return {};   // not a usable year

    int width = (mode == YearMode::Y2) ? 2 : (mode == YearMode::Y5) ? 5 : 10;
    int start = year - (year % width);
    int end   = start + width - 1;
    return std::to_wstring (start) + L"-" + std::to_wstring (end);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Per-component rhythm filter — does this level apply to this song?
//  The song's rhythm is the normalized genre; RHY_ALL applies to everything.
// ─────────────────────────────────────────────────────────────────────────────

static unsigned rhythmBitFor (const ScannedSong& s)
{
    std::wstring r = normalizeRhythm (s.genre);
    if (r == L"Tango")   return RHY_TANGO;
    if (r == L"Vals")    return RHY_VALS;
    if (r == L"Milonga") return RHY_MILONGA;
    return RHY_OTHER;   // Cortina / Other / untagged
}

static bool componentApplies (const Component& c, const ScannedSong& s)
{
    if (c.rhythmMask == RHY_ALL) return true;        // applies to every song
    return (c.rhythmMask & rhythmBitFor (s)) != 0;   // only the chosen rhythms
}

// ─────────────────────────────────────────────────────────────────────────────
//  Component → segment
// ─────────────────────────────────────────────────────────────────────────────

std::wstring TigerFoldersPlugin::segmentFor (const Component& c, const ScannedSong& s) const
{
    // Rhythm filter: a song outside the component's chosen rhythms skips this level.
    if (!componentApplies (c, s)) return {};

    std::wstring seg;
    switch (c.field)
    {
        case Field::Genre:
            seg = (c.genreValue == GroupValue::Normalize)
                ? normalizeRhythm (s.genre)
                : s.genre;
            break;
        case Field::Label:      seg = s.label;   break;
        case Field::Album:      seg = s.album;   break;
        case Field::Bandleader: seg = formatNameField (s.bandleader, c.nameMode); break;
        case Field::Singer:
            // Instrumentals have no singer — skip this level for them so a Singer
            // component cleanly names only the vocal tracks.
            if (s.instrumental) return {};
            seg = formatNameField (s.singer, c.nameMode);
            break;
        case Field::Year:
            // Instrumental-only scope: vocal tracks skip the year level entirely.
            if (c.yearScope == GroupScope::Instrumental && !s.instrumental) return {};
            seg = yearBucket (s.year, c.yearMode);
            break;
        case Field::VocalSplit:
            seg = s.instrumental ? L"Instrumentals" : L"Singers";
            break;
        case Field::Grouping:
        {
            // Instrumental-only scope: vocal tracks skip this level entirely.
            if (c.groupScope == GroupScope::Instrumental && !s.instrumental)
                return {};
            seg = s.grouping;
            break;
        }
    }
    if (normalizeSpanish) seg = normalizeLatinAccents (seg);
    return sanitizeSegment (seg);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Year-range name modes ([YY] / [YYYY])
// ─────────────────────────────────────────────────────────────────────────────

static int parseYear4 (const std::wstring& yearStr)
{
    int year = 0, digits = 0;
    for (wchar_t c : yearStr)
    {
        if (c >= L'0' && c <= L'9') { year = year * 10 + (c - L'0'); if (++digits == 4) break; }
        else if (digits > 0) break;
    }
    return (digits == 4 && year >= 1000) ? year : 0;
}

static bool isRangeMode (NameMode m)
{
    return m == NameMode::YearRangeShort || m == NameMode::YearRangeLong;
}

static std::wstring twoDigit (int y)
{
    int yy = ((y % 100) + 100) % 100;
    std::wstring s = std::to_wstring (yy);
    return (s.size() < 2) ? (L"0" + s) : s;
}

// "41-43 " (short) or "1941-1943 " (long). A single year collapses to "41 "
// unless padSingle is set, in which case it renders as a range too ("44-44 ").
static std::wstring formatYearRange (const std::pair<int,int>& r, NameMode mode, bool padSingle)
{
    int lo = r.first, hi = r.second;
    if (lo <= 0) lo = hi;
    if (hi <= 0) hi = lo;
    if (lo <= 0) return {};

    bool single = (lo == hi) && !padSingle;
    if (mode == NameMode::YearRangeShort)
        return single ? (twoDigit (lo) + L" ")
                      : (twoDigit (lo) + L"-" + twoDigit (hi) + L" ");
    return single ? (std::to_wstring (lo) + L" ")
                  : (std::to_wstring (lo) + L"-" + std::to_wstring (hi) + L" ");
}

// The folder segment(s) a component contributes for a song, as a set of parallel
// "branches". Each branch is a sequence of segments to append; a component emits
// one branch normally, an empty branch when it skips this level, and — when
// splitMultiSingers is on and a song has multiple singers — one branch per singer
// so the song is filed under each. The Singer/Inst split emits "Instrumentals",
// or "Singers" + the styled singer name. `range` flags a name segment that wants
// the [YY]/[YYYY] year prefix, resolved in buildPathsFor once the path is known.
namespace
{
    struct PathSeg { std::wstring name; bool range; NameMode mode; };
    using Branch = std::vector<PathSeg>;
}

// Sanitized, formatted singer names for a song, deduped of empties. When
// `normalize` is set, Spanish accents are folded out first (matching segmentFor).
static std::vector<std::wstring> singerSegments (const std::wstring& singer, NameMode mode,
                                                 bool normalize)
{
    std::vector<std::wstring> out;
    for (const auto& nm : formatNamesList (singer, mode))
    {
        std::wstring s = normalize ? normalizeLatinAccents (nm) : nm;
        s = sanitizeSegment (s);
        if (!s.empty()) out.push_back (s);
    }
    return out;
}

static std::vector<Branch> branchesFor (const TigerFoldersPlugin* self,
                                        const Component& c, const ScannedSong& s)
{
    std::vector<Branch> branches;
    const bool split = self->splitMultiSingers;
    const bool range = isRangeMode (c.nameMode);

    // Rhythm filter applies to every field, including the Singer/Inst split:
    // a non-matching song passes straight through this level.
    if (!componentApplies (c, s)) { branches.push_back (Branch {}); return branches; }

    if (c.field == Field::VocalSplit)
    {
        if (s.instrumental)
        {
            branches.push_back ({ { L"Instrumentals", false, c.nameMode } });
            return branches;
        }
        if (split)
        {
            auto names = singerSegments (s.singer, c.nameMode, self->normalizeSpanish);
            if (names.size() > 1)
            {
                for (const auto& nm : names)
                    branches.push_back ({ { L"Singers", false, c.nameMode },
                                          { nm, range, c.nameMode } });
                return branches;
            }
        }
        Branch b { { L"Singers", false, c.nameMode } };
        std::wstring nm = formatNameField (s.singer, c.nameMode);
        if (self->normalizeSpanish) nm = normalizeLatinAccents (nm);
        nm = sanitizeSegment (nm);
        if (!nm.empty()) b.push_back ({ nm, range, c.nameMode });
        branches.push_back (std::move (b));
        return branches;
    }

    if (c.field == Field::Singer && split && !s.instrumental)
    {
        auto names = singerSegments (s.singer, c.nameMode, self->normalizeSpanish);
        if (names.size() > 1)
        {
            for (const auto& nm : names)
                branches.push_back ({ { nm, range, c.nameMode } });
            return branches;
        }
    }

    std::wstring seg = self->segmentFor (c, s);
    if (seg.empty()) { branches.push_back (Branch {}); return branches; }   // skip → pass-through
    bool wantRange = (c.field == Field::Bandleader || c.field == Field::Singer) && range;
    branches.push_back ({ { seg, wantRange, c.nameMode } });
    return branches;
}

// Aggregate the min–max recording year for every [YY]/[YYYY] name group, keyed by
// the folder path up to and including that person's bare name. Must run before
// buildPathFor for the current `songs`/`components`.
void TigerFoldersPlugin::computeSingerYearRanges()
{
    singerYearRanges.clear();

    bool any = false;
    for (const auto& c : components)
        if (isRangeMode (c.nameMode)
            && (c.field == Field::Bandleader || c.field == Field::Singer
                || c.field == Field::VocalSplit)) { any = true; break; }
    if (!any) return;

    for (const auto& s : songs)
    {
        int yr = parseYear4 (s.year);
        if (yr <= 0) continue;

        // Walk every branch in parallel, accumulating the bare-name path that keys
        // a year range, so split singers each get their own range.
        std::vector<std::wstring> bases { sanitizeSegment (rootName) };
        for (const auto& c : components)
        {
            std::vector<std::wstring> next;
            for (const auto& base : bases)
                for (const auto& br : branchesFor (this, c, s))
                {
                    std::wstring b = base;
                    for (const auto& part : br)
                    {
                        b = b.empty() ? part.name : (b + L"/" + part.name);
                        if (part.range)
                        {
                            auto it = singerYearRanges.find (b);
                            if (it == singerYearRanges.end()) singerYearRanges[b] = { yr, yr };
                            else { if (yr < it->second.first)  it->second.first  = yr;
                                   if (yr > it->second.second) it->second.second = yr; }
                        }
                    }
                    next.push_back (std::move (b));
                }
            bases = std::move (next);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Full path for a song (root / seg / seg / ...). Empty segments are skipped.
// ─────────────────────────────────────────────────────────────────────────────

std::vector<std::wstring> TigerFoldersPlugin::buildPathsFor (const ScannedSong& s) const
{
    struct Partial { std::wstring path; std::wstring base; };   // base = bare-name key
    std::wstring root = sanitizeSegment (rootName);
    std::vector<Partial> partials { { root, root } };

    for (const auto& c : components)
    {
        std::vector<Partial> next;
        for (const auto& pt : partials)
            for (const auto& br : branchesFor (this, c, s))
            {
                std::wstring path = pt.path, base = pt.base;
                for (const auto& part : br)
                {
                    base = base.empty() ? part.name : (base + L"/" + part.name);
                    std::wstring finalSeg = part.name;
                    if (part.range)
                    {
                        auto it = singerYearRanges.find (base);
                        if (it != singerYearRanges.end())
                            finalSeg = formatYearRange (it->second, part.mode, singleYearRange) + part.name;
                    }
                    path = path.empty() ? finalSeg : (path + L"/" + finalSeg);
                }
                next.push_back ({ std::move (path), std::move (base) });
            }
        partials = std::move (next);
    }

    // Collapse paths that coincide (e.g. the same singer credited twice).
    std::vector<std::wstring> out;
    std::set<std::wstring> seen;
    for (auto& pt : partials)
        if (seen.insert (pt.path).second)
            out.push_back (std::move (pt.path));
    return out;
}

std::wstring TigerFoldersPlugin::buildPathFor (const ScannedSong& s) const
{
    auto paths = buildPathsFor (s);
    return paths.empty() ? std::wstring() : paths.front();
}

// A song is "unfiled" when no component produced a segment — it lands directly
// in the root folder rather than a subfolder.
bool TigerFoldersPlugin::isUnfiled (const ScannedSong& s) const
{
    for (const auto& c : components)
        if (!segmentFor (c, s).empty())
            return false;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Folder exclusions — return the deepest prefix of `full` whose every folder is
//  checked. A song destined for an unchecked folder therefore falls back to its
//  nearest checked ancestor (empty result = lands at the MyLists parent).
// ─────────────────────────────────────────────────────────────────────────────

std::wstring TigerFoldersPlugin::effectivePath (const std::wstring& full) const
{
    if (excludedFolders.empty()) return full;

    std::wstring accum;
    size_t start = 0;
    while (start <= full.size())
    {
        size_t slash = full.find (L'/', start);
        std::wstring part = (slash == std::wstring::npos)
            ? full.substr (start) : full.substr (start, slash - start);
        if (!part.empty())
        {
            std::wstring cand = accum.empty() ? part : (accum + L"/" + part);
            if (excludedFolders.count (cand)) break;   // stop before the excluded folder
            accum = cand;
        }
        if (slash == std::wstring::npos) break;
        start = slash + 1;
    }
    return accum;
}

// ─────────────────────────────────────────────────────────────────────────────
//  "Other" small-folder cutoff
//
//  Builds a map from each song destination path to the folder it should actually
//  land in once small folders are folded into a sibling "Other". Empty when the
//  cutoff is off, so callers skip the remap entirely.
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
    // Parent of a "a/b/c" path → "a/b" (empty if top-level / no slash).
    std::wstring parentPath (const std::wstring& p)
    {
        size_t slash = p.rfind (L'/');
        return (slash == std::wstring::npos) ? std::wstring() : p.substr (0, slash);
    }
}

std::map<std::wstring, std::wstring> TigerFoldersPlugin::computeOtherFolding() const
{
    std::map<std::wstring, std::wstring> remap;
    if (folderCutoffMode == CutoffMode::None) return remap;

    int cutoff = (folderCutoffSize < 2) ? 2 : (folderCutoffSize > 10 ? 10 : folderCutoffSize);

    // subtreeCount[node] = songs filed at the node or anywhere below it.
    // nonLeaf = every node that is a proper ancestor of some path (i.e. has children).
    std::map<std::wstring, int> subtreeCount;
    std::set<std::wstring>      nonLeaf;
    std::set<std::wstring>      destPaths;   // distinct song destinations

    for (const auto& s : songs)
        for (const auto& path : buildPathsFor (s))
        {
            if (path.empty()) continue;
            destPaths.insert (path);

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
                    subtreeCount[accum]++;
                    if (slash != std::wstring::npos) nonLeaf.insert (accum);
                }
                if (slash == std::wstring::npos) break;
                start = slash + 1;
            }
        }

    if (folderCutoffMode == CutoffMode::Leaf)
    {
        // Only the deepest folders fold; a song's destination IS its leaf folder.
        for (const auto& p : destPaths)
        {
            if (nonLeaf.count (p)) continue;               // has children → keep
            std::wstring parent = parentPath (p);
            if (parent.empty()) continue;                  // top-level → no "Other"
            if (subtreeCount[p] <= cutoff)
                remap[p] = parent + L"/Other";
        }
        return remap;
    }

    // CutoffMode::Any — fold the shallowest folder whose whole subtree is small.
    // Sort candidate nodes by depth so an outer fold subsumes inner ones.
    std::vector<std::wstring> nodes;
    nodes.reserve (subtreeCount.size());
    for (const auto& kv : subtreeCount) nodes.push_back (kv.first);
    std::sort (nodes.begin(), nodes.end(),
               [] (const std::wstring& a, const std::wstring& b) {
                   long da = (long) std::count (a.begin(), a.end(), L'/');
                   long db = (long) std::count (b.begin(), b.end(), L'/');
                   return (da != db) ? (da < db) : (a < b);
               });

    std::set<std::wstring> foldPoints;
    for (const auto& n : nodes)
    {
        if (parentPath (n).empty()) continue;              // the root itself never folds
        if (subtreeCount[n] > cutoff) continue;
        // Skip if an ancestor already folds (the outer fold wins).
        bool covered = false;
        std::wstring accum; size_t start = 0;
        while (start <= n.size())
        {
            size_t slash = n.find (L'/', start);
            std::wstring part = (slash == std::wstring::npos)
                ? n.substr (start) : n.substr (start, slash - start);
            if (!part.empty())
            {
                accum = accum.empty() ? part : (accum + L"/" + part);
                if (accum != n && foldPoints.count (accum)) { covered = true; break; }
            }
            if (slash == std::wstring::npos) break;
            start = slash + 1;
        }
        if (!covered) foldPoints.insert (n);
    }

    // Each destination remaps to the "Other" sibling of its shallowest fold ancestor.
    for (const auto& p : destPaths)
    {
        std::wstring accum; size_t start = 0; std::wstring foldAt;
        while (start <= p.size())
        {
            size_t slash = p.find (L'/', start);
            std::wstring part = (slash == std::wstring::npos)
                ? p.substr (start) : p.substr (start, slash - start);
            if (!part.empty())
            {
                accum = accum.empty() ? part : (accum + L"/" + part);
                if (foldPoints.count (accum)) { foldAt = accum; break; }
            }
            if (slash == std::wstring::npos) break;
            start = slash + 1;
        }
        if (!foldAt.empty())
            remap[p] = parentPath (foldAt) + L"/Other";
    }
    return remap;
}

// Recompute per-row excluded / ancestor-excluded flags from `excludedFolders`.
void TigerFoldersPlugin::applyExclusionFlags()
{
    for (auto& r : previewRows)
    {
        r.excluded = excludedFolders.count (r.path) > 0;

        r.ancestorExcluded = false;
        std::wstring accum;
        size_t start = 0;
        while (start <= r.path.size())
        {
            size_t slash = r.path.find (L'/', start);
            std::wstring part = (slash == std::wstring::npos)
                ? r.path.substr (start) : r.path.substr (start, slash - start);
            if (!part.empty())
            {
                std::wstring cand = accum.empty() ? part : (accum + L"/" + part);
                if (cand != r.path && excludedFolders.count (cand)) { r.ancestorExcluded = true; break; }
                accum = cand;
            }
            if (slash == std::wstring::npos) break;
            start = slash + 1;
        }
    }
}
