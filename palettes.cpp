#include "palettes.h"
#include "preferences.h"

#include <algorithm>
#include "HexView/hexview.h"
#include "theme.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

// ─── applyPalette ─────────────────────────────────────────────────────────────

// Helper: return c if valid, otherwise fallback.
static QColor orElse(const QColor &c, const QColor &fallback)
{
    return c.isValid() ? c : fallback;
}

void applyPalette(HexView *hv, const PaletteInfo &info)
{
    hv->setHexColour(HVC_BACKGROUND,         info.bg);
    hv->setHexColour(HVC_ADDRESS,            info.address);   // QColor() → palette(WindowText)
    hv->setHexColour(HVC_RESIZEBAR,          info.resizeBar); // QColor() → palette(Mid)

    hv->setHexColour(HVC_HEXODD,             info.hexOdd);
    hv->setHexColour(HVC_HEXEVEN,            info.hexEven);
    hv->setHexColour(HVC_ASCII,              info.ascii);

    // Selected text — HEXODDSEL/HEXEVENSEL/ASCIISEL left invalid to chain here
    hv->setHexColour(HVC_HEXODDSEL,          QColor());
    hv->setHexColour(HVC_HEXEVENSEL,         QColor());
    hv->setHexColour(HVC_ASCIISEL,           QColor());

    hv->setHexColour(HVC_MODIFY,             info.modified);
    hv->setHexColour(HVC_MODIFYSEL,          info.modified);//.lighter(130));

    hv->setHexColour(HVC_SELECTION,          info.selection);
    hv->setHexColour(HVC_SELTEXT,            info.selectionText); // QColor() → palette(HighlightedText)
    hv->setHexColour(HVC_SELECTION_INACTIVE, info.selectionInactive); // QColor() → palette default
    hv->setHexColour(HVC_SELTEXT_INACTIVE,   info.selectionTextInactive); // QColor() → contrast derived in realiseColour

    hv->setHexColour(HVC_MATCHED,            info.matched);
    hv->setHexColour(HVC_MATCHEDSEL,         info.matched.lighter(140));

    static const QColor kBookmarkDefaults[7] = {
        QColor(255, 255,   0), QColor(255, 165,   0), QColor(255,  80,  80),
        QColor(180, 100, 220), QColor( 80, 200, 120), QColor( 80, 160, 255),
        QColor(255, 150, 200),
    };
    for (int i = 0; i < 7; ++i)
        hv->setHexColour(HvColorSlot(HVC_BOOKMARK1 + i), orElse(info.bookmarks[i], kBookmarkDefaults[i]));

    hv->viewport()->update();
}

void applyUiPalette(const PaletteInfo &info)
{
    setUiColourOverrides({ info.window, info.windowText, info.toolbar, info.highlight });
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

QList<PaletteInfo> loadAllPalettes()
{
    QList<PaletteInfo> palettes = loadEmbeddedPalettes();
    for (const PaletteInfo &p : loadCustomPalettes()) {
        const auto it = std::find_if(palettes.begin(), palettes.end(),
                                     [&](const PaletteInfo &e){ return e.name == p.name; });
        if (it != palettes.end())
            *it = p;
        else
            palettes.append(p);
    }
    return palettes;
}

// ─── Custom palette I/O ──────────────────────────────────────────────────────

// Returns the user-writable directory where custom palettes are stored.
// Linux:   ~/.config/qexed/palettes/
// Windows: %APPDATA%/qexed/palettes/
QString paletteStorageDir()
{
    QSettings s(QSettings::IniFormat, QSettings::UserScope, "qexed", "qexed");
    return QFileInfo(s.fileName()).dir().filePath("palettes");
}

QList<PaletteInfo> loadCustomPalettes()
{
    QList<PaletteInfo> result;
    const QDir dir(paletteStorageDir());
    if (!dir.exists()) return result;
    for (const QString &name : dir.entryList({"*.palette"}, QDir::Files)) {
        const PaletteInfo p = parsePaletteFile(dir.filePath(name));
        if (!p.name.isEmpty())
            result.append(p);
    }
    return result;
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
    s.setValue("Highlight",    cs(info.highlight));
    s.sync();
    return s.status() == QSettings::NoError;
}

// ─── ColorPickerWidget ───────────────────────────────────────────────────────

static constexpr int CPW_HUE_H = 20;   // hue strip height
static constexpr int CPW_GAP   =  8;   // gap between SV rect and hue strip
static constexpr int CPW_RAD   =  4;   // corner radius

class ColorPickerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ColorPickerWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMouseTracking(true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    QSize sizeHint() const override
    {
        return QSize(360, 200 + CPW_GAP + CPW_HUE_H);
    }

    QColor color() const { return m_color; }

    void setColor(const QColor &c)
    {
        m_color = c.isValid() ? c : Qt::black;
        update();
    }

signals:
    void colorChanged(const QColor &c);

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!isEnabled()) {
            const qreal dpr = devicePixelRatioF();
            QImage img(size() * dpr, QImage::Format_ARGB32);
            img.setDevicePixelRatio(dpr);
            img.fill(Qt::transparent);
            { QPainter pp(&img); drawContent(pp); }
            // Convert to greyscale
            for (int y = 0; y < img.height(); ++y) {
                QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
                for (int x = 0; x < img.width(); ++x) {
                    const int g = 128 + (qGray(line[x]) >> 1);
                    line[x] = qRgba(g, g, g, qAlpha(line[x]));
                }
            }
            QPainter p(this);
            p.drawImage(0, 0, img);
            return;
        }
        QPainter p(this);
        drawContent(p);
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton) return;
        if (svRectF().contains(e->position()))        { m_zone = ZoneSV;  pickSV(e->position()); }
        else if (hueRectF().contains(e->position()))  { m_zone = ZoneHue; pickHue(e->position()); }
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (m_zone == ZoneSV)  pickSV(e->position());
        if (m_zone == ZoneHue) pickHue(e->position());
    }

    void mouseReleaseEvent(QMouseEvent *) override { m_zone = ZoneNone; }

