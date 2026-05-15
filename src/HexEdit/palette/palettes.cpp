#include "palettes.h"
#include "palette/screencolorpicker.h"
#include "settings/settingscard.h"
#include "settings/settings.h"

#include <algorithm>
#include <cmath>
#include "theme.h"

#include <QAction>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidgetAction>

// ─── applyPalette ─────────────────────────────────────────────────────────────

static QColor blend(const QColor &a, const QColor &b, double r = 0.5)
{
    return QColor(
        qRound(a.red()   + r * (b.red()   - a.red())),
        qRound(a.green() + r * (b.green() - a.green())),
        qRound(a.blue()  + r * (b.blue()  - a.blue())),
        qRound(a.alpha() + r * (b.alpha() - a.alpha()))
    );
}

static QColor brighten(const QColor &color, int f=130)
{
    return !isDarkMode() ? color.lighter(f) : color.darker(f);
}

static QColor darken(const QColor &color, int f=130)
{
    return isDarkMode() ? color.lighter(f) : color.darker(f);
}

static QColor readableTextColourFor(const QColor &background)
{
    return background.lightness() >= 128 ? Qt::black : Qt::white;
}

static double srgbChannelToLinear(int channel)
{
    const double c = channel / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

static double relativeLuminance(const QColor &colour)
{
    return 0.2126 * srgbChannelToLinear(colour.red())
         + 0.7152 * srgbChannelToLinear(colour.green())
         + 0.0722 * srgbChannelToLinear(colour.blue());
}

static double contrastRatio(const QColor &a, const QColor &b)
{
    const double l1 = relativeLuminance(a);
    const double l2 = relativeLuminance(b);
    const double lighter = std::max(l1, l2);
    const double darker  = std::min(l1, l2);
    return (lighter + 0.05) / (darker + 0.05);
}

static QColor contrastColourFor(const QColor &background,
                                const QColor &candidateA,
                                const QColor &candidateB)
{
    return contrastRatio(background, candidateA) >= contrastRatio(background, candidateB)
        ? candidateA
        : candidateB;
}

QColor effectiveHexColour(const PaletteInfo &info, HvColorSlot slot, const QPalette &pal)
{
    switch (slot) {
        case HVC_BACKGROUND:         return info.bg.isValid()                ? info.bg                : HexView::defaultColourForSlot(slot, pal);
        case HVC_SELECTION:          {
            QColor c = HexView::defaultColourForSlot(slot, pal);
            c = matchLuminance(platformAccentColour(), isDarkMode() ? QColor("#234B69") : QColor("#A2D7FF"));
            return info.selection.isValid()         ? info.selection         : c;//QColor("red");//brighten(c, 200);
        }
        case HVC_SELECTION_INACTIVE:
            if (info.selectionInactive.isValid())
                return info.selectionInactive;
            if (info.name == QStringLiteral("Default"))
                return QColor("#cacaca");
            return HexView::defaultColourForSlot(slot, pal);
        case HVC_SELTEXT:
            return info.selectionText.isValid()
                       ? info.selectionText
                       : readableTextColourFor(effectiveHexColour(info, HVC_SELECTION, pal));
        case HVC_SELTEXT_INACTIVE:
            return info.selectionTextInactive.isValid()
                ? info.selectionTextInactive
                : readableTextColourFor(effectiveHexColour(info, HVC_SELECTION_INACTIVE, pal));
        case HVC_HEXODDSEL:
        case HVC_HEXEVENSEL:
        case HVC_ASCIISEL:           return effectiveHexColour(info, HVC_SELTEXT, pal);
        case HVC_ADDRESS:            return info.address.isValid()           ? info.address           : HexView::defaultColourForSlot(slot, pal);
        case HVC_HEXODD:             return info.hexOdd.isValid()            ? info.hexOdd            : HexView::defaultColourForSlot(slot, pal);
        case HVC_HEXEVEN:            return info.hexEven.isValid()           ? info.hexEven           : HexView::defaultColourForSlot(slot, pal);
        case HVC_ASCII:              return info.ascii.isValid()             ? info.ascii             : HexView::defaultColourForSlot(slot, pal);
        case HVC_MODIFY:             return info.modified.isValid()          ? info.modified          : HexView::defaultColourForSlot(slot, pal);
        case HVC_MODIFYSEL:          return info.modified.isValid()          ? info.modified          : HexView::defaultColourForSlot(slot, pal);
        case HVC_RESIZEBAR:          return info.resizeBar.isValid()         ? info.resizeBar         : HexView::defaultColourForSlot(slot, pal);
        case HVC_MATCHED:            return info.matched.isValid()           ? info.matched           : HexView::defaultColourForSlot(slot, pal);
        case HVC_MATCHEDSEL:
            return info.matchSelected.isValid()
                ? info.matchSelected
                : //darken(effectiveHexColour(info, HVC_SELECTION, pal), 130);
                       blend(effectiveHexColour(info, HVC_SELECTION, pal), effectiveHexColour(info, HVC_MATCHED, pal));
        case HVC_BOOKMARK1:
        case HVC_BOOKMARK2:
        case HVC_BOOKMARK3:
        case HVC_BOOKMARK4:
        case HVC_BOOKMARK5:
        case HVC_BOOKMARK6:
        case HVC_BOOKMARK7: {
            const int i = slot - HVC_BOOKMARK1;
            return info.bookmarks[i].isValid() ? info.bookmarks[i] : HexView::defaultColourForSlot(slot, pal);
        }
        case HVC_BOOKMARK1_FG:
        case HVC_BOOKMARK2_FG:
        case HVC_BOOKMARK3_FG:
        case HVC_BOOKMARK4_FG:
        case HVC_BOOKMARK5_FG:
        case HVC_BOOKMARK6_FG:
        case HVC_BOOKMARK7_FG: {
            const auto bgSlot = HvColorSlot(HVC_BOOKMARK1 + (slot - HVC_BOOKMARK1_FG));
            return contrastColourFor(effectiveHexColour(info, bgSlot, pal),
                                     effectiveHexColour(info, HVC_BACKGROUND, pal),
                                     effectiveHexColour(info, HVC_ASCII, pal));
        }
        default:
            return HexView::defaultColourForSlot(slot, pal);
    }
}

// Maps a PaletteElem to its INI key name (shared between [Palette],
// [Palette-Light], and [Palette-Dark] sections).
static const char *elemIniKey(PaletteElem e)
{
    switch (e) {
        case PE_BG:                      return "Background";
        case PE_HEX_ODD:                 return "HexOdd";
        case PE_HEX_EVEN:                return "HexEven";
        case PE_ADDRESS:                 return "Address";
        case PE_ASCII:                   return "Ascii";
        case PE_SELECTION:               return "Selection";
        case PE_SELECTION_TEXT:          return "SelectionText";
        case PE_SELECTION_INACTIVE:      return "SelectionInactive";
        case PE_SELECTION_TEXT_INACTIVE: return "SelectionTextInactive";
        case PE_MODIFIED:                return "Modified";
        case PE_MATCHED:                 return "Matched";
        case PE_MATCH_SELECTED:          return "MatchSelected";
        case PE_RESIZE_BAR:              return "ResizeBar";
        case PE_BOOKMARK_1:              return "Bookmark1";
        case PE_BOOKMARK_2:              return "Bookmark2";
        case PE_BOOKMARK_3:              return "Bookmark3";
        case PE_BOOKMARK_4:              return "Bookmark4";
        case PE_BOOKMARK_5:              return "Bookmark5";
        case PE_BOOKMARK_6:              return "Bookmark6";
        case PE_BOOKMARK_7:              return "Bookmark7";
        case PE_WINDOW:                  return "Window";
        case PE_WINDOWTEXT:              return "WindowText";
        case PE_HIGHLIGHT:               return "Highlight";
        case PE_TOOLBAR:                 return "Toolbar";
        case PE_PANELBORDERS:            return "PanelBorders";
        case PE_COUNT:                   return nullptr;
    }
    return nullptr;
}

// Fold mode-specific overrides into a flat PaletteInfo for the given mode.
// The returned value is a plain PaletteInfo ready for applyPalette() — no
// override maps, no mode awareness needed downstream.
PaletteInfo resolvedPaletteForMode(const PaletteInfo &base, bool dark)
{
    const QHash<int, QColor> &ov = dark ? base.darkOverrides : base.lightOverrides;
    if (ov.isEmpty()) return base;
    PaletteInfo r = base;
    for (auto it = ov.cbegin(); it != ov.cend(); ++it) {
        switch (PaletteElem(it.key())) {
            case PE_BG:                      r.bg                    = it.value(); break;
            case PE_HEX_ODD:                 r.hexOdd                = it.value(); break;
            case PE_HEX_EVEN:                r.hexEven               = it.value(); break;
            case PE_ADDRESS:                 r.address               = it.value(); break;
            case PE_ASCII:                   r.ascii                 = it.value(); break;
            case PE_SELECTION:               r.selection             = it.value(); break;
            case PE_SELECTION_TEXT:          r.selectionText         = it.value(); break;
            case PE_SELECTION_INACTIVE:      r.selectionInactive     = it.value(); break;
            case PE_SELECTION_TEXT_INACTIVE: r.selectionTextInactive = it.value(); break;
            case PE_MODIFIED:                r.modified              = it.value(); break;
            case PE_MATCHED:                 r.matched               = it.value(); break;
            case PE_MATCH_SELECTED:          r.matchSelected         = it.value(); break;
            case PE_RESIZE_BAR:              r.resizeBar             = it.value(); break;
            case PE_BOOKMARK_1:              r.bookmarks[0]          = it.value(); break;
            case PE_BOOKMARK_2:              r.bookmarks[1]          = it.value(); break;
            case PE_BOOKMARK_3:              r.bookmarks[2]          = it.value(); break;
            case PE_BOOKMARK_4:              r.bookmarks[3]          = it.value(); break;
            case PE_BOOKMARK_5:              r.bookmarks[4]          = it.value(); break;
            case PE_BOOKMARK_6:              r.bookmarks[5]          = it.value(); break;
            case PE_BOOKMARK_7:              r.bookmarks[6]          = it.value(); break;
            case PE_WINDOW:                  r.window                = it.value(); break;
            case PE_WINDOWTEXT:              r.windowText            = it.value(); break;
            case PE_TOOLBAR:                 r.toolbar               = it.value(); break;
            case PE_HIGHLIGHT:               r.highlight             = it.value(); break;
            case PE_PANELBORDERS:            r.panelBorders          = it.value(); break;
            case PE_COUNT:                   break;
        }
    }
    return r;
}

void applyPalette(HexView *hv, const PaletteInfo &info)
{
    const PaletteInfo eff = resolvedPaletteForMode(info, isDarkMode());
    const QPalette pal = hv->palette();

    hv->setHexColour(HVC_BACKGROUND,         effectiveHexColour(eff, HVC_BACKGROUND, pal));
    hv->setHexColour(HVC_ADDRESS,            effectiveHexColour(eff, HVC_ADDRESS, pal));
    hv->setHexColour(HVC_RESIZEBAR,          effectiveHexColour(eff, HVC_RESIZEBAR, pal));

    hv->setHexColour(HVC_HEXODD,             effectiveHexColour(eff, HVC_HEXODD, pal));
    hv->setHexColour(HVC_HEXEVEN,            effectiveHexColour(eff, HVC_HEXEVEN, pal));
    hv->setHexColour(HVC_ASCII,              effectiveHexColour(eff, HVC_ASCII, pal));

    // Selected text — HEXODDSEL/HEXEVENSEL/ASCIISEL left invalid to chain here
    hv->setHexColour(HVC_HEXODDSEL,          QColor());
    hv->setHexColour(HVC_HEXEVENSEL,         QColor());
    hv->setHexColour(HVC_ASCIISEL,           QColor());

    hv->setHexColour(HVC_MODIFY,             effectiveHexColour(eff, HVC_MODIFY, pal));
    hv->setHexColour(HVC_MODIFYSEL,          effectiveHexColour(eff, HVC_MODIFYSEL, pal));

    hv->setHexColour(HVC_SELECTION,          effectiveHexColour(eff, HVC_SELECTION, pal));
    hv->setHexColour(HVC_SELTEXT,            effectiveHexColour(eff, HVC_SELTEXT, pal));
    hv->setHexColour(HVC_SELECTION_INACTIVE, effectiveHexColour(eff, HVC_SELECTION_INACTIVE, pal));
    hv->setHexColour(HVC_SELTEXT_INACTIVE,   effectiveHexColour(eff, HVC_SELTEXT_INACTIVE, pal));

    hv->setHexColour(HVC_MATCHED,            effectiveHexColour(eff, HVC_MATCHED, pal));
    hv->setHexColour(HVC_MATCHEDSEL,         effectiveHexColour(eff, HVC_MATCHEDSEL, pal));

    for (int i = 0; i < 7; ++i) {
        hv->setHexColour(HvColorSlot(HVC_BOOKMARK1 + i),
                         effectiveHexColour(eff, HvColorSlot(HVC_BOOKMARK1 + i), pal));
        hv->setHexColour(HvColorSlot(HVC_BOOKMARK1_FG + i),
                         effectiveHexColour(eff, HvColorSlot(HVC_BOOKMARK1_FG + i), pal));
    }

    hv->viewport()->update();
}

void applyUiPalette(const PaletteInfo &info)
{
    const PaletteInfo eff = resolvedPaletteForMode(info, isDarkMode());
    setUiColourOverrides({
        eff.window,
        eff.windowText,
        eff.toolbar,
        eff.highlight,
        eff.panelBorders,
    });
}

// ─── Palette loading ─────────────────────────────────────────────────────────

static PaletteInfo parsePaletteFile(const QString &path)
{
    PaletteInfo info;
    QSettings s(path, QSettings::IniFormat);
    s.beginGroup("Palette");
    info.name              = s.value("Name").toString();
    info.bg                = QColor(s.value("Background").toString());
    info.address           = QColor(s.value("Address").toString());
    info.hexOdd            = QColor(s.value("HexOdd").toString());
    info.hexEven           = QColor(s.value("HexEven").toString());
    info.ascii             = QColor(s.value("Ascii").toString());
    info.selection         = QColor(s.value("Selection").toString());
    info.selectionText     = QColor(s.value("SelectionText").toString());
    info.selectionInactive = QColor(s.value("SelectionInactive").toString());
    info.selectionTextInactive = QColor(s.value("SelectionTextInactive").toString());
    info.modified          = QColor(s.value("Modified").toString());
    info.matched           = QColor(s.value("Matched").toString());
    info.matchSelected     = QColor(s.value("MatchSelected").toString());
    info.resizeBar         = QColor(s.value("ResizeBar").toString());
    // Backward compat: 'Foreground' was a catch-all for text colours in older files.
    // Propagate it only to the two slots that used it as an orElse fallback.
    // hexOdd/hexEven/ascii never used it, and each now falls back to palette(WindowText).
    const QColor fg = QColor(s.value("Foreground").toString());
    if (fg.isValid()) {
        if (!info.address.isValid())       info.address       = fg;
        if (!info.selectionText.isValid()) info.selectionText = fg;
    }
    for (int i = 0; i < 7; ++i)
        info.bookmarks[i]  = QColor(s.value(QStringLiteral("Bookmark%1").arg(i + 1)).toString());
    info.window     = QColor(s.value("Window").toString());
    info.windowText = QColor(s.value("WindowText").toString());
    info.toolbar    = QColor(s.value("Toolbar").toString());
    info.highlight  = QColor(s.value("Highlight").toString());
    info.panelBorders = QColor(s.value("PanelBorders").toString());
    s.endGroup();

    // Per-mode override sections [Palette-Light] and [Palette-Dark].
    auto readOverrides = [&](const QString &group, QHash<int, QColor> &map) {
        s.beginGroup(group);
        for (int i = 0; i < PE_COUNT; ++i) {
            const char *key = elemIniKey(PaletteElem(i));
            if (!key) continue;
            const QColor c(s.value(QLatin1String(key)).toString());
            if (c.isValid()) map[i] = c;
        }
        s.endGroup();
    };
    readOverrides(QStringLiteral("Palette-Light"), info.lightOverrides);
    readOverrides(QStringLiteral("Palette-Dark"),  info.darkOverrides);

    return info;
}

QList<PaletteInfo> loadEmbeddedPalettes()
{
    QList<PaletteInfo> palettes;
    for (const QString &name : QDir(":/palettes").entryList({"*.palette"}, QDir::Files)) {
        const PaletteInfo p = parsePaletteFile(":/palettes/" + name);
        if (!p.name.isEmpty())
            palettes.append(p);
    }
    return palettes;
}

static QList<PaletteInfo> loadUserPalettes(const QDir &dir)
{
    QList<PaletteInfo> result;
    if (!dir.exists()) return result;
    for (const QString &name : dir.entryList({"*.palette"}, QDir::Files)) {
        const PaletteInfo p = parsePaletteFile(dir.filePath(name));
        if (!p.name.isEmpty())
            result.append(p);
    }
    return result;
}

QList<PaletteInfo> reloadPalettes(const QDir &customDir)
{
    // load built-in palettes
    QList<PaletteInfo> palettes = loadEmbeddedPalettes();

    // load custom user-defined palettes
    for (const PaletteInfo &p : loadUserPalettes(customDir)) {
        const auto it = std::find_if(palettes.begin(), palettes.end(),
                                     [&](const PaletteInfo &e){ return e.name == p.name; });
        if (it != palettes.end())
            *it = p;
        else
            palettes.append(p);
    }
    // sort the palettes alphabetically, but ensure the default palette is always first
    std::stable_sort(palettes.begin(), palettes.end(),
                     [](const PaletteInfo &a, const PaletteInfo &b) {
        const bool aDefault = a.name.compare(QStringLiteral("Default"), Qt::CaseInsensitive) == 0;
        const bool bDefault = b.name.compare(QStringLiteral("Default"), Qt::CaseInsensitive) == 0;
        if (aDefault != bDefault)
            return aDefault;

        const int ci = QString::compare(a.name, b.name, Qt::CaseInsensitive);
        if (ci != 0)
            return ci < 0;
        return a.name < b.name;
    });
    return palettes;
}

// ─── Custom palette I/O ──────────────────────────────────────────────────────

bool reloadPalette(const QDir &customDir, const QString &name, PaletteInfo *out)
{
    if (name.isEmpty() || !out)
        return false;

    const QList<PaletteInfo> palettes = reloadPalettes(customDir);
    const auto it = std::find_if(palettes.cbegin(), palettes.cend(),
                                 [&](const PaletteInfo &p) { return p.name == name; });
    if (it == palettes.cend())
        return false;

    *out = *it;
    return true;
}


// Returns the user-writable directory where custom palettes are stored.
// Linux:   ~/.config/catch22/palettes/
// Windows: %APPDATA%/catch22/palettes/
QString paletteStorageDir()
{
    OPEN_SETTINGS;
    return QFileInfo(s.fileName()).dir().filePath("palettes");
}

QString paletteFilePath(const QString &name)
{
    QString filename;
    for (QChar c : name)
        filename += (c.isLetterOrNumber() || c == '-') ? c : QLatin1Char('_');
    return QDir(paletteStorageDir()).filePath(filename + ".palette");
}

// Write a PaletteInfo to the custom palettes directory.
// Returns false on failure (e.g. permission error).
bool savePalette(const PaletteInfo &info)
{
    if (!QDir().mkpath(paletteStorageDir())) return false;

    auto cs = [](const QColor &c) { return c.isValid() ? c.name().toUpper() : QString(); };

    QSettings s(paletteFilePath(info.name), QSettings::IniFormat);
    s.beginGroup("Palette");
    s.setValue("Name",              info.name);
    s.setValue("Background",        cs(info.bg));
    s.setValue("Address",           cs(info.address));
    s.setValue("HexOdd",            cs(info.hexOdd));
    s.setValue("HexEven",           cs(info.hexEven));
    s.setValue("Ascii",             cs(info.ascii));
    s.setValue("Modified",          cs(info.modified));
    s.setValue("Matched",           cs(info.matched));
    s.setValue("MatchSelected",     cs(info.matchSelected));
    s.setValue("Selection",         cs(info.selection));
    s.setValue("SelectionText",     cs(info.selectionText));
    s.setValue("SelectionInactive", cs(info.selectionInactive));
    s.setValue("SelectionTextInactive", cs(info.selectionTextInactive));
    s.setValue("ResizeBar",         cs(info.resizeBar));
    for (int i = 0; i < 7; ++i)
        s.setValue(QStringLiteral("Bookmark%1").arg(i + 1), cs(info.bookmarks[i]));
    s.setValue("Window",     cs(info.window));
    s.setValue("WindowText", cs(info.windowText));
    s.setValue("Toolbar",    cs(info.toolbar));
    s.setValue("Highlight",  cs(info.highlight));
    s.setValue("PanelBorders", cs(info.panelBorders));
    s.endGroup();

    // Clear old override sections before rewriting (handles removed overrides).
    s.beginGroup(QStringLiteral("Palette-Light")); s.remove(QString()); s.endGroup();
    s.beginGroup(QStringLiteral("Palette-Dark"));  s.remove(QString()); s.endGroup();

    auto writeOverrides = [&](const QString &group, const QHash<int, QColor> &map) {
        if (map.isEmpty()) return;
        s.beginGroup(group);
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            const char *key = elemIniKey(PaletteElem(it.key()));
            if (key && it.value().isValid())
                s.setValue(QLatin1String(key), it.value().name().toUpper());
        }
        s.endGroup();
    };
    writeOverrides(QStringLiteral("Palette-Light"), info.lightOverrides);
    writeOverrides(QStringLiteral("Palette-Dark"),  info.darkOverrides);

    s.sync();
    return s.status() == QSettings::NoError;
}



