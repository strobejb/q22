# q22

[![Build and Test](https://github.com/strobejb/q22/actions/workflows/build.yml/badge.svg)](https://github.com/strobejb/q22/actions/workflows/build.yml)
[![Release](https://github.com/strobejb/q22/actions/workflows/release.yml/badge.svg)](https://github.com/strobejb/q22/actions/workflows/release.yml)

q22 is a cross-platform binary file editor from Catch22, and the successor to the
original Catch22 HexEdit application for Windows.

The core file editing and hex view capabilities are carried forward from the
legacy application, while the application UX has been largely
rewritten in Qt6. The goal is to keep the direct, capable binary-editing model
of the original HexEdit while modernising the UI, settings, theming, packaging,
and cross-platform foundations.

The original Catch22 HexEdit project lived at:

- https://www.catch22.net/software
- https://github.com/strobejb/HexEdit

## Features

Editing:

- Open and edit binary files directly, including large file support.
- Cut, copy, paste, insert, delete, undo, redo.
- View data in hex, decimal, octal, binary, and text forms.
- Group bytes as 8-bit, 16-bit, 32-bit, or 64-bit values.
- Search and jump to offsets without leaving the editor.

Structure View:

- Inspect files through named C-style structures instead of raw offsets.
- Built-in views for PE and ELF files.
- Expand headers, sections, imports, exports, symbols, and related tables.
- Automatically choose known formats from file type or magic bytes.
- Add custom `.struct` definitions using the Strata language.

Disassembly:

- Disassemble code!
- Supports x86 64-bit, x86 32-bit, ARM, ARM Thumb, and AArch64.
- Detects PE and ELF entrypoints and follows reachable code.
- Browse discovered functions and jump between disassembly and bytes.

Bookmarks and annotations:

- Bookmarks and highlights within the hex display.
- Annotate and highlight byte ranges.
- Fast access to saved bookmarked positions through the bookmark viewer.
- Bookmark state is persisted for future use.

Clipboard, import, and export formats:

- Copy-as and paste-special workflows for moving data between text and binary forms.
- Import and export files or clipboard data using multiple formats.
- Hex, ASCII/text, and raw data.
- Base64 and UUEncode.
- Intel HEX and Motorola S-records.
- HTML.
- C++, C, and assembler-style data.

UX:

- File properties, checksum calculation, and string extraction panels.
- Recent files and configurable preferences.
- Light, dark, and system theme modes.
- Built-in palettes plus custom palette creation and editing.
- Custom titlebar, docked panels, and platform-specific window chrome for
  Windows, KDE/KWin, and GNOME/Mutter.
- Windows and Linux build/release workflows.

## Status

This repository is the active Qt6 successor to Catch22 HexEdit. The core hex
view/editor lineage is inherited from the legacy application, while the
application shell, theming, docked panels, Structure View, Strata definitions,
disassembler, and platform integration have been rebuilt around Qt.

The application is named q22. "Catch22 Hex Editor" is the descriptive name used
in packaging and metadata.

## Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## License

Copyright (C) 2001-2026 James Brown.

q22 is licensed under GPL-3.0-or-later. The full license text is included in
[LICENSE](LICENSE).

Releases are created manually from the GitHub Actions **Release** workflow using a `MAJOR.MINOR.PATCH` version.