private:
    void drawContent(QPainter &p)
    {
        const QRectF sv  = svRectF();
        const QRectF hr  = hueRectF();
        const qreal hueF = qBound(0.0, m_color.hsvHueF(), 1.0);

        p.setRenderHint(QPainter::Antialiasing);

        // ── SV square: horizontal white→hue, vertical transparent→black ──────
        QPainterPath svPath;
        svPath.addRoundedRect(sv, CPW_RAD, CPW_RAD);

        QLinearGradient gS(sv.left(), 0, sv.right(), 0);
        gS.setColorAt(0.0, Qt::white);
        gS.setColorAt(1.0, QColor::fromHsvF(hueF, 1.0, 1.0));
        p.save();
        p.setClipPath(svPath);
        p.fillRect(sv, gS);

        QLinearGradient gV(0, sv.top(), 0, sv.bottom());
        gV.setColorAt(0.0, Qt::transparent);
        gV.setColorAt(1.0, Qt::black);
        p.fillRect(sv, gV);
        p.restore();

        p.setPen(QPen(palette().color(QPalette::Mid), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(sv.adjusted(0.5, 0.5, -0.5, -0.5), CPW_RAD, CPW_RAD);

        // SV cursor
        if (isEnabled()) {
            const qreal cx = sv.left() + qBound(0.0, m_color.hsvSaturationF(), 1.0) * sv.width();
            const qreal cy = sv.top()  + (1.0 - qBound(0.0, m_color.valueF(), 1.0)) * sv.height();
            p.setPen(QPen(Qt::black, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QPointF(cx, cy), 7.0, 7.0);
            p.setPen(QPen(Qt::white, 1.5));
            p.drawEllipse(QPointF(cx, cy), 5.5, 5.5);
        }

        // ── Hue strip ─────────────────────────────────────────────────────────
        QLinearGradient gHue(hr.left(), 0, hr.right(), 0);
        for (int i = 0; i <= 12; ++i)
            gHue.setColorAt(i / 12.0, QColor::fromHsvF(i == 12 ? 0.9999 : i / 12.0, 1.0, 1.0));

        QPainterPath huePath;
        huePath.addRoundedRect(hr, CPW_RAD, CPW_RAD);
        p.fillPath(huePath, gHue);

        p.setPen(QPen(palette().color(QPalette::Mid), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(hr.adjusted(0.5, 0.5, -0.5, -0.5), CPW_RAD, CPW_RAD);

        // Hue cursor
        if (isEnabled()) {
            const qreal hx = hr.left() + hueF * hr.width();
            p.setPen(QPen(Qt::black, 2.0));
            p.drawLine(QPointF(hx, hr.top() + 2), QPointF(hx, hr.bottom() - 2));
            p.setPen(QPen(Qt::white, 1.0));
            p.drawLine(QPointF(hx, hr.top() + 3), QPointF(hx, hr.bottom() - 3));
        }
    }

    enum Zone { ZoneNone, ZoneSV, ZoneHue };

    QRectF svRectF()  const { return QRectF(0, 0, width(), height() - CPW_GAP - CPW_HUE_H); }
    QRectF hueRectF() const { return QRectF(0, height() - CPW_HUE_H, width(), CPW_HUE_H); }

    void pickSV(const QPointF &pos)
    {
        const QRectF sv = svRectF();
        const qreal  h  = qBound(0.0, m_color.hsvHueF(), 1.0);
        const qreal  s  = qBound(0.0, (pos.x() - sv.left()) / sv.width(), 1.0);
        const qreal  v  = qBound(0.0, 1.0 - (pos.y() - sv.top()) / sv.height(), 1.0);
        m_color = QColor::fromHsvF(h, s, v);
        update();
        emit colorChanged(m_color);
    }

    void pickHue(const QPointF &pos)
    {
        const QRectF hr = hueRectF();
        const qreal  h  = qBound(0.0, (pos.x() - hr.left()) / hr.width(), 1.0);
        m_color = QColor::fromHsvF(h, qBound(0.0, m_color.hsvSaturationF(), 1.0),
                                      qBound(0.0, m_color.valueF(), 1.0));
        update();
        emit colorChanged(m_color);
    }

    QColor m_color = Qt::red;
    Zone   m_zone  = ZoneNone;
};

// ─── PaletteSwatch ───────────────────────────────────────────────────────────

PaletteSwatch::PaletteSwatch(const PaletteInfo &info, QWidget *parent)
    : QAbstractButton(parent), m_info(info)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(SW_W, SW_H);
    setToolTip(info.name);
    setAttribute(Qt::WA_NoSystemBackground);
}

void PaletteSwatch::mouseDoubleClickEvent(QMouseEvent *)
{
    emit doubleClicked();
}

void PaletteSwatch::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const bool  dark = palette().color(QPalette::Window).lightness() < 128;
    const QRectF card = QRectF(rect()).adjusted(SW_SHADOW, SW_SHADOW,
                                               -SW_SHADOW, -SW_SHADOW);

    // ── Drop shadow ──────────────────────────────────────────────────────────
    p.setPen(Qt::NoPen);
    for (int i = SW_SHADOW; i >= 1; --i) {
        const int alpha = qRound(7.0 * qreal(SW_SHADOW - i + 1) / SW_SHADOW);
        p.setBrush(QColor(0, 0, 0, dark ? alpha / 2 : alpha));
        const qreal r = SW_RADIUS + i * 0.4;
        p.drawRoundedRect(card.adjusted(-i, -(i - 1), i, i), r, r);
    }

    // ── Card: background + border ────────────────────────────────────────────
    // Border adapts to the swatch colour so it reads well on any palette.
    // When selected, swap to the app highlight colour for a clear indicator.
    // Fall back to palette roles for any automatic (invalid) colours.
    const QColor effectiveBg = m_info.bg.isValid() ? m_info.bg
                                                    : palette().color(QPalette::Base);
    const bool  checked    = isChecked();
    const qreal borderW    = checked ? 2.0 : SW_BORDER;
    const QColor borderCol = checked
        ? palette().color(QPalette::Highlight)
        : (effectiveBg.lightness() < 128 ? QColor(255, 255, 255, 30)
                                         : QColor(0,   0,   0,   30));
    const qreal h = borderW * 0.5;
    p.setPen(QPen(borderCol, borderW));
    p.setBrush(effectiveBg);
    p.drawRoundedRect(card.adjusted(h, h, -h, -h), SW_RADIUS - h + 0.5, SW_RADIUS - h + 0.5);

    // ── Centered name text on a selection-colour pill ────────────────────────
    // Derive both colours from the palette's own fields only — no app palette fallback.
    const QColor selBg   = m_info.selection.isValid()
                           ? m_info.selection
                           : (effectiveBg.lightness() >= 128 ? effectiveBg.darker(150)
                                                              : effectiveBg.lighter(200));
    const QColor selText = m_info.selectionText.isValid()
                           ? m_info.selectionText
                           : (selBg.lightness() >= 128 ? Qt::black : Qt::white);
    p.setFont(font());
    const QFontMetrics fm(font());
    const int textW  = fm.horizontalAdvance(m_info.name);
    const int textH  = fm.height();
    const int padX   = 6;
    const int padY   = 3;
    const QRectF pill(card.center().x() - textW / 2.0 - padX,
                      card.center().y() - textH / 2.0 - padY,
                      textW + padX * 2,
                      textH + padY * 2);
    p.setPen(Qt::NoPen);
    p.setBrush(selBg);
    p.drawRect(pill);
    p.setPen(selText);
    p.drawText(pill.toRect(), Qt::AlignCenter, m_info.name);
}

// ─── PaletteEditorDialog ─────────────────────────────────────────────────────

PaletteEditorDialog::PaletteEditorDialog(const PaletteInfo &info, QWidget *parent)
    : QDialog(parent), m_info(info)
{
    removeDialogIcon(this);
    setWindowTitle(tr("Edit Palette"));
    resize(460, 580);

    // ── Name field ────────────────────────────────────────────────────────────
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(tr("Palette name…"));
    m_nameEdit->setText(info.name);

    auto *nameRow = new QWidget(this);
    nameRow->setObjectName(QStringLiteral("overlayHeader")); // back button inserted here by SlideOverlay
    {
        // Left half: bold "Theme" title, flush left
        auto *leftHalf = new QWidget(nameRow);
        {
            auto *l = new QHBoxLayout(leftHalf);
            l->setContentsMargins(0, 0, 0, 0);
            l->setSpacing(8);
            auto *themeLabel = new QLabel(tr("Theme"), leftHalf);
            QFont lf = themeLabel->font();
            lf.setBold(true);
            themeLabel->setFont(lf);
            l->addWidget(themeLabel);
            //l->addStretch();
        }

        // Right half: "Name:" label + stretching edit — mirrors the "Hex:" row layout
        auto *rightHalf = new QWidget(nameRow);
        {
            auto *l = new QHBoxLayout(rightHalf);
            l->setContentsMargins(0, 0, 0, 0);
            //l->setSpacing(8);
            l->addWidget(new QLabel(tr("Name:"), rightHalf));
            l->addWidget(m_nameEdit, 1);
        }

        // Stretch 3:4 compensates for the ~36 px (button + gap) that
        // SlideOverlay inserts at pos-0, keeping rightHalf ≈ same width
        // as hexRow's rightHalf so the two edit fields match in width.
        auto *lay = new QHBoxLayout(nameRow);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(8);
        lay->addWidget(leftHalf,  3);
        lay->addWidget(rightHalf, 4);
    }

    // ── Element list ──────────────────────────────────────────────────────────
    m_list = new QListWidget(this);
    m_list->setUniformItemSizes(true);
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
    }
    m_list->setCurrentRow(0);

    // ── Color picker ──────────────────────────────────────────────────────────
    m_picker = new ColorPickerWidget(this);

    // ── Automatic toggle ──────────────────────────────────────────────────────
    m_autoToggle = new SettingsToggle(tr("Automatic"), this);
    m_autoToggle->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    // ── Hex input ─────────────────────────────────────────────────────────────
    m_hexEdit = new QLineEdit(this);
    m_hexEdit->setMaxLength(7);

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
        m_nameEdit->setStyleSheet(ss);
        m_hexEdit->setStyleSheet(ss +
            "QLineEdit:disabled { color: palette(window); background: palette(window); }");
    }

    // ── Buttons ───────────────────────────────────────────────────────────────
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    m_saveBtn = buttons->button(QDialogButtonBox::Save);
    m_saveBtn->setEnabled(!m_nameEdit->text().trimmed().isEmpty());
    for (QAbstractButton *btn : buttons->buttons())
        btn->setIcon(QIcon());

    buttons->layout()->setSpacing(16);

    connect(buttons->button(QDialogButtonBox::Apply), &QAbstractButton::clicked,
            this, [this]() { emit paletteChanged(m_info); accept(); });

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        m_info.name = m_nameEdit->text().trimmed();
        if (QFile::exists(paletteFilePath(m_info.name))) {
            QMessageBox msg(this);
            msg.setWindowTitle(tr("Overwrite palette?"));
            msg.setText(tr("A palette named \"%1\" already exists. Overwrite it?")
                            .arg(m_info.name));
            msg.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            for (QAbstractButton *btn : msg.buttons())
                btn->setIcon(QIcon());
            if (msg.exec() != QMessageBox::Yes) return;
        }
        if (!savePalette(m_info)) return;  // stay open on write failure
        emit paletteSaved(m_info);
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // ── Layout ────────────────────────────────────────────────────────────────
    auto *hexRow = new QWidget(this);
    {
        // Left half: Automatic toggle
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
    connect(m_nameEdit, &QLineEdit::textChanged, this, [this](const QString &t) {
        m_saveBtn->setEnabled(!t.trimmed().isEmpty());
    });

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || row >= PE_COUNT) return;
        updateColorUI(PaletteElem(row));
    });

    connect(m_autoToggle, &SettingsToggle::toggled, this, [this](bool automatic) {
        const int row = m_list->currentRow();
        if (row < 0 || row >= PE_COUNT) return;
        const auto e = PaletteElem(row);
        if (automatic) {
            setColorAt(e, QColor());
            // Leave hex edit and picker as-is — they just become disabled.
        } else {
            // Restore from the hex edit — updateColorUI always populates it, so
            // the previous manual colour survives while automatic is on.
            // Fall back to the computed fallback only if the text isn't valid.
            const QColor prev = QColor(m_hexEdit->text());
            const QColor fallback = colorAt(e);
            const QColor seed = prev.isValid() ? prev : (fallback.isValid() ? fallback : QColor(128, 128, 128));
            setColorAt(e, seed);
            m_picker->blockSignals(true);
            m_picker->setColor(seed);
            m_picker->blockSignals(false);
            m_hexEdit->setText(seed.name().toUpper());
        }
        m_hexLabel->setEnabled(!automatic);
        m_hexEdit->setEnabled(!automatic);
        m_picker->setEnabled(!automatic);
        m_list->item(row)->setData(Qt::DecorationRole, makeColorSwatch(colorAt(e)));
        emit paletteChanged(m_info);
    });

    connect(m_picker, &ColorPickerWidget::colorChanged, this, [this](const QColor &c) {
        const int row = m_list->currentRow();
        if (row < 0 || row >= PE_COUNT) return;
        setColorAt(PaletteElem(row), c);
        m_hexEdit->setText(c.name().toUpper());
        m_list->item(row)->setData(Qt::DecorationRole, makeColorSwatch(c));
        emit paletteChanged(m_info);
    });

    connect(m_hexEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        if (text.length() != 7) return;
        const QColor c(text);
        if (!c.isValid()) return;
        const int row = m_list->currentRow();
        if (row < 0 || row >= PE_COUNT) return;
        setColorAt(PaletteElem(row), c);
        m_picker->setColor(c);
        m_list->item(row)->setData(Qt::DecorationRole, makeColorSwatch(c));
        emit paletteChanged(m_info);
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
        case PE_SELECTION_TEXT:     return "Selection Text";
        case PE_SELECTION_INACTIVE: return "Selection (Inactive)";
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
        //case PE_WINDOW:             return "Window";
        //case PE_WINDOWTEXT:         return "Window Text";
        //case PE_TOOLBAR:            return "Toolbar";
        case PE_HIGHLIGHT:          return "Window Highlight";
        case PE_COUNT:              return "";
    }
    return "";
}

