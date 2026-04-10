#ifndef PALETTES_H
#define PALETTES_H

#include <QAbstractButton>
#include <QColor>
#include <QDialog>
#include <QList>
#include <QString>

class HexView;
class QLineEdit;
class QListWidget;
class QPushButton;
class ColorPickerWidget;

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
    PE_MODIFIED,
    PE_MATCHED,
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
    PE_COUNT
};

// ── PaletteInfo ───────────────────────────────────────────────────────────────
// Parsed representation of a .palette resource file.
struct PaletteInfo {
    QString name;
    QColor  bg;                // Background
    QColor  fg;                // Foreground (default text)
    QColor  address;           // Address column text
    QColor  hexOdd;            // Odd-column hex digits
    QColor  hexEven;           // Even-column hex digits
    QColor  ascii;             // ASCII panel text
    QColor  selection;         // Selection background
    QColor  selectionText;     // Selection text foreground
    QColor  selectionInactive; // Selection background when unfocused
    QColor  modified;          // Modified-byte indicator
    QColor  matched;           // Search-match highlight (background)
    QColor  resizeBar  ;         // Resize bar
    QColor  bookmarks[7];      // Bookmark preset colours

    // ── UI colours (not forwarded to HexView) ─────────────────────────────
    // Add fields here as new PE_* enum values are defined above.
    QColor  window;
    QColor  windowText;
    QColor  toolbar;
    QColor  highlight;
};

// Apply a PaletteInfo to a HexView by calling setHexColour for every slot.
void applyPalette(HexView *hv, const PaletteInfo &info);

// Apply the UI-level colour overrides (PE_WINDOW / PE_WINDOWTEXT / PE_TOOLBAR)
// from a PaletteInfo to the application palette and stylesheet.
// Invalid colours in info are treated as "use theme default" (no override).
// The caller is responsible for calling TitleBar::refreshStylesheet() afterward.
void applyUiPalette(const PaletteInfo &info);

// Load the built-in embedded palettes (from Qt resources).
QList<PaletteInfo> loadEmbeddedPalettes();

// Load any user-saved palettes from the QSettings storage directory.
QList<PaletteInfo> loadCustomPalettes();

// Return the directory where custom palettes are stored.
QString paletteStorageDir();

// Return the full path that savePalette would write to for the given name.
QString paletteFilePath(const QString &name);

// Write a palette to the user's palette storage directory.
// The filename is derived from info.name. Returns false on I/O failure.
bool savePalette(const PaletteInfo &info);

// ── Swatch size constants (shared by PaletteSwatch and AddPaletteSwatch) ─────
inline constexpr int SW_SHADOW =  3;   // transparent margin for drop shadow
inline constexpr int SW_W      = 84 + 2 * SW_SHADOW;   // total widget width  (90)
inline constexpr int SW_H      = 66 + 2 * SW_SHADOW;   // total widget height (72)
inline constexpr int SW_RADIUS = 10;
inline constexpr int SW_BORDER =  1;

// ── PaletteSwatch ─────────────────────────────────────────────────────────────
// Checkable button that renders a colour preview for one palette.
class PaletteSwatch : public QAbstractButton
{
    Q_OBJECT
public:
    explicit PaletteSwatch(const PaletteInfo &info, QWidget *parent = nullptr);

signals:
    void doubleClicked();

protected:
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void paintEvent(QPaintEvent *) override;

private:
    PaletteInfo m_info;
};

// ── PaletteEditorDialog ───────────────────────────────────────────────────────
// Dialog for creating or editing a custom palette.
class PaletteEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PaletteEditorDialog(const PaletteInfo &info, QWidget *parent = nullptr);

signals:
    void paletteChanged(const PaletteInfo &info);
    void paletteSaved(const PaletteInfo &info);

private:
    static const char *elemName(PaletteElem e);
    QColor colorAt(PaletteElem e) const;
    void   setColorAt(PaletteElem e, const QColor &c);
    static QPixmap makeColorSwatch(const QColor &c);

    QLineEdit         *m_nameEdit = nullptr;
    QListWidget       *m_list     = nullptr;
    ColorPickerWidget *m_picker   = nullptr;
    QLineEdit         *m_hexEdit  = nullptr;
    QPushButton       *m_saveBtn  = nullptr;
    PaletteInfo        m_info;
};

#endif // PALETTES_H
