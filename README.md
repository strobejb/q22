# q22

[![Build and Test](https://github.com/strobejb/q22/actions/workflows/build.yml/badge.svg)](https://github.com/strobejb/q22/actions/workflows/build.yml)
[![Release](https://github.com/strobejb/q22/actions/workflows/release.yml/badge.svg)](https://github.com/strobejb/q22/actions/workflows/release.yml)

q22 is a cross-platform Qt 6 hex editor from Catch22, and the successor to the
original Catch22 HexEdit Win32 application.

The core file editing and hex view capabilities are carried forward from the
legacy application, while the application UX has been largely
rewritten in Qt6. The goal is to keep the direct, capable binary-editing model
of the original HexEdit while modernising the UI, settings, theming, packaging,
and cross-platform foundations.

The original Catch22 HexEdit project lived at:

- https://www.catch22.net/software
- https://github.com/strobejb/HexEdit

## Features

Core editing:

- Open and edit files as raw bytes, independent of the file format.
- Large-file oriented editing model inherited from the original HexEdit.
- Cut, copy, paste, insert, delete, undo, redo, and select-all operations.
- Flexible display options including hex, decimal, octal, and binary views.
- 8-bit, 16-bit, 32-bit, and 64-bit data grouping.
- Find, find next/previous, and goto tools.
- (Replace is not currently implemented in the Qt 6 version).

Custom types and structure viewer:

- Type/structure viewing is not currently implemented in the Qt 6 version.
- The original Win32 HexEdit TypeView provided a structured view over raw file bytes using C-style structure definitions.

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

Qt 6 application shell:

- File properties, checksum calculation, and string extraction panels.
- Recent files, configurable preferences, palettes, and light/dark/system theme modes.
- Windows and Linux build/release workflows.

## Status

This repository is the active Qt 6 successor to Catch22 HexEdit. Some low-level
editing and rendering code still reflects the legacy application's architecture;
much of the application shell, dialogs, side panels, theming, and platform
integration has been rebuilt around Qt.

The application is named q22. "Catch22 Hex Editor" is the descriptive name used
in packaging and metadata.

## Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## License

q22 is licensed under GPL-3.0-or-later. The full license text is included in
[LICENSE](LICENSE).

Releases are created manually from the GitHub Actions **Release** workflow using a `MAJOR.MINOR.PATCH` version.