// ─── PaletteEditorDialog ─────────────────────────────────────────────────────

static bool isValidPaletteName(const QString &raw, QString *reason = nullptr)
{
    const QString name = raw.trimmed();
    if (name.isEmpty()) {
        if (reason) *reason = QObject::tr("Enter a palette name");
        return false;
    }
    if (name.size() > 64) {
        if (reason) *reason = QObject::tr("Palette names must be 64 characters or fewer");
        return false;
    }
    if (name != raw) {
        if (reason) *reason = QObject::tr("Remove leading or trailing spaces");
        return false;
    }

    const QString blocked = QStringLiteral(",/\\:*?\"<>|");
    for (const QChar c : name) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7f) {
            if (reason) *reason = QObject::tr("Palette names cannot contain control characters");
            return false;
        }
        if (blocked.contains(c)) {
            if (reason)
                *reason = QObject::tr("Palette names cannot contain: %1")
                              .arg(QStringLiteral(", / \\ : * ? \" < > |"));
            return false;
        }
    }
    return true;
}

// Delegate that appends a right-aligned "L", "D", or "L,D" indicator in mid
// colour for list items that have mode-specific colour overrides.
class PaletteItemDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &opt,
               const QModelIndex &idx) const override
    {
        QStyledItemDelegate::paint(p, opt, idx);
        const QString indicator = idx.data(Qt::UserRole).toString();

        constexpr int kSz  = 14;   // icon render size
        constexpr int kPad =  6;   // right margin from item edge

        // All slots are always reserved so icons do not shift when another
        // appears or disappears. Base is left, Light middle, Dark right.
        // The gap between them is one full icon width so they read as distinct.
        const int darkX  = opt.rect.right() - kPad - kSz;
        const int lightX = darkX - kSz - kSz;   // one icon-width gap between slots
        const int baseX  = lightX - kSz - kSz;
        const int y      = opt.rect.top() + (opt.rect.height() - kSz) / 2;

        const bool hasBase  = indicator.contains(QLatin1Char('B'));
        const bool hasLight = indicator.contains(QLatin1Char('L'));
        const bool hasDark  = indicator.contains(QLatin1Char('D'));
        if (!hasBase && !hasLight && !hasDark) return;

        // text() at ~65% opacity: more contrast than mid() but softer than solid black/white.
        // Switch to full highlightedText() when the row is selected.
        QColor base = (opt.state & QStyle::State_Selected)
                    ? opt.palette.highlightedText().color()
                    : opt.palette.text().color();
        base.setAlpha(opt.state & QStyle::State_Selected ? 255 : 165);
        const QColor col = base;

        if (hasBase)
            recoloredIcon(QLatin1String("half-circle"), col, kSz)
                .paint(p, QRect(baseX, y, kSz, kSz));
        if (hasLight)
            recoloredIcon(QLatin1String("light-mode"), col, kSz)
                .paint(p, QRect(lightX, y, kSz, kSz));
        if (hasDark)
            recoloredIcon(QLatin1String("dark-mode"), col, kSz)
                .paint(p, QRect(darkX, y, kSz, kSz));
    }
};