QColor PaletteEditorDialog::colorAt(PaletteElem e) const
{
    const QPalette &pal = qApp->palette();
    switch (e) {
        case PE_BG:                 return m_info.bg.isValid()        ? m_info.bg        : pal.color(QPalette::Base);
        //case PE_FG:
        case PE_HEX_ODD:            return m_info.hexOdd.isValid()    ? m_info.hexOdd    : pal.color(QPalette::Text);
        case PE_HEX_EVEN:           return m_info.hexEven.isValid()   ? m_info.hexEven   : pal.color(QPalette::Text);
        case PE_ASCII:              return m_info.ascii.isValid()     ? m_info.ascii     : pal.color(QPalette::Text);
        case PE_MODIFIED:           return m_info.modified.isValid()  ? m_info.modified  : QColor(200, 50, 50);
        case PE_SELECTION:          return m_info.selection.isValid() ? m_info.selection : pal.color(QPalette::Highlight);
        case PE_MATCHED:            return m_info.matched.isValid()   ? m_info.matched   : QColor(255, 165, 0);
        case PE_SELECTION_TEXT:     return m_info.selectionText.isValid()     ? m_info.selectionText     : pal.color(QPalette::HighlightedText);
        case PE_SELECTION_INACTIVE: return m_info.selectionInactive.isValid() ? m_info.selectionInactive : (m_info.selection.isValid() ? m_info.selection.darker(130) : pal.color(QPalette::Highlight).darker(130));
        case PE_ADDRESS:            return m_info.address.isValid()   ? m_info.address   : pal.color(QPalette::Text);
        case PE_RESIZE_BAR:         return m_info.resizeBar.isValid() ? m_info.resizeBar : pal.color(QPalette::Mid);
        case PE_BOOKMARK_1:         return m_info.bookmarks[0].isValid() ? m_info.bookmarks[0] : QColor(255, 255,   0);
        case PE_BOOKMARK_2:         return m_info.bookmarks[1].isValid() ? m_info.bookmarks[1] : QColor(255, 165,   0);
        case PE_BOOKMARK_3:         return m_info.bookmarks[2].isValid() ? m_info.bookmarks[2] : QColor(255,  80,  80);
        case PE_BOOKMARK_4:         return m_info.bookmarks[3].isValid() ? m_info.bookmarks[3] : QColor(180, 100, 220);
        case PE_BOOKMARK_5:         return m_info.bookmarks[4].isValid() ? m_info.bookmarks[4] : QColor( 80, 200, 120);
        case PE_BOOKMARK_6:         return m_info.bookmarks[5].isValid() ? m_info.bookmarks[5] : QColor( 80, 160, 255);
        case PE_BOOKMARK_7:         return m_info.bookmarks[6].isValid() ? m_info.bookmarks[6] : QColor(255, 150, 200);

        case PE_WINDOW:             return m_info.window.isValid()     ? m_info.window     : pal.color(QPalette::Window);
        case PE_WINDOWTEXT:         return m_info.windowText.isValid() ? m_info.windowText : pal.color(QPalette::WindowText);
        case PE_TOOLBAR:            return m_info.toolbar.isValid()    ? m_info.toolbar    : pal.color(QPalette::AlternateBase);
        case PE_HIGHLIGHT:          return m_info.highlight.isValid()  ? m_info.highlight  : pal.color(QPalette::Highlight);
        case PE_COUNT:              return {};
    }
    return {};
}

