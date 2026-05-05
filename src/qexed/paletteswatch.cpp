#include "paletteswatch.h"

#include "settings.h"
#include "theme.h"

#include <algorithm>
#include <cmath>

#include <QButtonGroup>
#include <QEnterEvent>
#include <QFocusEvent>
#include <QGridLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QTimer>

static int computeSwatchWidth(const QFont &widgetFont)
{
    const QString hexFamily = AppSettings::prefFontFamily();
    QFont mono = QFont(hexFamily.isEmpty() ? QStringLiteral("monospace") : hexFamily);
    mono.setPointSize(widgetFont.pointSize());
    mono.setBold(false);

    QFont bold = widgetFont;
    bold.setBold(true);

    const QFontMetrics fmMono(mono);
    const QFontMetrics fmBold(bold);
    const int textW = 8 * fmMono.horizontalAdvance(QStringLiteral("AA"))
                    + 7 * fmBold.horizontalAdvance(QStringLiteral(" "));

    return 2 * SW_SHADOW + 2 * SW_PAD_X + textW;
}

static int computeSwatchHeight(const QFont &widgetFont)
{
    const QString hexFamily = AppSettings::prefFontFamily();
    QFont mono = QFont(hexFamily.isEmpty() ? QStringLiteral("monospace") : hexFamily);
    mono.setPointSize(widgetFont.pointSize());
    mono.setBold(false);

    const QFontMetrics fmMono(mono);
    const int cardH = 10 + 2 * fmMono.height() + fmMono.ascent() + 60;
    return 2 * SW_SHADOW + cardH;
}

PaletteSwatch::PaletteSwatch(const PaletteInfo &info, QWidget *parent)
    : QAbstractButton(parent), m_info(info)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(computeSwatchHeight(font()));
    setFixedWidth(computeSwatchWidth(font()));
    setFocusPolicy(Qt::NoFocus);
    setToolTip(info.name);
    setAttribute(Qt::WA_NoSystemBackground);
}

PaletteSwatch::PaletteSwatch(QWidget *parent)
    : QAbstractButton(parent), m_addMode(true)
{
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(computeSwatchHeight(font()));
    setFixedWidth(computeSwatchWidth(font()));
    setToolTip(tr("Add palette..."));
    setAttribute(Qt::WA_NoSystemBackground);
}

void PaletteSwatch::mouseDoubleClickEvent(QMouseEvent *)
{
    if (!m_addMode)
        emit doubleClicked();
}

void PaletteSwatch::setKeyboardCursor(bool on)
{
    if (m_keyboardCursor == on) return;
    m_keyboardCursor = on;
    update();
}

void PaletteSwatch::enterEvent(QEnterEvent *e) { update(); QAbstractButton::enterEvent(e); }
void PaletteSwatch::leaveEvent(QEvent *e) { update(); QAbstractButton::leaveEvent(e); }
void PaletteSwatch::focusInEvent(QFocusEvent *e) { update(); QAbstractButton::focusInEvent(e); }
void PaletteSwatch::focusOutEvent(QFocusEvent *e) { update(); QAbstractButton::focusOutEvent(e); }