PaletteEditorDialog::PaletteEditorDialog(const PaletteInfo &info, QWidget *parent)
    : QDialog(parent), m_info(info)
{
    removeDialogIcon(this);
    setWindowTitle(tr("Edit Palette"));
    resize(460, 580);

    // ── Name field + mode selector ────────────────────────────────────────────
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(tr("Palette name…"));
    m_nameEdit->setText(info.name);

    auto modeIcon = [](const QString &name) {
        QIcon icon(QStringLiteral(":/icons/hicolor/scalable/actions/") + name + QStringLiteral(".svg"));
        return icon.isNull() ? QIcon::fromTheme(name) : icon;
    };
    m_modeGroup = new ToggleButtonGroup({
        modeIcon(QStringLiteral("half-circle")),
        modeIcon(QStringLiteral("light-mode")),
        modeIcon(QStringLiteral("dark-mode"))
    }, this);

    // nameRow: SlideOverlay inserts a back button at pos-0 of this widget's layout.
    auto *nameRow = new QWidget(this);
    nameRow->setObjectName(QStringLiteral("overlayHeader"));
    {
        auto *lay = new QHBoxLayout(nameRow);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(8);
        // Layout after SlideOverlay inserts back button at pos-0:
        // [<back(28)>] [Palette:] [spacer(35)] [nameEdit──stretch] [12gap] [ToggleButtonGroup]
        // The 35px spacer after the label keeps nameEdit centred on the page while leaving the
        // label left-aligned next to the back button (same net offset as having it before).
        auto *palLbl = new QLabel(tr("Palette:"), nameRow);
        QFont lf = palLbl->font(); lf.setBold(true); palLbl->setFont(lf);
        lay->addWidget(palLbl);
        lay->addSpacing(35);
        lay->addWidget(m_nameEdit, 1);
        lay->addSpacing(12);
        lay->addWidget(m_modeGroup);
    }

    // ── Element list ──────────────────────────────────────────────────────────
    m_list = new QListWidget(this);
    m_list->setUniformItemSizes(true);
    m_list->setItemDelegate(new PaletteItemDelegate(m_list));
    {
        const int vPad = qMax(4, m_list->fontMetrics().height() / 2);
        const bool dark = qApp->palette().window().color().lightness() < 128;
        const QString border = dark ? QLatin1String("rgba(255,255,255,0.18)")
                                    : QLatin1String("rgba(0,0,0,0.15)");
        m_list->setStyleSheet(QString(
            "QListWidget { border: 1px solid %1; outline: 0; }"
            "QListWidget::item { padding: %2px 4px; }"
        ).arg(border).arg(vPad));
    }
    for (int i = 0; i < PE_COUNT; ++i) {
        const auto e = PaletteElem(i);
        auto *item = new QListWidgetItem(tr(elemName(e)));
        item->setData(Qt::DecorationRole, makeColorSwatch(colorAt(e)));
        m_list->addItem(item);
        updateItemIndicator(i);
    }
    m_list->setCurrentRow(0);

    // ── Color picker ──────────────────────────────────────────────────────────
    m_picker = new PaletteColourPicker(this);

    // ── Override toggle ───────────────────────────────────────────────────────
    m_autoToggle = new SettingsToggle(tr("Override"), this);
    m_autoToggle->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    // ── Hex input ─────────────────────────────────────────────────────────────
    m_hexEdit = new QLineEdit(this);
    m_hexEdit->setMaxLength(7);
    m_screenPickerAction = m_hexEdit->addAction(
        recoloredIcon(QStringLiteral("color-picker"),
                      palette().buttonText().color(), 16),
        QLineEdit::TrailingPosition);
    m_screenPickerAction->setCheckable(true);
    m_screenPickerAction->setToolTip(tr("Pick colour from screen"));
    QTimer::singleShot(0, this, [this] {
        for (QAbstractButton *btn : m_hexEdit->findChildren<QAbstractButton *>())
            btn->setCursor(Qt::PointingHandCursor);
    });
    m_screenPicker = new ScreenColorPicker(this);

    // ── Line edit styling (rounded, padded, mode-aware border) ────────────────
    {
        const bool dark = qApp->palette().window().color().lightness() < 128;
        const QString border  = dark ? QLatin1String("rgba(255,255,255,0.18)")
                                     : QLatin1String("rgba(0,0,0,0.15)");
        const QString focusColor = qApp->palette().highlight().color().name();
        const QString ss = QString(
            "QLineEdit {"
            "  border: 1px solid %1;"
            "  border-radius: 6px;"
            "  padding: 5px 8px;"
            "  background: palette(base);"
            "}"
            "QLineEdit:focus { border: 2px solid %2; }"
        ).arg(border, focusColor);
        m_nameEdit->setStyleSheet(ss +
            "QLineEdit[builtInPalette=\"true\"] { background: palette(window); }");
        m_hexEdit->setStyleSheet(ss +
            "QLineEdit:disabled { color: palette(window); background: palette(window); }");
    }

    // ── Buttons ───────────────────────────────────────────────────────────────
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    m_saveBtn = buttons->button(QDialogButtonBox::Save);
    m_saveBtn->setEnabled(false); // updated by updateNameBg() below
    buttons->button(QDialogButtonBox::Apply)->setToolTip(tr("Apply palette without saving"));
    for (QAbstractButton *btn : buttons->buttons())
        btn->setIcon(QIcon());

    buttons->layout()->setSpacing(16);

    connect(buttons->button(QDialogButtonBox::Apply), &QAbstractButton::clicked,
            this, [this]() { emit paletteChanged(m_info); accept(); });

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        m_info.name = m_nameEdit->text().trimmed();
        QString nameError;
        if (!isValidPaletteName(m_nameEdit->text(), &nameError)) {
            m_nameEdit->setToolTip(nameError);
            m_nameEdit->setFocus();
            return;
        }
        if (QFile::exists(paletteFilePath(m_info.name))) {
            QMessageBox msg(QMessageBox::Warning,
                            tr("Overwrite palette?"),
                            tr("A palette named \"%1\" already exists. Overwrite it?")
                                .arg(m_info.name),
                            QMessageBox::NoButton,
                            this);
            QPushButton *overwriteBtn = msg.addButton(tr("Overwrite"), QMessageBox::AcceptRole);
            msg.addButton(tr("Cancel"), QMessageBox::RejectRole);
            msg.setDefaultButton(overwriteBtn);
            styleMessageBox(&msg);
            msg.exec();
            if (msg.clickedButton() != overwriteBtn) return;
        }
        if (!savePalette(m_info)) return;  // stay open on write failure
        emit paletteSaved(m_info);
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // ── Layout ────────────────────────────────────────────────────────────────
    auto *hexRow = new QWidget(this);
    {
        // Left half: Override toggle
        auto *leftHalf = new QWidget(hexRow);
        {
            auto *l = new QHBoxLayout(leftHalf);
            l->setContentsMargins(0, 0, 0, 0);
            l->addWidget(m_autoToggle);
            l->addStretch();
        }

        // Right half: Hex label + stretching edit
        auto *rightHalf = new QWidget(hexRow);
        {
            auto *l = new QHBoxLayout(rightHalf);
            l->setContentsMargins(0, 0, 0, 0);
            l->setSpacing(8);
            m_hexLabel = new QLabel(tr("Hex:"), rightHalf);
            l->addWidget(m_hexLabel);
            l->addWidget(m_hexEdit, 1);
        }

        auto *lay = new QHBoxLayout(hexRow);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);
        lay->addWidget(leftHalf,  1);
        lay->addWidget(rightHalf, 1);
    }

    // ── Constrain list and picker to the same height (~5 visible rows) ───────
    // Item height mirrors the stylesheet padding: vPad top + text + vPad bottom.
    {
        const int vPad  = qMax(4, m_list->fontMetrics().height() / 2);
        const int itemH = m_list->fontMetrics().height() + 2 * vPad;
        const int panelH = 5 * itemH + 2 * m_list->frameWidth();
        m_list->setFixedHeight(panelH);
        m_picker->setFixedHeight(panelH);
    }

    auto *vlay = new QVBoxLayout(this);
    vlay->setContentsMargins(20, 20, 20, 20);
    vlay->setSpacing(12);
    vlay->addWidget(nameRow);
    vlay->addWidget(m_list);
    vlay->addWidget(hexRow);
    vlay->addWidget(m_picker);   // no stretch — height is now fixed
    vlay->addWidget(buttons);

    // ── Connections ───────────────────────────────────────────────────────────
    // Collect built-in palette names to drive the grey-background hint.
    const QStringList builtInNames = []{
        QStringList ns;
        for (const PaletteInfo &p : loadEmbeddedPalettes()) ns << p.name;
        return ns;
    }();
    auto updateNameBg = [this, builtInNames](const QString &t) {
        const QString name = t.trimmed();
        QString nameError;
        const bool validName = isValidPaletteName(t, &nameError);
        const bool isBuiltIn = builtInNames.contains(name);
        if (m_nameEdit->property("builtInPalette").toBool() != isBuiltIn) {
            m_nameEdit->setProperty("builtInPalette", isBuiltIn);
            m_nameEdit->style()->unpolish(m_nameEdit);
            m_nameEdit->style()->polish(m_nameEdit);
            m_nameEdit->update();
        }
        const bool canSave = validName && !isBuiltIn;
        m_saveBtn->setEnabled(canSave);
        if (canSave)
            m_saveBtn->setToolTip(tr("Save palette to disk"));
        else if (isBuiltIn)
            m_saveBtn->setToolTip(tr("Cannot overwrite bundled themes"));
        else
            m_saveBtn->setToolTip(nameError);
        m_nameEdit->setToolTip(validName ? QString() : nameError);
    };
    connect(m_nameEdit, &QLineEdit::textChanged, this, [updateNameBg](const QString &t) {
        updateNameBg(t);
    });
    updateNameBg(m_nameEdit->text()); // set initial background and save-button state

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || row >= PE_COUNT) return;
        updateColorUI(PaletteElem(row));
    });

    connect(m_modeGroup, &ToggleButtonGroup::modeChanged, this, [this](int mode) {
        emit previewModeRequested(mode);
        // Refresh every list item swatch to show effective colours for the new mode.
        for (int i = 0; i < PE_COUNT; ++i)
            m_list->item(i)->setData(Qt::DecorationRole, makeColorSwatch(colorAt(PaletteElem(i))));
        const int row = m_list->currentRow();
        if (row >= 0 && row < PE_COUNT)
            updateColorUI(PaletteElem(row));
        // Defer paletteChanged so it fires after previewModeRequested updates isDarkMode().
        QTimer::singleShot(0, this, [this]() { emit paletteChanged(m_info); });
    });

    connect(m_autoToggle, &SettingsToggle::toggled, this, [this](bool hasOverride) {
        const int row = m_list->currentRow();
        if (row < 0 || row >= PE_COUNT) return;
        const auto e = PaletteElem(row);
        if (hasOverride) {
            // Restore from the hex edit — updateColorUI always populates it, so
            // the previous colour survives while override is off.
            // Fall back to the computed effective colour only if the text isn't valid.
            const QColor prev = QColor(m_hexEdit->text());
            const QColor fallback = colorAt(e);
            const QColor seed = prev.isValid() ? prev : (fallback.isValid() ? fallback : QColor(128, 128, 128));
            setColorAt(e, seed);
            m_picker->blockSignals(true);
            m_picker->setColor(seed);
            m_picker->blockSignals(false);
            m_hexEdit->setText(seed.name().toUpper());
        } else {
            setScreenPickerActive(false);
            setColorAt(e, QColor());
            // Leave hex edit and picker as-is — they just become disabled.
        }
        m_hexLabel->setEnabled(hasOverride);
        m_hexEdit->setEnabled(hasOverride);
        m_screenPickerAction->setEnabled(hasOverride);
        m_picker->setEnabled(hasOverride);
        m_list->item(row)->setData(Qt::DecorationRole, makeColorSwatch(colorAt(e)));
        emit paletteChanged(m_info);
    });

    connect(m_picker, &PaletteColourPicker::colourChanged, this, [this](const QColor &c) {
        applyEditedColor(c);
    });

    connect(m_hexEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        if (text.length() != 7) return;
        const QColor c(text);
        if (!c.isValid()) return;
        m_picker->setColor(c);
        applyEditedColor(c);
    });

    connect(m_screenPickerAction, &QAction::toggled,
            this, &PaletteEditorDialog::setScreenPickerActive);
    connect(m_screenPicker, &ScreenColorPicker::colorPicked,
            this, &PaletteEditorDialog::applyEditedColor);
    connect(m_screenPicker, &ScreenColorPicker::activeChanged, this, [this](bool active) {
        m_screenPickerActive = active;
        if (m_screenPickerAction->isChecked() != active) {
            m_screenPickerAction->blockSignals(true);
            m_screenPickerAction->setChecked(active);
            m_screenPickerAction->blockSignals(false);
        }
        if (!active && m_hiddenForPicker) {
            m_hiddenForPicker = false;
            window()->setWindowOpacity(1.0);
            window()->raise();
            window()->activateWindow();
        }
    });

    // Initialise UI state for the first list item.
    updateColorUI(PE_BG);

    // Tab order: name → list → rest follows natural widget order.
    setTabOrder(m_nameEdit, m_list);
    // Focus is set in showEvent() rather than here: calling setFocus() in the
    // constructor has no effect because the widget is not yet visible, and
    // SlideOverlay's slideIn() calls m_content->setFocus() after show() which
    // would override any focus we set during the show event itself.
}

