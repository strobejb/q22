#ifndef PALETTES_H
#define PALETTES_H

#include <QColor>
#include "theme.h"
#include <QDialog>
#include <QHash>
#include <QList>
#include <QString>
#include "HexView/hexview.h"
#include "palettecolourpicker.h"
#include "settings/togglebuttongroup.h"

class QDir;
class HexView;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QAction;
class SettingsToggle;
class ScreenColorPicker;

// ── PaletteElem ───────────────────────────────────────────────────────────────
// Enumerates every colour slot editable in PaletteEditorDialog.
// Slots up to PE_BOOKMARK_7 are forwarded to HexView via applyPalette().
// Future UI-level colour slots (window chrome, toolbar, etc.) should be added
// after PE_BOOKMARK_7 and before PE_COUNT; add matching fields to PaletteInfo.
enum PaletteElem {
    // ── HexView colours ───────────────────────────────────────────────────
    PE_BG = 0,
    //PE_FG,
    PE_HEX_ODD,
    PE_HEX_EVEN,
    PE_ADDRESS,
    PE_ASCII,
    PE_SELECTION,
    PE_SELECTION_TEXT,
    PE_SELECTION_INACTIVE,
    PE_SELECTION_TEXT_INACTIVE,
    PE_MODIFIED,
    PE_MATCHED,
    PE_MATCH_SELECTED,
    PE_RESIZE_BAR,
    PE_BOOKMARK_1,
    PE_BOOKMARK_2,
    PE_BOOKMARK_3,
    PE_BOOKMARK_4,
    PE_BOOKMARK_5,
    PE_BOOKMARK_6,
    PE_BOOKMARK_7,

    // ── UI colours (not forwarded to HexView) ─────────────────────────────
    // (none yet — add fields to PaletteInfo and cases to elemName/colorAt/setColorAt)

    PE_WINDOW,
    PE_WINDOWTEXT,
    PE_HIGHLIGHT,
    PE_TOOLBAR,
    PE_PANELBORDERS,
    PE_COUNT
};

// ── PaletteInfo ───────────────────────────────────────────────────────────────
// Parsed representation of a .palette resource file.
struct PaletteInfo {
    QString name;
    QColor  bg;                // Background
    QColor  address;           // Address column text
    QColor  hexOdd;            // Odd-column hex digits
    QColor  hexEven;           // Even-column hex digits
    QColor  ascii;             // ASCII panel text
    QColor  selection;         // Selection background
    QColor  selectionText;     // Selection text foreground
    QColor  selectionInactive; // Selection background when unfocused
    QColor  selectionTextInactive; // Selection text when unfocused
    QColor  modified;          // Modified-byte indicator
    QColor  matched;           // Search-match highlight (background)
    QColor  matchSelected;     // Highlight when also selected; invalid = auto-mix selection+match
    QColor  resizeBar  ;         // Resize bar
    QColor  bookmarks[7];      // Bookmark preset colours

    // ── UI colours (not forwarded to HexView) ─────────────────────────────
    // Add fields here as new PE_* enum values are defined above.
    QColor  window;
    QColor  windowText;
    QColor  toolbar;
    QColor  highlight;
    QColor  panelBorders;

    // ── Per-mode overrides ────────────────────────────────────────────────
    // Key = PaletteElem cast to int.  Only elements that differ between modes
    // appear here.  Apply logic: use dark (or light) override when active,
    // fall back to the named field above.
    QHash<int, QColor> lightOverrides;
    QHash<int, QColor> darkOverrides;
};

// Apply a PaletteInfo to a HexView by calling setHexColour for every slot.
void applyPalette(HexView *hv, const PaletteInfo &info);

// Fold mode-specific overrides into a flat PaletteInfo for the requested mode.
PaletteInfo resolvedPaletteForMode(const PaletteInfo &base, bool dark);

// Resolve a HexView slot using HexEdit palette policy: explicit PaletteInfo entry
// first, then HexEdit fallbacks, then HexView's plain QPalette defaults.
QColor effectiveHexColour(const PaletteInfo &info, HvColorSlot slot, const QPalette &pal);

// Apply the UI-level colour overrides (PE_WINDOW / PE_WINDOWTEXT / PE_TOOLBAR)
// from a PaletteInfo to the application palette and stylesheet.
// Invalid colours in info are treated as "use theme default" (no override).
// The caller is responsible for calling TitleBar::refreshStylesheet() afterward.
void applyUiPalette(const PaletteInfo &info);

// Load the built-in embedded palettes (from Qt resources).
QList<PaletteInfo> loadEmbeddedPalettes();


// Reload all palettes, using custom palettes from the supplied directory.
QList<PaletteInfo> reloadPalettes(const QDir &customDir);

// Reload palettes from the supplied directory and return the named palette.
bool reloadPalette(const QDir &customDir, const QString &name, PaletteInfo *out);

// Return the directory where custom palettes are stored.
QString paletteStorageDir();

// Return the full path that savePalette would write to for the given name.
QString paletteFilePath(const QString &name);

// Write a palette to the user's palette storage directory.
// The filename is derived from info.name. Returns false on I/O failure.
bool savePalette(const PaletteInfo &info);

// ── PaletteEditorDialog ───────────────────────────────────────────────────────
// Dialog for creating or editing a custom palette.
class PaletteEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PaletteEditorDialog(const PaletteInfo &info, QWidget *parent = nullptr);

    // Returns the palette as it stands at dialog close (after any edits).
    // Use this to persist the applied-but-unsaved state for subsequent opens.
    PaletteInfo currentInfo() const { return m_info; }

signals:
    void paletteChanged(const PaletteInfo &info);
    void paletteSaved(const PaletteInfo &info);
    // Emitted when the mode toggle changes so the host can temporarily switch
    // the app's colour scheme for a live preview (0=Both→restore, 1=Light, 2=Dark).
    void previewModeRequested(int mode);

protected:
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;
    void changeEvent(QEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;

private:
    static const char *elemName(PaletteElem e);
    QColor colorAt(PaletteElem e) const;
    QColor rawColorAt(PaletteElem e) const;
    void   setColorAt(PaletteElem e, const QColor &c);
    void   updateColorUI(PaletteElem e);
    void   setScreenPickerActive(bool active);
    void   applyEditedColor(const QColor &c);
    static QPixmap makeColorSwatch(const QColor &c);

    void updateItemIndicator(int row);

    QLineEdit         *m_nameEdit   = nullptr;
    QListWidget       *m_list       = nullptr;
    PaletteColourPicker *m_picker   = nullptr;
    SettingsToggle    *m_autoToggle = nullptr;
    QAction           *m_screenPickerAction = nullptr;
    ScreenColorPicker *m_screenPicker = nullptr;
    ToggleButtonGroup   *m_modeGroup  = nullptr;
    QLabel            *m_hexLabel   = nullptr;
    QLineEdit         *m_hexEdit    = nullptr;
    QPushButton       *m_saveBtn    = nullptr;
    PaletteInfo        m_info;
    bool               m_screenPickerActive = false;
};

#endif // PALETTES_H