void PaletteSwatch::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QPalette pal = systemPalette();
    const QRectF card = QRectF(rect()).adjusted(SW_SHADOW, SW_SHADOW,
                                                -SW_SHADOW, -SW_SHADOW);

    auto drawRings = [&]() {
        if (isChecked()) {
            p.setPen(QPen(pal.color(QPalette::Highlight), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(card.adjusted(1, 1, -1, -1), SW_RADIUS - 1, SW_RADIUS - 1);
        }
        if (m_keyboardCursor) {
            p.setPen(QPen(pal.color(QPalette::Highlight), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(card.adjusted(4, 4, -4, -4), SW_RADIUS - 4, SW_RADIUS - 4);
        }
    };

    if (m_addMode) {
        const QColor plusCol = underMouse() ? pal.color(QPalette::ButtonText) : pal.color(QPalette::Mid);
        p.setPen(QPen(plusCol, SW_BORDER));
        p.setBrush(underMouse() ? pal.color(QPalette::Midlight)
                                : pal.color(QPalette::Button));
        p.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), SW_RADIUS, SW_RADIUS);

        QFont f = font();
        f.setPixelSize(24);
        p.setFont(f);
        p.setPen(plusCol);
        p.drawText(card.toRect(), Qt::AlignCenter, "+");

        drawRings();
        return;
    }

    const bool dark = pal.color(QPalette::Window).lightness() < 128;
    const PaletteInfo eff = resolvedPaletteForMode(m_info, dark);

    p.setPen(Qt::NoPen);
    for (int i = SW_SHADOW; i >= 1; --i) {
        const int alpha = qRound(7.0 * qreal(SW_SHADOW - i + 1) / SW_SHADOW);
        p.setBrush(QColor(0, 0, 0, dark ? alpha / 2 : alpha));
        const qreal r = SW_RADIUS + i * 0.4;
        p.drawRoundedRect(card.adjusted(-i, -(i - 1), i, i), r, r);
    }

    const QColor effectiveBg = eff.bg.isValid() ? eff.bg
                                                 : pal.color(QPalette::Base);
    const QColor borderCol = effectiveBg.lightness() < 128 ? QColor(255, 255, 255, 30)
                                                            : QColor(0, 0, 0, 30);
    p.setPen(QPen(borderCol, SW_BORDER));
    p.setBrush(effectiveBg);
    p.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), SW_RADIUS - 0.5, SW_RADIUS - 0.5);

    constexpr int kPadX = SW_PAD_X;
    constexpr int kPadTop = 10;
    const QColor intendedTextCol = eff.ascii.isValid()
        ? eff.ascii
        : (effectiveBg.lightness() < 128 ? QColor(255, 255, 255, 200)
                                         : QColor(0, 0, 0, 180));
    auto lum = [](const QColor &c) -> qreal {
        auto s = [](qreal v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
        return 0.2126 * s(c.redF()) + 0.7152 * s(c.greenF()) + 0.0722 * s(c.blueF());
    };
    const qreal l1 = lum(intendedTextCol), l2 = lum(effectiveBg);
    const qreal contrast = (qMax(l1, l2) + 0.05) / (qMin(l1, l2) + 0.05);
    const QColor textCol = contrast < 3.0 ? pal.color(QPalette::WindowText) : intendedTextCol;
    QFont qfont = font();
    qfont.setBold(true);
    p.setFont(qfont);

    const QFontMetrics fm(qfont);
    const int nameMaxW = int(card.width()) - kPadX - 34;
    const QString elidedName = fm.elidedText(m_info.name, Qt::ElideRight, nameMaxW);
    p.setPen(textCol);
    p.drawText(QPointF(card.left() + kPadX, card.top() + kPadTop + fm.ascent()), elidedName);

    const QString hexFamily = AppSettings::prefFontFamily();
    qfont = QFont(hexFamily.isEmpty() ? QStringLiteral("monospace") : hexFamily);
    qfont.setPointSize(font().pointSize());
    qfont.setBold(false);
    p.setFont(qfont);
    const QFontMetrics fm2(qfont);

    const QColor hexOdd = eff.hexOdd.isValid() ? eff.hexOdd : pal.color(QPalette::Text);
    const QColor hexEven = eff.hexEven.isValid() ? eff.hexEven : pal.color(QPalette::Text);
    const qreal hexBaseY = card.top() + kPadTop + fm2.height() + 16;
    static const quint8 kSample[2][8] = {
        { 0xA4, 0x3F, 0xB2, 0x91, 0xE7, 0x60, 0xC3, 0x2A },
        { 0x5E, 0xC8, 0x07, 0x6D, 0x1F, 0x94, 0x38, 0xBB },
    };
    const qreal lineStep = fm2.ascent() + 6;
    for (int row = 0; row < 2; ++row) {
        const qreal rowY = hexBaseY + fm2.ascent() + row * lineStep;
        qreal xPos = card.left() + kPadX;
        for (int i = 0; i < 8 && xPos < card.right() - 10; ++i) {
            const QString tok = QStringLiteral("%1").arg(kSample[row][i], 2, 16, QLatin1Char('0')).toUpper();
            p.setPen(i % 2 == 0 ? hexOdd : hexEven);
            p.drawText(QPointF(xPos, rowY), tok);
            xPos += fm2.horizontalAdvance(tok);
            if (i < 7) xPos += fm.horizontalAdvance(QStringLiteral(" "));
        }
    }

    constexpr int kSwatchH = 12;
    constexpr int kSwatchRadius = 3;
    constexpr int kSwatchBotPad = 10;
    constexpr qreal kSwatchGap = 5;
    constexpr int kNSwatches = 6;
    const QColor swatchCols[kNSwatches] = {
        eff.hexOdd.isValid()       ? eff.hexOdd       : pal.color(QPalette::Text),
        eff.ascii.isValid()        ? eff.ascii        : pal.color(QPalette::Text),
        eff.selection.isValid()    ? eff.selection    : pal.color(QPalette::Highlight),
        eff.modified.isValid()     ? eff.modified     : QColor(200, 50, 50),
        eff.matched.isValid()      ? eff.matched      : QColor(255, 165, 0),
        eff.bookmarks[0].isValid() ? eff.bookmarks[0] : pal.color(QPalette::Base),
    };
    const qreal swatchY = card.bottom() - kSwatchBotPad - kSwatchH;
    const qreal swatchAreaW = card.width() - 2 * kPadX;
    const qreal swatchW = (swatchAreaW - (kNSwatches - 1) * kSwatchGap) / kNSwatches;
    p.setPen(Qt::NoPen);
    for (int i = 0; i < kNSwatches; ++i) {
        const qreal sx = card.left() + kPadX + i * (swatchW + kSwatchGap);
        p.setBrush(swatchCols[i]);
        p.drawRoundedRect(QRectF(sx, swatchY, swatchW, kSwatchH), kSwatchRadius, kSwatchRadius);
    }

    drawRings();

    if (isChecked()) {
        constexpr int kBadgeD = 20;
        constexpr int kIconSz = 16;
        constexpr int kInset = 8;
        const QPointF centre(card.right() - kBadgeD / 2.0 - kInset,
                             card.top() + kBadgeD / 2.0 + kInset);
        p.setBrush(pal.color(QPalette::Highlight));
        p.setPen(Qt::NoPen);
        p.drawEllipse(centre, kBadgeD / 2.0, kBadgeD / 2.0);

        const QIcon tick = recoloredIcon("object-select-symbolic", Qt::white, kIconSz);
        const QRectF iconRect(centre.x() - kIconSz / 2.0,
                              centre.y() - kIconSz / 2.0,
                              kIconSz, kIconSz);
        tick.paint(&p, iconRect.toRect());
    }
}

