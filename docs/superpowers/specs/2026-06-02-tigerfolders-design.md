# TigerFolders — Virtual Folder Builder for VirtualDJ

**Date:** 2026-06-02
**Status:** Design approved, pending spec review

## Overview

TigerFolders is a Windows-only VirtualDJ native plugin that reads the tags of
every song under a selected browser folder (recursively) and builds out a tree
of VDJ **virtual folders**, filing each song into a folder path derived from its
tags. The folder structure is fully user-configurable through an ordered list of
**components** (tag field + display mode).

The plugin only *references* songs (it adds `<song path="…"/>` entries to
`.vdjfolder` files) — it never moves, renames, or edits files on disk.

It reuses the virtual-folder writing engine from `tigertag-vst` and the UI
aesthetic / plugin scaffolding from `tigertanda-vdj`.

## Goals

- Let a tango DJ define a folder hierarchy from song tags and generate it in VDJ.
- Process the currently selected browser folder **and all its subfolders**.
- Preview the resulting tree before writing anything.
- Be non-destructive by default (merge), with an explicit rebuild option.

## Non-Goals (YAGNI)

- Curated-database matching (this reads the song's own tags, not a CSV).
- File renaming / tag editing / writing back to songs.
- Fields beyond the seven listed below (no BPM, Key, Rating, Composer, folder name).
- macOS build.
- Single-song / live-browsed mode (this is a batch tool).

## Architecture

Windows-only VDJ native plugin (`IVdjPlugin8`, MODULE DLL, **no JUCE**).

| Concern | Source repo | Reused pieces |
|---|---|---|
| UI shell & aesthetic | `tigertanda-vdj` | Dark owner-drawn Win32 window, two-column layout, gear→Settings toggle, Segoe UI, GDI helpers (`fillRect`/`drawText`/`createFont`), `TCol` colors, INI settings beside the DLL, 250 ms timer pattern |
| Virtual-folder engine | `tigertag-vst` (`TigerTagVirtualFolder.cpp`) | `ensureVirtualFolderListFileExists` (writes `.vdjfolder` + `.subfolders/` + `order`), `ensureVirtualFolderPathExists`, `appendSongToVirtualFolderListFile` (dedup + XML-escape), `add_virtualfolder` registration, MyLists root discovery (`getVdjMyListsRootCandidates`/`getPreferredVdjMyListsRoot`) |
| Name parsing | `tigertag-vst` (`TangoMatcher`) | `getLeaderLastName`, `getLeaderFirstName`, `getSingerLastNames` — extended to all five name formats |
| Folder scanning | `tigertag-vst` batch pattern | `recurse_folder` → `file_count` → `browser_scroll 'top'` then `+1` loop, reading `get_browsed_song 'field'` and `get_browsed_filepath` per row |

### Proposed source files (`Source/`)

| File | Responsibility |
|---|---|
| `TigerFolders.h` | Plugin class, `CtrlId` enum, layout constants, `Component`/`ScannedSong`/`TreeNode` structs |
| `TigerFolders.cpp` | Plugin lifecycle (`OnLoad`, `OnGetPluginInfo`, `Release`), VDJ wrappers (`vdjSend`/`vdjGetString`/`vdjGetValue`), INI load/save |
| `TigerFoldersUI.cpp` | Window proc, `WM_PAINT`/`WM_DRAWITEM`/`WM_COMMAND`, `applyLayout`, the Add-component row, the component list, the preview tree |
| `TigerFoldersScan.cpp` | Recursive scan of the selected folder; reads tags into `ScannedSong`s |
| `TigerFoldersPath.cpp` | Component → path-segment logic (name formats, year buckets, grouping normalize, instrumental handling); builds the in-memory tree |
| `TigerFoldersWrite.cpp` | Adapted virtual-folder engine: create folders, append song refs, register |
| `TigerFoldersHelpers.h/.cpp` | String/UTF/GDI helpers carried over |
| `vdjPlugin8.h` | VDJ SDK header (unmodified) |

## Data Model

### Component

A **component** = `{ field, mode }`. The ordered component list (top → bottom)
defines folder nesting (**root → leaf**). Components can be reordered by drag.

| Field | Mode(s) | Notes |
|---|---|---|
| Genre | exact | |
| Label | exact | |
| Album | exact | |
| Bandleader | First Last · Last, First · Last · LAST · LAST, First | parsed from artist tag |
| Singer | First Last · Last, First · Last · LAST · LAST, First | parsed from artist tag |
| Grouping | combined: `{All\|Instrumental} · {Exact\|Normalize}` | single secondary dropdown |
| Year | 2 years · 5 years · 10 years (decade) | grid-aligned |

### Field derivation rules

- **Artist tag split:** the artist tag is split on `" - "` (space-hyphen-space).
  Left side = **bandleader**, right side = **singer**. If there is no `" - "`,
  the whole value is the bandleader and the singer is empty.
- **Instrumental detection:** a track is instrumental when its singer equals
  `"Instrumental"` (case-insensitive).
- **Name formats** (applied to bandleader or singer, reusing/extending the
  TangoMatcher helpers):
  - *First Last* — value as-is (or reconstructed from a "Last, First" tag).
  - *Last, First* — `Lastname, Firstname`.
  - *Last* — last name only.
  - *LAST* — last name uppercased.
  - *LAST, First* — uppercase last name + first name.
  - Multiple singers (joined by " and " / " y ") follow the existing
    `getSingerLastNames` multi-name behavior.
- **Grouping — Normalize:** maps the grouping value into one of
  `Tango / Vals / Milonga / Cortina / Other` by case-insensitive substring word
  match; anything unmatched (or empty) → `Other`.
- **Grouping — scope:** `Instrumental` scope means the component contributes a
  level **only for instrumental tracks**; for vocal tracks the level is skipped.
  `All` scope applies to every track.
- **Year buckets — grid-aligned** to fixed edges:
  - 2 years → `1940–1941, 1942–1943, …` (floor to even year)
  - 5 years → `1940–1944, 1945–1949, …`
  - 10 years → `1940–1949, …` (decade)
- **Empty / missing field:** that level is **skipped** — the song nests one level
  shallower (matching TigerTag's behavior). Illegal path characters are
  sanitized to `_`.

## UI

Dark, owner-drawn, two-column window with a gear toggle (Main ↔ Settings),
matching TigerTanda.

### Add-component row (single row, fixed ADD)

`Add component:` label above one row:

```
[ Field ▾ ] [ secondary ▾ ] ............... [ ADD → ]
```

- Primary dropdown lists the seven fields.
- On selecting a field, the secondary dropdown is **exposed** if the field has
  sub-options (Bandleader, Singer, Grouping, Year). Fields without sub-options
  (Genre, Label, Album) show no secondary.
- The secondary slot's horizontal space is **reserved** whether or not it is
  shown, so the **ADD button stays pinned at a fixed spot on the right** and
  never shifts between states.
- **ADD lights up** (amber) only when the selection is complete — i.e. a primary
  is chosen and, where applicable, a secondary. Otherwise it is dim/disabled.
- Clicking ADD appends a component token to the structure list.

### Structure list

Ordered, numbered, draggable list of components (root → leaf), each showing the
field + mode and an `×` to remove. Drag to reorder.

### Source & root

- Read-only display of the currently selected VDJ browser folder, labeled
  "+ subfolders".
- Editable **root folder name** (the top-level virtual folder under MyLists).
- **Scan & Preview** and **Build** buttons.

### Preview tree (right column)

Shows the computed virtual-folder tree with per-folder song counts. Populated by
Scan & Preview; nothing is written until Build.

### Persistence

Component list (fields + modes + order) and root-folder name persist to an INI
file next to the DLL (TigerTanda settings pattern).

## Build Flow

1. **Scan & Preview** — issue `recurse_folder` on the selected folder, read
   `file_count`, iterate rows (`browser_scroll`), read each song's tags +
   filepath into memory, compute each song's folder path from the component
   list, and render the tree + counts on the right. Nothing is written.
2. **Build** — if virtual folders from a prior run already exist under the
   managed root, **prompt the user each time** to choose:
   - **Merge** — create missing folders, append song references, dedupe; never
     delete. (Default-safe; matches TigerTag.)
   - **Rebuild** — clear the plugin's managed root first, then write fresh.
3. Write `.vdjfolder` / `.subfolders/` / `order` files, append `<song path>`
   references (deduped, XML-escaped), and register each level with VDJ via
   `add_virtualfolder` (both slash and backslash variants, as TigerTag does).

## Error Handling

- No exceptions; operations return `bool` / are guarded (C++17, `std::filesystem`).
- Empty selected folder or empty component list → no-op with a status message.
- MyLists root not found → status message, no write.
- Songs missing all configured fields land at the root (or are reported in a
  count of "unfiled" songs in the preview).

## Open Items For Implementation

- Exact drag-reorder mechanism for the component list (owner-drawn listbox vs.
  custom hit-testing) — implementation detail.
- Whether the merge/rebuild prompt is a native `MessageBox` or an in-window
  panel — implementation detail.
