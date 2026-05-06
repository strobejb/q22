# qexed — Claude Code guidelines

qexed is a Qt 6 hex editor (port of HexEdit from Win32).  The codebase is C++
with platform-specific paths for Windows (`Q_OS_WIN`), KDE/KWin, and GNOME/Mutter.

## Platform / compositor rules (Linux)

These rules exist because a KDE shadow fix once regressed GNOME by changing the
default behaviour instead of adding a KDE-specific branch.

### GNOME / Mutter
- Mutter provides compositor shadows automatically for frameless ARGB windows.
- **Never** paint a self-drawn shadow or add a transparent shadow margin — it
  will produce a double shadow on top of Mutter's own.
- `paintEvent()` should draw only a rounded-rect background; transparent corners
  let Mutter clip its shadow to the window shape.

### KDE / KWin
- KWin does **not** auto-shadow frameless windows.
- The app must paint its own shadow: a gradient in a `kShadowSize`-pixel
  transparent margin, with `CornerClipper` repairing corners after child widgets
  overdraw them.

### Resize hit zone
On KDE the shadow margin (`kShadowSize = 18 px`) doubles as the edge-resize grab
zone — the user grabs the shadow to resize.  On non-KDE there is no shadow margin,
so the resize zone must be a narrow strip (`kResizeMargin = 5 px`).  The margin is
passed as a parameter to `edgesFromPos()` and selected at runtime with `isKDE()`.

### The golden rule
> Any fix that is KDE-specific **must** be gated behind `isKDE()`.  
> Never change the non-KDE default to accommodate KDE behaviour.

When touching the shadow / titlebar / compositor path, state explicitly which
desktop environments you have tested on and which you have only reasoned about.

## Icon system

### Loading priority — resources first, system second

Icons are always loaded from the embedded resource bundle first.  A system-provided
icon is only ever used as a fallback when a resource is absent.  This priority is
enforced in `recoloredIcon()` (`theme.cpp`):

```cpp
QIcon icon = QIcon(":/icons/hicolor/scalable/actions/" + name + ".svg"); // resource
if (icon.isNull())
    icon = QIcon::fromTheme(name); // system fallback only
```

**Never** reverse this order or skip the resource check.  The resource icons are
pure alpha-keyed SVGs; `SourceIn` compositing depends on this.  System icons (e.g.
KDE/Breeze) may return pre-coloured pixmaps that break `SourceIn` recolouring.

### Theme name and search path (main.cpp)

At startup, Qt's icon search path is redirected to the embedded resources:

```cpp
QIcon::setThemeSearchPaths({":/icons"});
QIcon::setThemeName("hicolor");
```

This makes `QIcon::fromTheme()` resolve against `:/icons/hicolor/` rather than
the host system's icon theme.  **Any new icon must be:**

1. Added as an SVG file under `resources/icons/hicolor/scalable/actions/`.
2. Registered in `resources/resources.qrc` — without this entry the file is not embedded
   and `QIcon(":/icons/…")` will return a null icon silently.

The theme name `"hicolor"` is declared in `resources/icons/hicolor/index.theme` (also
embedded).  If you rename the theme directory you must update both
`index.theme → Name=` and the `setThemeName()` call in `main.cpp`.

### Recolouring

`recoloredIcon(name, color, size)` (`theme.cpp`) loads the SVG, renders it to a
pixmap, then paints a solid colour over it using `CompositionMode_SourceIn`.  This
replaces every non-transparent pixel with `color` while preserving the alpha mask —
i.e. the icon silhouette is tinted to whatever foreground colour the current palette
requires.

`recolorToolButtons(parent)` (`theme.cpp`) iterates all `QToolButton` children of
`parent` and recolours them.  It resolves the icon name in this order:

1. The `iconThemeName` dynamic property on the button (set in `.ui` or cached by a
   prior call).
2. `btn->icon().name()` — the name Qt recorded when the icon was originally set.

If neither is available the button is skipped.  **When adding a new tool button**,
set its `iconThemeName` dynamic property in Qt Designer (string type) to the
icon's base name (e.g. `"document-open-symbolic"`) so recolouring works correctly
across theme/palette changes.

## ComboBox / QSS gotchas

This area caused significant iteration. The rules below encode hard-won knowledge
that should not need to be rediscovered.

### Drop-down arrow suppression
Any `QComboBox::drop-down` rule in QSS causes `QStyleSheetStyle` to suppress the
native `PE_IndicatorArrowDown` unless `::down-arrow` also provides an image.  The
fix is to **scope the rule to status-bar combos only**:

```css
QStatusBar QComboBox::drop-down { border: none; width: 20px; }
```

Dialog / plain combos are not descendants of `QStatusBar` so they receive normal
Fusion rendering including the native arrow.  Status-bar `ValueComboBox` subclasses
draw their own arrow on hover, so they don't need the native one.

### Border-width change and layout shift
Changing border width from 1 px to 2 px moves surrounding content.  The
**margin-compensation technique** prevents this: base and hover states use
`margin: 1px; border: 1px`, focus uses `margin: 0px; border: 2px`.  The lost
margin pixel absorbs the extra border pixel so nothing moves.

Do **not** also adjust `padding` to compensate — the margin already does it.
Adding both shifts text by 1 px (experienced in palette dialog line edits,
fixed in commit `7917e7e`).