QColor PaletteEditorDialog::rawColorAt(PaletteElem e) const
{
    switch (e) {
        case PE_BG:                 return m_info.bg;
        case PE_HEX_ODD:            return m_info.hexOdd;
        case PE_HEX_EVEN:           return m_info.hexEven;
        case PE_ASCII:              return m_info.ascii;
        case PE_MODIFIED:           return m_info.modified;
        case PE_SELECTION:          return m_info.selection;
        case PE_MATCHED:            return m_info.matched;
        case PE_SELECTION_TEXT:     return m_info.selectionText;
        case PE_SELECTION_INACTIVE: return m_info.selectionInactive;
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
        case PE_COUNT:              return {};
    }
    return {};
}

void PaletteEditorDialog::updateColorUI(PaletteElem e)
{
    const bool automatic = !rawColorAt(e).isValid();

    m_autoToggle->blockSignals(true);
    m_autoToggle->setChecked(automatic);
    m_autoToggle->blockSignals(false);

    m_hexLabel->setEnabled(!automatic);
    m_hexEdit->setEnabled(!automatic);
    m_picker->setEnabled(!automatic);

    const QColor display    = colorAt(e);
    const QColor pickerSeed = display.isValid() ? display : QColor(128, 128, 128);
    m_picker->blockSignals(true);
    m_picker->setColor(pickerSeed);
    m_picker->blockSignals(false);
    // Always populate the hex edit, even when disabled — the text is invisible
    // while automatic is on, but it means toggling off always has a value to read.
    m_hexEdit->setText(pickerSeed.name().toUpper());
}

void PaletteEditorDialog::setColorAt(PaletteElem e, const QColor &c)
{
    switch (e) {
        case PE_BG:                 m_info.bg                = c; break;
        case PE_HEX_ODD:            m_info.hexOdd            = c; break;
        case PE_HEX_EVEN:           m_info.hexEven           = c; break;
        case PE_ASCII:              m_info.ascii             = c; break;
        case PE_MODIFIED:           m_info.modified          = c; break;
        case PE_SELECTION:          m_info.selection         = c; break;
        case PE_MATCHED:            m_info.matched           = c; break;
        case PE_SELECTION_TEXT:     m_info.selectionText     = c; break;
        case PE_SELECTION_INACTIVE: m_info.selectionInactive = c; break;
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
        // Automatic — neutral placeholder
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(128, 128, 128, 160), 1));
        p.drawRoundedRect(QRectF(1.5, 1.5, 13, 13), 3, 3);
    }
    return px;
}

#include "palettes.moc"
