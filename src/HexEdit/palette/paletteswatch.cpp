#include "paletteswatch.h"

#include "settings/settings.h"
#include "theme.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QButtonGroup>
#include <QDialog>
#include <QEnterEvent>
#include <QFocusEvent>
#include <QGridLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>

static int computeSwatchWidth(const QFont &widgetFont)
{
    QFont mono = AppSettings::hexFont();
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
    QFont mono = AppSettings::hexFont();
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
    setFocusPolicy(Qt::StrongFocus);
    setToolTip(info.name);
    setAttribute(Qt::WA_NoSystemBackground);
}

PaletteSwatch::PaletteSwatch(QWidget *parent)
    : QAbstractButton(parent), m_addMode(true)
{
    setFocusPolicy(Qt::StrongFocus);
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
void PaletteSwatch::mousePressEvent(QMouseEvent *e)
{
    const auto oldPolicy = focusPolicy();
    setFocusPolicy(Qt::NoFocus);
    QAbstractButton::mousePressEvent(e);
    setFocusPolicy(oldPolicy);
}

void PaletteSwatch::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QPalette uiPal = qApp->palette();
    const QPalette previewPal = systemPalette();
    const QRectF card = QRectF(rect()).adjusted(SW_SHADOW, SW_SHADOW,
                                                -SW_SHADOW, -SW_SHADOW);

    auto drawRings = [&]() {
        if (isChecked()) {
            p.setPen(QPen(uiPal.color(QPalette::Highlight), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(card.adjusted(1, 1, -1, -1), SW_RADIUS - 1, SW_RADIUS - 1);
        }
        if (m_keyboardCursor) {
            p.setPen(QPen(uiPal.color(QPalette::Highlight), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(card.adjusted(4, 4, -4, -4), SW_RADIUS - 4, SW_RADIUS - 4);
        }
    };

    if (m_addMode) {
        const QColor plusCol = underMouse() ? uiPal.color(QPalette::ButtonText) : uiPal.color(QPalette::Mid);
        p.setPen(QPen(plusCol, SW_BORDER));
        p.setBrush(underMouse() ? uiPal.color(QPalette::Midlight)
                                : uiPal.color(QPalette::Button));
        p.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), SW_RADIUS, SW_RADIUS);

        QFont f = font();
        f.setPixelSize(24);
        p.setFont(f);
        p.setPen(plusCol);
        p.drawText(card.toRect(), Qt::AlignCenter, "+");

        drawRings();
        return;
    }

    const bool dark = isDarkMode();
    const PaletteInfo eff = resolvedPaletteForMode(m_info, dark);

    p.setPen(Qt::NoPen);
    for (int i = SW_SHADOW; i >= 1; --i) {
        const int alpha = qRound(7.0 * qreal(SW_SHADOW - i + 1) / SW_SHADOW);
        p.setBrush(QColor(0, 0, 0, dark ? alpha / 2 : alpha));
        const qreal r = SW_RADIUS + i * 0.4;
        p.drawRoundedRect(card.adjusted(-i, -(i - 1), i, i), r, r);
    }

    const QColor effectiveBg = effectiveHexColour(eff, HVC_BACKGROUND, previewPal);
    const QColor borderCol = effectiveBg.lightness() < 128 ? QColor(255, 255, 255, 30)
                                                            : QColor(0, 0, 0, 30);
    p.setPen(QPen(borderCol, SW_BORDER));
    p.setBrush(effectiveBg);
    p.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), SW_RADIUS - 0.5, SW_RADIUS - 0.5);

    constexpr int kPadX = SW_PAD_X;
    constexpr int kPadTop = 10;
    const QColor intendedTextCol = effectiveHexColour(eff, HVC_ASCII, previewPal);
    auto lum = [](const QColor &c) -> qreal {
        auto s = [](qreal v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
        return 0.2126 * s(c.redF()) + 0.7152 * s(c.greenF()) + 0.0722 * s(c.blueF());
    };
    const qreal l1 = lum(intendedTextCol), l2 = lum(effectiveBg);
    const qreal contrast = (qMax(l1, l2) + 0.05) / (qMin(l1, l2) + 0.05);
    const QColor textCol = contrast < 3.0 ? previewPal.color(QPalette::WindowText) : intendedTextCol;
    QFont qfont = font();
    qfont.setBold(true);
    p.setFont(qfont);

    const QFontMetrics fm(qfont);
    const int nameMaxW = int(card.width()) - kPadX - 34;
    const QString elidedName = fm.elidedText(m_info.name, Qt::ElideRight, nameMaxW);
    p.setPen(textCol);
    p.drawText(QPointF(card.left() + kPadX, card.top() + kPadTop + fm.ascent()), elidedName);

    qfont = AppSettings::hexFont();
    qfont.setPointSize(font().pointSize());
    qfont.setBold(false);
    p.setFont(qfont);
    const QFontMetrics fm2(qfont);

    const QColor hexOdd = effectiveHexColour(eff, HVC_HEXODD, previewPal);
    const QColor hexEven = effectiveHexColour(eff, HVC_HEXEVEN, previewPal);
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
        effectiveHexColour(eff, HVC_HEXODD, previewPal),
        effectiveHexColour(eff, HVC_ASCII, previewPal),
        effectiveHexColour(eff, HVC_SELECTION, previewPal),
        effectiveHexColour(eff, HVC_MODIFY, previewPal),
        effectiveHexColour(eff, HVC_MATCHED, previewPal),
        effectiveHexColour(eff, HVC_BOOKMARK1, previewPal),
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
        p.setBrush(uiPal.color(QPalette::Highlight));
        p.setPen(Qt::NoPen);
        p.drawEllipse(centre, kBadgeD / 2.0, kBadgeD / 2.0);

        const QIcon tick = recoloredIcon("actions/object-select-symbolic", Qt::white, kIconSz);
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

    // The grid owns arrow-key behavior, selection, and scrolling; the swatches
    // themselves own Tab focus.  Keeping the wrapper out of the focus chain
    // avoids a hidden stop between neighboring controls and the first swatch.
    setFocusPolicy(Qt::NoFocus);
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
        swatch->installEventFilter(this);
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
    m_addBtn->installEventFilter(this);
    m_buttons.append(m_addBtn);
    connect(m_addBtn, &QAbstractButton::clicked, this, &PaletteSwatchGrid::addRequested);

    m_cursor = qBound(0, checkedIndex(), allButtons().size() - 1);
}

void PaletteSwatchGrid::setCurrentPaletteName(const QString &name)
{
    for (int i = 0; i < m_buttons.size(); ++i)
        m_buttons.at(i)->setChecked(i < m_paletteInfos.size() && m_paletteInfos.at(i).name == name);
    if (m_group->checkedButton()) {
        m_cursor = checkedIndex();
        ensureButtonVisible(m_group->checkedButton());
    }
}

void PaletteSwatchGrid::setBoundaryWidgets(QWidget *previous, QWidget *next)
{
    m_previousFocusWidget = previous;
    m_nextFocusWidget = next;
}

void PaletteSwatchGrid::focusFirst(Qt::FocusReason reason)
{
    setCursorIndex(0, true);
    if (auto *button = m_buttons.value(m_cursor))
        button->setFocus(reason);
}

void PaletteSwatchGrid::focusLast(Qt::FocusReason reason)
{
    setCursorIndex(m_buttons.size() - 1, true);
    if (auto *button = m_buttons.value(m_cursor))
        button->setFocus(reason);
}

void PaletteSwatchGrid::focusCurrent(Qt::FocusReason reason)
{
    if (auto *button = m_buttons.value(m_cursor))
        button->setFocus(reason);
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

QList<QWidget *> PaletteSwatchGrid::tabOrderWidgets() const
{
    QList<QWidget *> widgets;
    for (QAbstractButton *button : m_buttons) {
        if (button && button->focusPolicy() != Qt::NoFocus && button->isVisibleTo(window()))
            widgets.append(button);
    }
    return widgets;
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
    ensureButtonVisible(buttons.value(m_cursor));
}

void PaletteSwatchGrid::clearCursorRing()
{
    const auto buttons = allButtons();
    if (auto *swatch = qobject_cast<PaletteSwatch *>(buttons.value(m_cursor)))
        swatch->setKeyboardCursor(false);
}

bool PaletteSwatchGrid::focusBoundaryWidget(bool forward)
{
    QWidget *target = forward ? m_nextFocusWidget : m_previousFocusWidget;
    if (!target || target->focusPolicy() == Qt::NoFocus
            || target->isHidden() || !target->isVisibleTo(window()))
        return false;

    clearCursorRing();
    target->setFocus(forward ? Qt::TabFocusReason : Qt::BacktabFocusReason);
    for (QWidget *w = parentWidget(); w; w = w->parentWidget()) {
        if (auto *scroll = qobject_cast<QScrollArea *>(w)) {
            scroll->ensureWidgetVisible(target);
            break;
        }
    }
    return true;
}

bool PaletteSwatchGrid::focusAdjacentControl(bool forward)
{
    if (m_allowFocusEscape && focusBoundaryWidget(forward))
        return true;

    QWidget *candidate = nullptr;
    const int gridTop = mapToGlobal(QPoint(0, 0)).y();
    const int gridBottom = mapToGlobal(QPoint(0, height())).y();

    QWidget *scope = this;
    for (QWidget *w = parentWidget(); w; w = w->parentWidget()) {
        if (qobject_cast<QDialog *>(w)) {
            scope = w;
            break;
        }
    }

    const auto widgets = scope->findChildren<QWidget *>();
    for (QWidget *w : widgets) {
        if (w->focusPolicy() == Qt::NoFocus
                || w == this
                || w->isHidden()
                || !w->isVisibleTo(scope)
                || isAncestorOf(w))
            continue;

        if (forward) {
            const int top = w->mapToGlobal(QPoint(0, 0)).y();
            if (top < gridBottom)
                continue;
            if (!candidate || top < candidate->mapToGlobal(QPoint(0, 0)).y())
                candidate = w;
        } else {
            const int bottom = w->mapToGlobal(QPoint(0, w->height())).y();
            if (bottom > gridTop)
                continue;
            if (!candidate || bottom > candidate->mapToGlobal(QPoint(0, candidate->height())).y())
                candidate = w;
        }
    }

    if (!candidate)
        return false;

    clearCursorRing();
    candidate->setFocus(forward ? Qt::TabFocusReason : Qt::BacktabFocusReason);
    for (QWidget *w = parentWidget(); w; w = w->parentWidget()) {
        if (auto *scroll = qobject_cast<QScrollArea *>(w)) {
            scroll->ensureWidgetVisible(candidate);
            break;
        }
    }
    return true;
}

void PaletteSwatchGrid::ensureButtonVisible(QAbstractButton *button)
{
    if (!button)
        return;

    const int idx = m_buttons.indexOf(button);
    const bool firstRow = idx >= 0 && idx < m_columns;
    const bool lastRow = idx >= 0 && idx / m_columns == (m_buttons.size() - 1) / m_columns;

    for (QWidget *w = parentWidget(); w; w = w->parentWidget()) {
        if (auto *scroll = qobject_cast<QScrollArea *>(w)) {
            const QRect visibleRect = scroll->viewport()->rect();
            const QRect buttonRect(button->mapTo(scroll->viewport(), QPoint(0, 0)), button->size());
            if (visibleRect.contains(buttonRect) && m_allowFocusEscape)
                return;

            scroll->ensureWidgetVisible(button, 0, 0);
            if (auto *bar = scroll->verticalScrollBar()) {
                if (!m_allowFocusEscape && firstRow)
                    bar->setValue(bar->minimum());
                else if (!m_allowFocusEscape && lastRow)
                    bar->setValue(bar->maximum());
            }
            return;
        }
    }
}

bool PaletteSwatchGrid::eventFilter(QObject *obj, QEvent *event)
{
    auto *button = qobject_cast<QAbstractButton *>(obj);
    if (!button || !m_buttons.contains(button))
        return QWidget::eventFilter(obj, event);

    if (event->type() == QEvent::FocusIn) {
        const auto *focus = static_cast<QFocusEvent *>(event);
        const bool keyboard = focus->reason() == Qt::TabFocusReason
                           || focus->reason() == Qt::BacktabFocusReason;
        setCursorIndex(m_buttons.indexOf(button), keyboard);
        return false;
    }

    if (event->type() == QEvent::FocusOut) {
        QTimer::singleShot(0, this, [this]() {
            auto *focused = qobject_cast<QAbstractButton *>(QApplication::focusWidget());
            if (!focused || !m_buttons.contains(focused))
                clearCursorRing();
        });
        return false;
    }

    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab)
            return handleTabKey(key);
        return handleButtonKey(key);
    }

    return QWidget::eventFilter(obj, event);
}