void PaletteEditorDialog::showEvent(QShowEvent *e)
{
    QDialog::showEvent(e);
    // Post the focus change so it lands after SlideOverlay's setFocus() call.
    QTimer::singleShot(0, this, [this] {
        m_nameEdit->selectAll();
        m_nameEdit->setFocus();
    });
}

void PaletteEditorDialog::hideEvent(QHideEvent *e)
{
    setScreenPickerActive(false);
    QDialog::hideEvent(e);
}

void PaletteEditorDialog::changeEvent(QEvent *e)
{
    QDialog::changeEvent(e);
    if (e->type() == QEvent::PaletteChange && m_screenPickerAction) {
        m_screenPickerAction->setIcon(
            recoloredIcon(QStringLiteral("color-picker"),
                          palette().buttonText().color(), 16));
    }
}

void PaletteEditorDialog::keyPressEvent(QKeyEvent *e)
{
    if (m_screenPickerActive && e->key() == Qt::Key_Escape) {
        setScreenPickerActive(false);
        e->accept();
        return;
    }
    QDialog::keyPressEvent(e);
}

void PaletteEditorDialog::mousePressEvent(QMouseEvent *e)
{
    if (m_screenPickerActive) {
        setScreenPickerActive(false);
        e->accept();
        return;
    }
    QDialog::mousePressEvent(e);
}

