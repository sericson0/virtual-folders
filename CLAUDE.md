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
| `TigerFoldersPath.cpp` | `segmentFor` / `buildPathFor` — name formats, year buckets, genre rhythm-normalize, grouping instrumental scope |
| `TigerFoldersScan.cpp` | `scanBegin` (deterministic recurse_folder), `scanSettleStep` (wait for async flatten), `scanStep` (browser_scroll loop, self-driven via `WM_APP_SCANSTEP` — not WM_TIMER — to beat the ~15ms tick floor; progress repaints throttled to ~20/sec), `rebuildPreview` (tree + counts) |
| `TigerFoldersWrite.cpp` | Virtual-folder engine adapted from tigertag-vst: create `.vdjfolder`/`.subfolders`/`order`, append song refs, `add_virtualfolder`; plus `managedRootExists`/`removeManagedRoot` for Rebuild |
| `TigerFoldersUI.cpp` | `FoldersWndProc`, two-column owner-drawn UI, add-component row, draggable component list (subclassed), preview tree |
| `TigerFoldersHelpers.h/.cpp` | `TCol` colors, UTF/string utils, person-name parsing, GDI helpers, MyLists root discovery |
| `vdjPlugin8.h`, `vdjDsp8.h` | VDJ SDK headers (do not modify) |

### Data model

A `Component` = `{ field, mode }`. Fields: Genre, Bandleader, Singer, Grouping,
Label, Year, Album. The ordered `components` vector maps root → leaf. Per-song
path is built by `buildPathFor`, skipping empty segments.

- Bandleader/Singer parsed from the artist tag split on `" - "`.
- Instrumental = singer equals `Instrumental` (case-insensitive). A Singer
  component returns an empty segment for instrumentals (they have no singer).
- Genre has a value mode (Exact / Normalize → Tango/Vals/Milonga/Cortina/Other).
- Grouping has scope only (All / Instrumental-only).
- Year buckets (2/5/10) are grid-aligned and have a scope (All / Instrumental-only).
- Bandleader/Singer name modes include `[YY]`/`[YYYY]`, which prepend the group's
  min–max recording year (computed by `computeSingerYearRanges` into
  `singerYearRanges`, keyed by the parent path + bare name).
- `VocalSplit` ("Singers/Instrumentals") emits a single `Instrumentals`/`Singers`
  segment so later components fill each branch.
- Preview folders carry an include/exclude checkbox; on build, songs under an
  unchecked folder fall back to the nearest checked ancestor (`effectivePath`).
  Exclusions persist as `excluded=` lines in the settings INI.

The dialog floats above VDJ via a 250ms `TIMER_KEEPALIVE` that re-asserts
`HWND_TOP` while `dialogRequestedOpen` is set, so clicking VDJ browser folders
no longer hides it. `WM_SHOWWINDOW` syncs the open intent; `suppressNextHideSync`
swallows self-triggered hides.

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