PaletteSwatchGrid::PaletteSwatchGrid(QWidget *parent)
    : QWidget(parent)
{
    m_layout = new QGridLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(5 + 2 * SW_SHADOW);
    m_layout->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);

    setFocusPolicy(Qt::StrongFocus);
}

void PaletteSwatchGrid::clear()
{
    const auto old = m_group->buttons();
    for (auto *button : old)
        delete button;
    m_addBtn = nullptr;
    while (auto *item = m_layout->takeAt(0))
        delete item;
    m_buttons.clear();
    m_paletteInfos.clear();
    m_cursor = 0;
}

void PaletteSwatchGrid::setPalettes(const QList<PaletteInfo> &palettes,
                                    const QString &currentName,
                                    int maxPaletteCards)
{
    clear();

    const int visibleCount = maxPaletteCards < 0 ? palettes.size()
                                                 : qMin(maxPaletteCards, palettes.size());
    for (int i = 0; i < visibleCount; ++i) {
        const PaletteInfo info = palettes.at(i);
        auto *swatch = new PaletteSwatch(info, this);
        if (info.name == currentName)
            swatch->setChecked(true);
        m_group->addButton(swatch);
        m_layout->addWidget(swatch, i / m_columns, i % m_columns);
        m_buttons.append(swatch);
        m_paletteInfos.append(info);

        connect(swatch, &QAbstractButton::clicked, this, [this, swatch, info]() {
            setCursorIndex(allButtons().indexOf(swatch), false);
            setCurrentPaletteName(info.name);
            emit paletteSelected(info);
        });
        connect(swatch, &PaletteSwatch::doubleClicked, this, [this, info]() {
            emit paletteEditRequested(info);
        });
    }

    m_addBtn = new PaletteSwatch(this);
    m_layout->addWidget(m_addBtn, visibleCount / m_columns, visibleCount % m_columns);
    m_buttons.append(m_addBtn);
    connect(m_addBtn, &QAbstractButton::clicked, this, &PaletteSwatchGrid::addRequested);

    m_cursor = qBound(0, checkedIndex(), allButtons().size() - 1);
}