const char *PaletteEditorDialog::elemName(PaletteElem e)
{
    switch (e) {
        case PE_BG:                 return "Background";
        //case PE_FG:                 return "Foreground";
        case PE_HEX_ODD:            return "Hex Odd";
        case PE_HEX_EVEN:           return "Hex Even";
        case PE_ASCII:              return "ASCII";
        case PE_MODIFIED:           return "Modified";
        case PE_SELECTION:          return "Selection";
        case PE_MATCHED:            return "Search Match";
        case PE_MATCH_SELECTED:     return "Match (Selected)";
        case PE_SELECTION_TEXT:     return "Selection Text";
        case PE_SELECTION_INACTIVE: return "Selection (Inactive)";
        case PE_SELECTION_TEXT_INACTIVE: return "Selection Text (Inactive)";
        case PE_ADDRESS:            return "Address";
        case PE_RESIZE_BAR:         return "Resize Bar";
        case PE_BOOKMARK_1:         return "Bookmark 1";
        case PE_BOOKMARK_2:         return "Bookmark 2";
        case PE_BOOKMARK_3:         return "Bookmark 3";
        case PE_BOOKMARK_4:         return "Bookmark 4";
        case PE_BOOKMARK_5:         return "Bookmark 5";
        case PE_BOOKMARK_6:         return "Bookmark 6";
        case PE_BOOKMARK_7:         return "Bookmark 7";

        // main window elements
        case PE_WINDOW:             return "Window";
        case PE_WINDOWTEXT:         return "Window Text";
        case PE_TOOLBAR:            return "Toolbar";
        case PE_PANELBORDERS:       return "Panel Borders";
        case PE_HIGHLIGHT:          return "Window Highlight";
        case PE_COUNT:              return "";
    }
    return "";
}

