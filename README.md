# TigerFolders

A VirtualDJ plugin that builds out a tree of **virtual folders** from the tags of
the songs in a selected browser folder (and all of its subfolders).

You define a folder structure as an ordered list of **components** (a tag field
plus a display mode), preview the resulting tree, and TigerFolders writes the
`.vdjfolder` files into VirtualDJ's `MyLists` so the structure shows up in the
browser. Songs are only *referenced* — nothing on disk is moved or renamed.

## Components

A component is a tag field plus a mode. The ordered list maps root → leaf.

| Field | Mode(s) |
|---|---|
| Genre, Label, Album | exact |
| Bandleader, Singer | First Last · Last, First · Last · LAST · LAST, First |
| Grouping | scope (All / Instrumental-only) × value (Exact / Normalize) |
| Year | 2 years · 5 years · 10 years (decade) — grid-aligned |

- **Bandleader / Singer** are read from the **artist tag** split on `" - "`
  (left = bandleader, right = singer). A track is *instrumental* when its singer
  is `Instrumental` (any case).
- **Grouping → Normalize** buckets into `Tango / Vals / Milonga / Cortina / Other`
  (case-insensitive word match). **Instrumental-only** scope skips the level for
  vocal tracks.
- **Year** buckets are aligned to a fixed grid (e.g. 5-year → 1940–1944, 1945–1949).
- A song with an empty value for a component simply skips that level.

## Usage

1. Build a structure on the left: pick a field, pick its mode, click **ADD →**.
   Drag rows to reorder (top = root, bottom = leaf). **Remove** / **Clear** edit
   the list.
2. Set the **Root** folder name (the top-level virtual folder).
3. Select a folder in VirtualDJ's browser, then click **Scan & Preview** — the
   right column shows the resulting tree with per-folder song counts.
4. Click **Build**. If folders under this root already exist, you'll be asked to
   **Merge** (add to existing) or **Rebuild** (wipe this root first).

Settings (your structure and root name) persist in `tigerfolders_settings.ini`
next to the DLL.

## Building

Windows-only. Requires CMake 3.22+ and MSVC (Visual Studio 2022).

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Output: `build/Release/TigerFolders.dll`, auto-copied to
`%LOCALAPPDATA%\VirtualDJ\Plugins64\SoundEffect\TigerFolders\` when VirtualDJ is
installed.

## Installing

1. Copy `TigerFolders.dll` to
   `%LOCALAPPDATA%\VirtualDJ\Plugins64\SoundEffect\TigerFolders\`.
2. Restart VirtualDJ.
3. Add TigerFolders to a Master Effects slot, then open it.

## Credits

Reuses the virtual-folder writing engine from
[tigertag-vst](https://github.com/sericson0/tigertag-vst) and the UI approach
from [tigertanda-vdj](https://github.com/sericson0/tigertanda-vdj).

## License

MIT