bool PaletteSwatchGrid::handleTabKey(QKeyEvent *e)
{
    const auto buttons = allButtons();
    if (buttons.isEmpty())
        return false;

    const bool backward = e->key() == Qt::Key_Backtab
                       || (e->key() == Qt::Key_Tab && e->modifiers().testFlag(Qt::ShiftModifier));
    const int next = m_cursor + (backward ? -1 : 1);

    // Interior swatch-to-swatch tabbing is handled here because Qt's global
    // tab chain is rebuilt around dynamically inserted overlay content.  The
    // grid still gives Qt the boundary cases, so leaving the first/last swatch
    // follows the normal dialog tab order and wrap behavior.
    if (next < 0 || next >= buttons.size()) {
        if (focusBoundaryWidget(!backward)) {
            e->accept();
            return true;
        }
        return false;
    }

    setCursorIndex(next, true);
    if (auto *button = buttons.value(m_cursor))
        button->setFocus(backward ? Qt::BacktabFocusReason : Qt::TabFocusReason);
    e->accept();
    return true;
}

bool PaletteSwatchGrid::handleButtonKey(QKeyEvent *e)
{
    const auto buttons = allButtons();
    if (buttons.isEmpty())
        return false;

    if (e->key() == Qt::Key_Space || e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        if (m_cursor < m_paletteInfos.size() && buttons.at(m_cursor)->isChecked())
            emit paletteEditRequested(m_paletteInfos.at(m_cursor));
        else
            buttons.at(m_cursor)->click();
        e->accept();
        return true;
    }

    int next = m_cursor;
    switch (e->key()) {
        case Qt::Key_Right: next = qMin(m_cursor + 1, buttons.size() - 1); break;
        case Qt::Key_Left:  next = qMax(m_cursor - 1, 0); break;
        case Qt::Key_Up:
            if (m_cursor - m_columns >= 0) {
                next = m_cursor - m_columns;
            } else {
                focusAdjacentControl(false);
                e->accept();
                return true;
            }
            break;
        case Qt::Key_Down:
            next = m_cursor + m_columns;
            if (next >= buttons.size()) {
                const bool hasRowBelow = (m_cursor / m_columns) < ((buttons.size() - 1) / m_columns);
                if (hasRowBelow) {
                    next = buttons.size() - 1;
                    break;
                }
                focusAdjacentControl(true);
                e->accept();
                return true;
            }
            break;
        default:
            return false;
    }

    setCursorIndex(next, true);
    if (auto *button = buttons.value(m_cursor))
        button->setFocus(Qt::TabFocusReason);
    e->accept();
    return true;
}
