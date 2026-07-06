# Q22 Smoke Tests

This is a manual pre-release checklist. It is not run by CTest.

## Platforms

- Fedora GNOME Wayland
- Fedora GNOME X11, if available
- KDE Plasma / KWin, Wayland or X11
- Windows 11
- Optional: Ubuntu LTS AppImage launch test

## Core Checks

- Launch Q22 from a terminal.
- Launch Q22 from the desktop launcher or packaged artifact.
- Open a small binary file.
- Open a large binary file.
- Edit bytes in overwrite mode.
- Edit bytes in insert mode.
- Delete bytes.
- Undo and redo edits.
- Save As, reopen the saved file, and verify the content.
- Search for bytes or text.
- Use the Goto panel.
- Use the bookmark dropdown in the Goto panel.
- Add, edit, activate, and delete a basic bookmark.
- Add and activate a function bookmark.
- Confirm function bookmark activation opens the disassembly panel.
- Open the File Statistics panel.
- Open Structure View on an ELF sample.
- Open Structure View on a PE sample.
- Open Disassembly on an ELF sample.
- Open Disassembly on a PE sample.
- Use Disassemble This from a selected byte range in HexView.
- Toggle File Info, Structure View, and Disassembly from the status bar.
- Resize the window aggressively.
- Switch light, dark, and system theme modes.
- Open Preferences, change a simple setting, close, and reopen.

## Platform-Specific Checks

- GNOME: no double shadow on frameless windows.
- GNOME: rounded corners look sane.
- GNOME: edge resize works without a transparent shadow margin.
- KDE: app-painted shadow is visible.
- KDE: resize via the shadow margin works.
- Windows: window chrome/titlebar looks correct.
- Windows: release zip runs on a clean or mostly clean machine.
- AppImage: file is executable and launches.
- AppImage: icon, desktop file, and AppStream metadata are present.