### Open/popup state — two mechanisms coexist
Qt's built-in `:open` pseudo-state fires correctly for **standard Qt combo
popups**.  Use it for the background colour when the popup is open:

```css
QComboBox:open { background: palette(button); }
```

`DataTypeComboBox` and `ValueComboBox` use **custom `QMenu`-based popups**, so
`:open` never fires for them.  They rely on a `popupOpen` dynamic property that is
kept in sync inside `NoFocusRectStyle::drawComplexControl()` by detecting
`State_On` changes:

```cpp
combo->setProperty("popupOpen", open);
QMetaObject::invokeMethod(combo, [combo]() {
    combo->style()->unpolish(combo); combo->style()->polish(combo); combo->update();
}, Qt::QueuedConnection);  // deferred — do NOT polish mid-paint
```

The QSS covers both mechanisms:

```css
QComboBox:open { background: palette(button); }                 /* standard popup */
QComboBox[popupOpen="true"] { background: palette(button); }   /* custom QMenu popup */
```

### Adwaita bypasses PE_FrameFocusRect
Adwaita calls `drawPrimitive(PE_FrameFocusRect)` directly (not via proxy) for
`CC_ComboBox`, bypassing `NoFocusRectStyle`'s suppression of the dotted focus
rectangle.  Fix: in `drawComplexControl`, strip `State_HasFocus` from a copy of
the style option before forwarding.  The QSS `:focus` border provides visible
focus indication instead.

### I-beam cursor on hover
Fusion / QStyleSheetStyle leaves the text I-beam cursor active over combo boxes.
Fix: `cursor: arrow;` in the `QComboBox` base rule.

### Windows: ghost outline on combo dropdown lists (Windows only)
`DwmExtendFrameIntoClientArea({-1,-1,-1,-1})` (used by `menuShadowFilter` for
rounded menu shadows) creates a DWM glow that bleeds behind the straight edges of
rectangular dropdown lists, producing a ghost secondary outline.  Do **not** apply
`menuShadowFilter` to `QComboBoxPrivateContainer`.  Omitting
`NoDropShadowWindowHint` lets Windows draw the normal `CS_DROPSHADOW` rectangular
shadow, which is appropriate for a list and has no artefact.

### Same-click toggle guard
When a popup closes via Qt's auto-close (clicking the combo again), `showPopup()`
fires immediately after `aboutToHide`.  Without a guard the popup reopens
instantly.  `ValueComboBox::isSameClickReopen()` / `recordMenuClose()` track the
cursor position at close time; `showPopup()` bails if the click position matches.
`DataTypeComboBox::showPopup()` calls the same helpers via inheritance.

### Status-bar combos are scoped separately
`QStatusBar QComboBox` has a transparent background by default and only shows a
border/background on hover.  This rule block is deliberately separate from the
general `QComboBox` block — keep the two scopes distinct when editing styles.

## Docked find/goto panel focus

The Find and Goto panels are docked panels, not real modal dialogs.  Do not make
each panel trap focus independently.  They are hosted together in `DockPanelHost`,
which treats all visible hosted panels as one tab group while preserving the
existing layout and separator hairlines.

`DockPanelHost` owns the keyboard boundary behavior:

- `Tab` / `Shift+Tab` move through visible controls across both panels.
- Focus wraps within the hosted panel group instead of falling back into HexView.
- `Ctrl+Tab` explicitly returns focus to HexView.
- If one panel closes while another remains visible, focus should return to the
  remaining panel's edit field, not its left-most tool button.

## Preferences / palette-picker keyboard navigation

The Preferences dialog and palette overlays needed careful focus handling because
the palette swatch grid is rebuilt dynamically and does not behave like a simple
linear form.  Keep the focus rules centralized; avoid hard-coded “nth widget”
tab fixes.

- Call `FocusNavigation::assignTabOrder()` after the Preferences dialog is shown
  or after an overlay is embedded, once widget geometry and rebuilt swatches are
  valid.
- Use `PaletteSwatchGrid::setBoundaryWidgets()` to describe where focus should
  enter/leave the grid.  In the main Preferences page the grid sits between
  `ViewMoreButton` and the Font row; in the all-palettes overlay the back button
  is both the previous and next boundary.
- `PaletteSwatchGrid::setAllowFocusEscape(true)` is for the main Preferences
  page, where focus should continue through the dialog.  The all-palettes overlay
  uses `false` so tabbing wraps inside the overlay.
- The Preferences event filter handles the awkward edges: `View More` Tab/Down
  enters the first swatch, Shift+Tab from the first normal control wraps to the
  last dialog control, and Backtab from the Font row enters the last swatch.
- Swatch focus rings should appear for keyboard focus only (`TabFocusReason` /
  `BacktabFocusReason`), not mouse focus.
- When closing a slide overlay, defer refocusing the main swatch grid with
  `QTimer::singleShot(0, ...)` so reparenting/layout has settled.

## Build

```
cmake --build build/Desktop_Qt_6_10_2-Debug
```

## Key files

| File | Purpose |
|------|---------|
| `src/qexed/mainwindow.cpp` | Main window, custom titlebar, shadow painting, platform event handling |
| `src/qexed/theme.cpp` | Theming, colour system, `enableKWinShadow()`, menu/tooltip styling |
| `src/HexView/` | The hex editor widget |
