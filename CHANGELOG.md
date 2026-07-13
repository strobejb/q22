# Changelog

## 3.1.0 - 2026-07-13

### Added

* Added Android DEX Structure View support with raw DEX tables and a semantic summary for strings, types, fields, methods, and classes.
* Added Flattened Device Tree (DTB/FDT) Structure View support for firmware and embedded Linux images.
* Added Strata `uleb128` and `sleb128` primitive integer types for variable-width encodings used by DEX, WebAssembly-style formats, and related binary formats.
* Added Strata export `version(...)` metadata so user definitions can intentionally override built-in definitions.
* Added Qt Creator highlighting for canonical `.strata` files while keeping legacy `.struct` highlighting.

### Changed

* Renamed shipped Strata definitions from `.struct` to `.strata`; legacy `.struct` user definitions remain supported.
* Moved user Strata definitions to `~/.config/catch22/q22/strata`, with migration from the older `structs` directory.
* Moved q22 settings, palettes, bookmarks, and Strata files under the `catch22/q22` config directory.
* Changed Strata `magic(...)` metadata to use byte-sequence-first syntax, with the optional offset second.
* Improved duplicate built-in/user Strata definition logging, including clear picked/ignored entries.
* Updated Strata documentation, keyword reference, examples, and Qt Creator highlighter metadata.

### Fixed

* Improved rendering of tagged byte arrays as strings in Structure View.
* Improved decoded DEX string display, including modified UTF-8 handling and cleaner summary labels.
* Removed noisy early DEX dynamic rows that duplicated raw table data without adding useful names.
* Fixed stale copied built-in `.struct` files from being loaded beside renamed `.strata` definitions.

## 3.0.2 - 2026-07-07

### Added

* First public Q22 release package for Linux and Windows.
* Qt 6 desktop application based on the original Catch22 HexEdit editing model.
* Hex editor with overwrite and insert editing, undo/redo, selection, search, goto, and bookmark workflows.
* File properties, checksums, string extraction, and entropy/statistics panels.
* Structure View for inspecting PE, ELF, and custom Strata structure definitions.
* Disassembly panel using Capstone, with x86, x86-64, ARM, ARM Thumb, and AArch64 support.
* Function discovery, function bookmarks, and HexView "Disassemble This" integration.
* Light, dark, and system theme modes with custom palettes.
* Linux AppImage and Windows zip release artifacts.

### Changed

* Modernized the UI around Qt docked panels, custom status bar controls, preferences, and platform-aware window chrome.
* Added GNOME/Mutter and KDE/KWin-specific frameless-window handling.
* Added AppStream metadata for Linux packaging.
* Added third-party notices for Qt, Capstone, and GNOME/Adwaita icons.

### Notes

* Linux packaging currently targets AppImage.
* Windows packages are unsigned zip archives.
* This release should be smoke-tested on Fedora GNOME, KDE Plasma, Windows 11, and an Ubuntu LTS AppImage run before publication.