QColor PaletteEditorDialog::colorAt(PaletteElem e) const
{
    const int editMode = m_modeGroup ? m_modeGroup->mode() : 0;
    if (editMode != 0) {
        const auto &ov = editMode == 2 ? m_info.darkOverrides : m_info.lightOverrides;
        auto it = ov.constFind(int(e));
        if (it != ov.constEnd()) return *it;
        // No mode-specific override — fall through to base with system defaults.
    }
    const QPalette &pal = qApp->palette();
    switch (e) {
        case PE_BG:                 return effectiveHexColour(m_info, HVC_BACKGROUND, pal);
        //case PE_FG:
        case PE_HEX_ODD:            return effectiveHexColour(m_info, HVC_HEXODD, pal);
        case PE_HEX_EVEN:           return effectiveHexColour(m_info, HVC_HEXEVEN, pal);
        case PE_ASCII:              return effectiveHexColour(m_info, HVC_ASCII, pal);
        case PE_MODIFIED:           return effectiveHexColour(m_info, HVC_MODIFY, pal);
        case PE_SELECTION:          return effectiveHexColour(m_info, HVC_SELECTION, pal);
        case PE_MATCHED:            return effectiveHexColour(m_info, HVC_MATCHED, pal);
        case PE_MATCH_SELECTED:     return effectiveHexColour(m_info, HVC_MATCHEDSEL, pal);
        case PE_SELECTION_TEXT:     return effectiveHexColour(m_info, HVC_SELTEXT, pal);
        case PE_SELECTION_INACTIVE: return effectiveHexColour(m_info, HVC_SELECTION_INACTIVE, pal);
        case PE_SELECTION_TEXT_INACTIVE: return effectiveHexColour(m_info, HVC_SELTEXT_INACTIVE, pal);
        case PE_ADDRESS:            return effectiveHexColour(m_info, HVC_ADDRESS, pal);
        case PE_RESIZE_BAR:         return effectiveHexColour(m_info, HVC_RESIZEBAR, pal);
        case PE_BOOKMARK_1:         return effectiveHexColour(m_info, HVC_BOOKMARK1, pal);
        case PE_BOOKMARK_2:         return effectiveHexColour(m_info, HVC_BOOKMARK2, pal);
        case PE_BOOKMARK_3:         return effectiveHexColour(m_info, HVC_BOOKMARK3, pal);
        case PE_BOOKMARK_4:         return effectiveHexColour(m_info, HVC_BOOKMARK4, pal);
        case PE_BOOKMARK_5:         return effectiveHexColour(m_info, HVC_BOOKMARK5, pal);
        case PE_BOOKMARK_6:         return effectiveHexColour(m_info, HVC_BOOKMARK6, pal);
        case PE_BOOKMARK_7:         return effectiveHexColour(m_info, HVC_BOOKMARK7, pal);

        case PE_WINDOW:             return m_info.window.isValid()     ? m_info.window     : pal.color(QPalette::Window);
        case PE_WINDOWTEXT:         return m_info.windowText.isValid() ? m_info.windowText : pal.color(QPalette::WindowText);
        case PE_TOOLBAR:            return m_info.toolbar.isValid()    ? m_info.toolbar    : pal.color(QPalette::AlternateBase);
        case PE_HIGHLIGHT:          return m_info.highlight.isValid()  ? m_info.highlight  : pal.color(QPalette::Highlight);
        case PE_PANELBORDERS:       return m_info.panelBorders.isValid() ? m_info.panelBorders : themeBorderColor();
        case PE_COUNT:              return {};
    }
    return {};
}

