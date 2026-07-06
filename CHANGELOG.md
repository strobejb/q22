# Changelog

## 3.0.1 - 2026-07-06

### Added

- First public Q22 release package for Linux and Windows.
- Qt 6 desktop application based on the original Catch22 HexEdit editing model.
- Hex editor with overwrite and insert editing, undo/redo, selection, search, goto, and bookmark workflows.
- File properties, checksums, string extraction, and entropy/statistics panels.
- Structure View for inspecting PE, ELF, and custom Strata structure definitions.
- Disassembly panel using Capstone, with x86, x86-64, ARM, ARM Thumb, and AArch64 support.
- Function discovery, function bookmarks, and HexView "Disassemble This" integration.
- Light, dark, and system theme modes with custom palettes.
- Linux AppImage and Windows zip release artifacts.

### Changed

- Modernized the UI around Qt docked panels, custom status bar controls, preferences, and platform-aware window chrome.
- Added GNOME/Mutter and KDE/KWin-specific frameless-window handling.
- Added AppStream metadata for Linux packaging.
- Added third-party notices for Qt, Capstone, and GNOME/Adwaita icons.

### Notes

- Linux packaging currently targets AppImage.
- Windows packages are unsigned zip archives.
- This release should be smoke-tested on Fedora GNOME, KDE Plasma, Windows 11, and an Ubuntu LTS AppImage run before publication.

