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
//  Grouping normalize
// ─────────────────────────────────────────────────────────────────────────────

static std::wstring normalizeGrouping (const std::wstring& grouping)
{
    std::wstring g = toLowerW (grouping);
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
        case Field::Genre:      seg = s.genre;   break;
        case Field::Label:      seg = s.label;   break;
        case Field::Album:      seg = s.album;   break;
        case Field::Bandleader: seg = formatNameField (s.bandleader, c.nameMode); break;
        case Field::Singer:     seg = formatNameField (s.singer,     c.nameMode); break;
        case Field::Year:       seg = yearBucket (s.year, c.yearMode); break;
        case Field::Grouping:
        {
            // Instrumental-only scope: vocal tracks skip this level entirely.
            if (c.groupScope == GroupScope::Instrumental && !s.instrumental)
                return {};
            seg = (c.groupValue == GroupValue::Normalize)
                ? normalizeGrouping (s.grouping)
                : s.grouping;
            break;
        }
    }
    return sanitizeSegment (seg);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Full path for a song (root / seg / seg / ...). Empty segments are skipped.
// ─────────────────────────────────────────────────────────────────────────────

std::wstring TigerFoldersPlugin::buildPathFor (const ScannedSong& s) const
{
    std::wstring path = sanitizeSegment (rootName);

    for (const auto& c : components)
    {
        std::wstring seg = segmentFor (c, s);
        if (seg.empty()) continue;
        if (path.empty()) path = seg;
        else              path += L"/" + seg;
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