QColor PaletteEditorDialog::rawColorAt(PaletteElem e) const
{
    const int editMode = m_modeGroup ? m_modeGroup->mode() : 0;
    if (editMode != 0) {
        const auto &ov = editMode == 2 ? m_info.darkOverrides : m_info.lightOverrides;
        auto it = ov.constFind(int(e));
        // Return override if set; QColor() (invalid) means "no override = use base".
        return it != ov.constEnd() ? *it : QColor();
    }
    switch (e) {
        case PE_BG:                 return m_info.bg;
        case PE_HEX_ODD:            return m_info.hexOdd;
        case PE_HEX_EVEN:           return m_info.hexEven;
        case PE_ASCII:              return m_info.ascii;
        case PE_MODIFIED:           return m_info.modified;
        case PE_SELECTION:          return m_info.selection;
        case PE_MATCHED:            return m_info.matched;
        case PE_MATCH_SELECTED:     return m_info.matchSelected;
        case PE_SELECTION_TEXT:     return m_info.selectionText;
        case PE_SELECTION_INACTIVE: return m_info.selectionInactive;
        case PE_SELECTION_TEXT_INACTIVE: return m_info.selectionTextInactive;
        case PE_ADDRESS:            return m_info.address;
        case PE_RESIZE_BAR:         return m_info.resizeBar;
        case PE_BOOKMARK_1:         return m_info.bookmarks[0];
        case PE_BOOKMARK_2:         return m_info.bookmarks[1];
        case PE_BOOKMARK_3:         return m_info.bookmarks[2];
        case PE_BOOKMARK_4:         return m_info.bookmarks[3];
        case PE_BOOKMARK_5:         return m_info.bookmarks[4];
        case PE_BOOKMARK_6:         return m_info.bookmarks[5];
        case PE_BOOKMARK_7:         return m_info.bookmarks[6];
        case PE_WINDOW:             return m_info.window;
        case PE_WINDOWTEXT:         return m_info.windowText;
        case PE_TOOLBAR:            return m_info.toolbar;
        case PE_HIGHLIGHT:          return m_info.highlight;
        case PE_PANELBORDERS:       return m_info.panelBorders;
        case PE_COUNT:              return {};
    }
    return {};
}