void PaletteSwatchGrid::setCurrentPaletteName(const QString &name)
{
    for (int i = 0; i < m_buttons.size(); ++i)
        m_buttons.at(i)->setChecked(i < m_paletteInfos.size() && m_paletteInfos.at(i).name == name);
    if (m_group->checkedButton())
        m_cursor = checkedIndex();
}

void PaletteSwatchGrid::setGridContentsMargins(int left, int top, int right, int bottom)
{
    m_layout->setContentsMargins(left, top, right, bottom);
}

int PaletteSwatchGrid::gridWidthForColumns(int columns) const
{
    const PaletteSwatch probe;
    const int gap = m_layout ? m_layout->spacing() : (5 + 2 * SW_SHADOW);
    return columns * probe.width() + (columns - 1) * gap;
}

QList<QAbstractButton *> PaletteSwatchGrid::allButtons() const
{
    return m_buttons;
}

int PaletteSwatchGrid::checkedIndex() const
{
    const QAbstractButton *checked = m_group->checkedButton();
    if (!checked)
        return 0;
    return m_buttons.indexOf(const_cast<QAbstractButton *>(checked));
}

void PaletteSwatchGrid::setCursorIndex(int idx, bool showRing)
{
    const auto buttons = allButtons();
    if (buttons.isEmpty())
        return;
    idx = qBound(0, idx, buttons.size() - 1);
    if (auto *old = qobject_cast<PaletteSwatch *>(buttons.value(m_cursor)))
        old->setKeyboardCursor(false);
    m_cursor = idx;
    if (showRing) {
        if (auto *swatch = qobject_cast<PaletteSwatch *>(buttons.value(m_cursor)))
            swatch->setKeyboardCursor(true);
    }
}

void PaletteSwatchGrid::focusInEvent(QFocusEvent *e)
{
    QWidget::focusInEvent(e);
    const int checked = checkedIndex();
    setCursorIndex(checked < 0 ? 0 : checked, e->reason() == Qt::TabFocusReason
                                      || e->reason() == Qt::BacktabFocusReason
                                      || e->reason() == Qt::OtherFocusReason);
}

void PaletteSwatchGrid::focusOutEvent(QFocusEvent *e)
{
    setCursorIndex(m_cursor, false);
    QWidget::focusOutEvent(e);
}

void PaletteSwatchGrid::keyPressEvent(QKeyEvent *e)
{
    const auto buttons = allButtons();
    if (buttons.isEmpty()) {
        QWidget::keyPressEvent(e);
        return;
    }

    if (e->key() == Qt::Key_Space || e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        buttons.at(m_cursor)->click();
        e->accept();
        return;
    }

    int next = m_cursor;
    switch (e->key()) {
        case Qt::Key_Right: next = qMin(m_cursor + 1, buttons.size() - 1); break;
        case Qt::Key_Left:  next = qMax(m_cursor - 1, 0); break;
        case Qt::Key_Up:    next = qMax(m_cursor - m_columns, 0); break;
        case Qt::Key_Down:
            next = m_cursor + m_columns;
            if (next >= buttons.size()) {
                if (m_allowFocusEscape) {
                    setCursorIndex(m_cursor, false);
                    focusNextPrevChild(true);
                }
                e->accept();
                return;
            }
            break;
        default:
            QWidget::keyPressEvent(e);
            return;
    }

    setCursorIndex(next, true);
    e->accept();
}
