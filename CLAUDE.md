# CLAUDE.md

Guidance for Claude Code when working in this repository.

## Build

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64    # first time only
cmake --build build --config Release
# Output: build/Release/TigerFolders.dll (auto-copied to the VDJ plugin folder)
```

No tests, no linter. Rebuild after every change and load the DLL in VirtualDJ to
test. The post-build copy to `%LOCALAPPDATA%\VirtualDJ\Plugins64\SoundEffect\TigerFolders\`
is best-effort and harmless if VDJ is absent.

## Architecture

Windows-only VDJ native plugin (`IVdjPluginDsp8`, MODULE DLL, **no JUCE**). It is
a UI-only plugin: `OnProcessSamples` is a no-op; it lives under `SoundEffect` so
VDJ loads it via the DSP IID. It builds VDJ virtual folders from the tags of the
songs under the selected browser folder.

### Source files

| File | Responsibility |
|---|---|
| `TigerFolders.h` | Plugin class, `Component`/`ScannedSong`/`PreviewRow` structs, enums, `CtrlId`, layout constants |
| `TigerFolders.cpp` | Lifecycle (`OnLoad`/`OnGetUserInterface`/`Release`), VDJ wrappers, settings INI, component (de)serialization, `DllGetClassObject` |
| `TigerFoldersPath.cpp` | `segmentFor` / `buildPathFor` — name formats, year buckets, grouping normalize, instrumental handling |
| `TigerFoldersScan.cpp` | `scanSelectedFolder` (recurse_folder → file_count → browser_scroll loop), `rebuildPreview` (tree + counts) |
| `TigerFoldersWrite.cpp` | Virtual-folder engine adapted from tigertag-vst: create `.vdjfolder`/`.subfolders`/`order`, append song refs, `add_virtualfolder`; plus `managedRootExists`/`removeManagedRoot` for Rebuild |
| `TigerFoldersUI.cpp` | `FoldersWndProc`, two-column owner-drawn UI, add-component row, draggable component list (subclassed), preview tree |
| `TigerFoldersHelpers.h/.cpp` | `TCol` colors, UTF/string utils, person-name parsing, GDI helpers, MyLists root discovery |
| `vdjPlugin8.h`, `vdjDsp8.h` | VDJ SDK headers (do not modify) |

### Data model

A `Component` = `{ field, mode }`. Fields: Genre, Bandleader, Singer, Grouping,
Label, Year, Album. The ordered `components` vector maps root → leaf. Per-song
path is built by `buildPathFor`, skipping empty segments.

- Bandleader/Singer parsed from the artist tag split on `" - "`.
- Instrumental = singer equals `Instrumental` (case-insensitive).
- Grouping has scope (All / Instrumental-only) and value (Exact / Normalize →
  Tango/Vals/Milonga/Cortina/Other).
- Year buckets (2/5/10) are grid-aligned.

### Build flow

Scan & Preview → in-memory tree (no writes). Build → if the managed root already
exists, prompt Merge vs Rebuild (`MessageBox`), then write `.vdjfolder` files and
register folders with VDJ.

### Key gotchas (MSVC / Win32)

- Don't use `std::min`/`std::max` (Windows.h macros) — use a ternary.
- Owner-draw buttons (`BS_OWNERDRAW`), comboboxes (`CBS_OWNERDRAWFIXED`), and
  listboxes (`LBS_OWNERDRAWFIXED`) are all drawn in `WM_DRAWITEM`; `WM_MEASUREITEM`
  sets item heights.
- The component list is drag-reordered via a comctl32 subclass
  (`SetWindowSubclass`) using `LB_ITEMFROMPOINT`.
- Settings persist to `tigerfolders_settings.ini` beside the DLL.
- Virtual folders are written under `…/VirtualDJ/MyLists/` as `<name>.vdjfolder`
  with nested `<name>.subfolders/` directories and per-directory `order` files.