void PaletteEditorDialog::updateItemIndicator(int row)
{
    if (row < 0 || row >= PE_COUNT) return;
    const bool hasBase  = rawColorAt(PaletteElem(row)).isValid();
    const bool hasLight = m_info.lightOverrides.contains(row);
    const bool hasDark  = m_info.darkOverrides.contains(row);
    QString indicator;
    if (hasBase)  indicator += QLatin1Char('B');
    if (hasLight) indicator += QLatin1Char('L');
    if (hasDark)  indicator += QLatin1Char('D');
    m_list->item(row)->setData(Qt::UserRole, indicator);
}

void PaletteEditorDialog::updateColorUI(PaletteElem e)
{
    setScreenPickerActive(false);
    const bool hasOverride = rawColorAt(e).isValid();

    m_autoToggle->blockSignals(true);
    m_autoToggle->setChecked(hasOverride);
    m_autoToggle->blockSignals(false);

    m_hexLabel->setEnabled(hasOverride);
    m_hexEdit->setEnabled(hasOverride);
    m_screenPickerAction->setEnabled(hasOverride);
    m_picker->setEnabled(hasOverride);

    const QColor display    = colorAt(e);
    const QColor pickerSeed = display.isValid() ? display : QColor(128, 128, 128);
    m_picker->blockSignals(true);
    m_picker->setColor(pickerSeed);
    m_picker->blockSignals(false);
    // Always populate the hex edit, even when disabled — the text survives
    // while override is off so toggling on always has a value to restore.
    m_hexEdit->setText(pickerSeed.name().toUpper());
}

void PaletteEditorDialog::setScreenPickerActive(bool active)
{
    active = active && m_hexEdit && m_hexEdit->isEnabled();
    if (active == m_screenPickerActive)
        return;

    m_screenPickerActive = active;
    if (active) {
        QTimer::singleShot(0, this, [this] {
            if (!m_screenPickerActive) return;
            m_screenPicker->start(this, palette().buttonText().color());
            // Fade the window to invisible rather than hiding it — the window
            // must stay in the Windows process model so the app remains active,
            // SetCapture holds, and the Qt timer keeps firing.
            m_hiddenForPicker = true;
            window()->setWindowOpacity(0.0);
        });
    } else {
        m_screenPicker->cancel();
    }
}

void PaletteEditorDialog::applyEditedColor(const QColor &c)
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= PE_COUNT || !c.isValid()) return;
    setColorAt(PaletteElem(row), c);
    m_picker->blockSignals(true);
    m_picker->setColor(c);
    m_picker->blockSignals(false);
    m_hexEdit->setText(c.name().toUpper());
    m_list->item(row)->setData(Qt::DecorationRole, makeColorSwatch(c));
    emit paletteChanged(m_info);
}

void PaletteEditorDialog::setColorAt(PaletteElem e, const QColor &c)
{
    const int editMode = m_modeGroup ? m_modeGroup->mode() : 0;
    if (editMode != 0) {
        auto &ov = editMode == 2 ? m_info.darkOverrides : m_info.lightOverrides;
        if (c.isValid()) ov[int(e)] = c;
        else             ov.remove(int(e));
        updateItemIndicator(int(e));
        return;
    }
    switch (e) {
        case PE_BG:                 m_info.bg                = c; break;
        case PE_HEX_ODD:            m_info.hexOdd            = c; break;
        case PE_HEX_EVEN:           m_info.hexEven           = c; break;
        case PE_ASCII:              m_info.ascii             = c; break;
        case PE_MODIFIED:           m_info.modified          = c; break;
        case PE_SELECTION:          m_info.selection         = c; break;
        case PE_MATCHED:            m_info.matched           = c; break;
        case PE_MATCH_SELECTED:     m_info.matchSelected     = c; break;
        case PE_SELECTION_TEXT:     m_info.selectionText     = c; break;
        case PE_SELECTION_INACTIVE: m_info.selectionInactive = c; break;
        case PE_SELECTION_TEXT_INACTIVE: m_info.selectionTextInactive = c; break;
        case PE_ADDRESS:            m_info.address           = c; break;
        case PE_RESIZE_BAR:         m_info.resizeBar         = c; break;
        case PE_BOOKMARK_1:         m_info.bookmarks[0]      = c; break;
        case PE_BOOKMARK_2:         m_info.bookmarks[1]      = c; break;
        case PE_BOOKMARK_3:         m_info.bookmarks[2]      = c; break;
        case PE_BOOKMARK_4:         m_info.bookmarks[3]      = c; break;
        case PE_BOOKMARK_5:         m_info.bookmarks[4]      = c; break;
        case PE_BOOKMARK_6:         m_info.bookmarks[5]      = c; break;
        case PE_BOOKMARK_7:         m_info.bookmarks[6]      = c; break;

        case PE_WINDOW:             m_info.window            = c; break;
        case PE_WINDOWTEXT:         m_info.windowText        = c; break;
        case PE_TOOLBAR:            m_info.toolbar           = c; break;
        case PE_HIGHLIGHT:          m_info.highlight         = c; break;
        case PE_PANELBORDERS:       m_info.panelBorders      = c; break;
        case PE_COUNT:              break;
    }
}

QPixmap PaletteEditorDialog::makeColorSwatch(const QColor &c)
{
    QPixmap px(16, 16);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    if (c.isValid()) {
        p.setBrush(c);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(1, 1, 14, 14), 3, 3);
    } else {
        // No override — neutral placeholder
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(128, 128, 128, 160), 1));
        p.drawRoundedRect(QRectF(1.5, 1.5, 13, 13), 3, 3);
    }
    return px;
}
