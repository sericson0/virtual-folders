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

// Apply a name format across a possibly-multi-person field
// (joined by " and " / " y "), preserving the joiner as ", ".
static std::wstring formatNameField (const std::wstring& field, NameMode mode)
{
    std::wstring f = trimWs (field);
    if (f.empty()) return {};

    // Split on " and " / " y " (case-insensitive on the connector words).
    std::vector<std::wstring> people;
    std::wstring lower = toLowerW (f);
    size_t pos = 0;
    size_t cursor = 0;
    auto pushSeg = [&] (size_t end) {
        people.push_back (trimWs (f.substr (cursor, end - cursor)));
    };
    while (pos < lower.size())
    {
        size_t aPos = lower.find (L" and ", pos);
        size_t yPos = lower.find (L" y ", pos);
        size_t aHit = (aPos == std::wstring::npos) ? lower.size() : aPos;
        size_t yHit = (yPos == std::wstring::npos) ? lower.size() : yPos;
        size_t hit = (aHit < yHit) ? aHit : yHit;
        if (hit >= lower.size()) break;
        size_t connLen = (hit == aPos) ? 5 : 3;   // " and " vs " y "
        pushSeg (hit);
        cursor = hit + connLen;
        pos = cursor;
    }
    pushSeg (f.size());

    std::wstring out;
    for (const auto& person : people)
    {
        std::wstring formatted = formatOneName (person, mode);
        if (formatted.empty()) continue;
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
//  Component → segment
// ─────────────────────────────────────────────────────────────────────────────

std::wstring TigerFoldersPlugin::segmentFor (const Component& c, const ScannedSong& s) const
{
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

// "41-43 " (short) or "1941-1943 " (long); a single year collapses to "41 ".
static std::wstring formatYearRange (const std::pair<int,int>& r, NameMode mode)
{
    int lo = r.first, hi = r.second;
    if (lo <= 0) lo = hi;
    if (hi <= 0) hi = lo;
    if (lo <= 0) return {};

    if (mode == NameMode::YearRangeShort)
        return (lo == hi) ? (twoDigit (lo) + L" ")
                          : (twoDigit (lo) + L"-" + twoDigit (hi) + L" ");
    return (lo == hi) ? (std::to_wstring (lo) + L" ")
                      : (std::to_wstring (lo) + L"-" + std::to_wstring (hi) + L" ");
}

// The bare folder segment(s) a component contributes for a song. Most fields emit
// 0 or 1 segment; the Singer/Inst split emits "Instrumentals", or "Singers" plus
// the styled singer name (two segments). `range` flags a name segment that wants
// the [YY]/[YYYY] year prefix, resolved in buildPathFor once the path is known.
namespace { struct PathSeg { std::wstring name; bool range; NameMode mode; }; }

static std::vector<PathSeg> partsFor (const TigerFoldersPlugin* self,
                                      const Component& c, const ScannedSong& s)
{
    std::vector<PathSeg> out;

    if (c.field == Field::VocalSplit)
    {
        if (s.instrumental)
            out.push_back ({ L"Instrumentals", false, c.nameMode });
        else
        {
            out.push_back ({ L"Singers", false, c.nameMode });
            std::wstring nm = sanitizeSegment (formatNameField (s.singer, c.nameMode));
            if (!nm.empty()) out.push_back ({ nm, isRangeMode (c.nameMode), c.nameMode });
        }
        return out;
    }

    std::wstring seg = self->segmentFor (c, s);
    if (seg.empty()) return out;
    bool range = (c.field == Field::Bandleader || c.field == Field::Singer)
                 && isRangeMode (c.nameMode);
    out.push_back ({ seg, range, c.nameMode });
    return out;
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

        std::wstring base = sanitizeSegment (rootName);
        for (const auto& c : components)
            for (const auto& part : partsFor (this, c, s))
            {
                base = base.empty() ? part.name : (base + L"/" + part.name);
                if (part.range)
                {
                    auto it = singerYearRanges.find (base);
                    if (it == singerYearRanges.end()) singerYearRanges[base] = { yr, yr };
                    else { if (yr < it->second.first)  it->second.first  = yr;
                           if (yr > it->second.second) it->second.second = yr; }
                }
            }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Full path for a song (root / seg / seg / ...). Empty segments are skipped.
// ─────────────────────────────────────────────────────────────────────────────

std::wstring TigerFoldersPlugin::buildPathFor (const ScannedSong& s) const
{
    std::wstring path = sanitizeSegment (rootName);
    std::wstring base = path;   // bare-name accumulation = year-range lookup key

    for (const auto& c : components)
        for (const auto& part : partsFor (this, c, s))
        {
            base = base.empty() ? part.name : (base + L"/" + part.name);

            std::wstring finalSeg = part.name;
            if (part.range)
            {
                auto it = singerYearRanges.find (base);
                if (it != singerYearRanges.end())
                    finalSeg = formatYearRange (it->second, part.mode) + part.name;
            }
            path = path.empty() ? finalSeg : (path + L"/" + finalSeg);
        }
    return path;
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
