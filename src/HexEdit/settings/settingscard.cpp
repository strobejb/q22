#include "settingscard.h"
#include "theme.h"

#include <QApplication>
#include <QDesktopServices>
#include <QFocusEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

// ── Shared visual constants ───────────────────────────────────────────────────

static constexpr int SC_SHADOW  =  4;   // transparent shadow margin
static constexpr int SC_RADIUS  = 10;   // card corner radius

// Compact card (About / list style): rows edge-to-edge, no inter-row gap.
// Rows must carry their own vertical breathing room via NAV_ROW_VPAD.
static constexpr int SC_C_H_PAD =  16;  // horizontal inset inside card
static constexpr int SC_C_V_PAD =   0;   // rows carry their own height; no extra card padding
static constexpr int SC_C_SPACE =   0;  // no gap between rows

// Spaced card (Preferences / form style): rows have generous inter-row gaps.
static constexpr int SC_S_H_PAD =  20;
static constexpr int SC_S_V_PAD =  8;
static constexpr int SC_S_SPACE =  28;

// All row types use the same vertical padding — height = max(fm.height(), ROW_MIN_H) + 2×ROW_VPAD.
static constexpr int ROW_VPAD   =  8;   // vertical padding per side
static constexpr int ROW_MIN_H  = 22;   // minimum content height (matches pill / stepper button)

// Trailing icon (chevron / external-link) shared across NavigationRow variants.
static constexpr int ROW_ICON_SZ    = 12;  // icon pixel size
static constexpr int ROW_ICON_RIGHT =  0;   // gap from widget right edge to icon right edge

// Pill toggle
static constexpr int PILL_W        = 40;
static constexpr int PILL_H        = 22;
static constexpr int THUMB_MARGIN  =  2;

// Step spin-box
static constexpr int SSB_BTN_W   = 26;
static constexpr int SSB_BTN_H   = 22;
static constexpr int SSB_RADIUS  =  5;
static constexpr int SSB_VAL_GAP =  8;
static constexpr int SSB_VAL_W   = 24;
static constexpr int SSB_SPACING = 16;  // gap between label and value area

// ── focusRingPath ─────────────────────────────────────────────────────────────
// QPainterPath for a rounded rect where the top corners and bottom corners are
// independently rounded (or squared) — used for per-row focus rings.

static QPainterPath focusRingPath(const QRectF &r, bool roundTop, bool roundBot,
                                  qreal radius)
{
    const qreal tl = roundTop ? radius : 0, tr = roundTop ? radius : 0;
    const qreal br = roundBot ? radius : 0, bl = roundBot ? radius : 0;
    QPainterPath path;
    path.moveTo(r.left() + tl, r.top());
    path.lineTo(r.right() - tr, r.top());
    if (tr > 0) path.arcTo(r.right()-2*tr, r.top(),           2*tr, 2*tr,  90, -90);
    path.lineTo(r.right(), r.bottom() - br);
    if (br > 0) path.arcTo(r.right()-2*br, r.bottom()-2*br,   2*br, 2*br,   0, -90);
    path.lineTo(r.left() + bl, r.bottom());
    if (bl > 0) path.arcTo(r.left(),       r.bottom()-2*bl,   2*bl, 2*bl, 270, -90);
    path.lineTo(r.left(), r.top() + tl);
    if (tl > 0) path.arcTo(r.left(),       r.top(),           2*tl, 2*tl, 180, -90);
    path.closeSubpath();
    return path;
}

// ── SettingsCard ──────────────────────────────────────────────────────────────

SettingsCard::SettingsCard(QList<QWidget *> rows, Style style, QWidget *parent)
    : QWidget(parent), m_style(style)
{
    setAttribute(Qt::WA_NoSystemBackground);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setContentsMargins(SC_SHADOW, SC_SHADOW, SC_SHADOW, SC_SHADOW);

    auto *lay = new QVBoxLayout(this);
    if (style == Style::Compact) {
        lay->setContentsMargins(SC_C_H_PAD, SC_C_V_PAD + SC_SHADOW,
                                SC_C_H_PAD, SC_C_V_PAD + SC_SHADOW);
        lay->setSpacing(SC_C_SPACE);
    } else {
        lay->setContentsMargins(SC_S_H_PAD, SC_S_V_PAD + SC_SHADOW,
                                SC_S_H_PAD, SC_S_V_PAD + SC_SHADOW);
        lay->setSpacing(SC_S_SPACE);
    }

    for (int i = 0; i < rows.size(); ++i) {
        QWidget *w = rows[i];
        if (style == Style::Compact && qobject_cast<NavigationRow *>(w)) {
            const int edgeInset = ROW_VPAD / 4;
            w->setContentsMargins(0,
                                  i == rows.size() - 1 ? edgeInset : 0,
                                  0,
                                  i == 0 ? edgeInset : 0);
        }
        lay->addWidget(w);
        w->installEventFilter(this);
    }
}

bool SettingsCard::eventFilter(QObject *obj, QEvent *e)
{
    auto *lay       = static_cast<QVBoxLayout *>(layout());
    const int count = lay->count();

    // ── Hover tracking ─────────────────────────────────────────────────────────
    if (e->type() == QEvent::Enter || e->type() == QEvent::Leave) {
        int newIdx = -1;
        if (e->type() == QEvent::Enter) {
            for (int i = 0; i < count; ++i)
                if (lay->itemAt(i)->widget() == obj) { newIdx = i; break; }
        }
        if (newIdx != m_hoverIdx) { m_hoverIdx = newIdx; update(); }
    }

    // ── Keyboard focus ring — Tab/Shift-Tab/Up/Down only ─────────────────────
    if (e->type() == QEvent::FocusIn) {
        const auto reason = static_cast<QFocusEvent *>(e)->reason();
        if (reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason) {
            for (int i = 0; i < count; ++i) {
                if (lay->itemAt(i)->widget() == obj) {
                    if (m_focusIdx != i) { m_focusIdx = i; update(); }
                    break;
                }
            }
        } else {
            // Any other reason (mouse, window activation, etc.): no ring
            if (m_focusIdx >= 0) { m_focusIdx = -1; update(); }
        }
    } else if (e->type() == QEvent::FocusOut) {
        // Defer so a sibling's FocusIn updates m_focusIdx before we clear it.
        QTimer::singleShot(0, this, [this] {
            auto *fw  = QApplication::focusWidget();
            auto *lay = static_cast<QVBoxLayout *>(layout());
            for (int i = 0; i < lay->count(); ++i)
                if (lay->itemAt(i)->widget() == fw) return;
            if (m_focusIdx >= 0) { m_focusIdx = -1; update(); }
        });
    }

    // ── Up/Down: navigate rows like Tab/Shift-Tab, continuing out of card at boundary
    if (e->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(e)->key();
        if (key == Qt::Key_Up || key == Qt::Key_Down) {
            int cur = -1;
            for (int i = 0; i < count; ++i)
                if (lay->itemAt(i)->widget() == obj) { cur = i; break; }
            if (cur >= 0) {
                const bool forward = (key == Qt::Key_Down);
                const int  next    = forward ? cur + 1 : cur - 1;
                if (next >= 0 && next < count) {
                    QWidget *target = lay->itemAt(next)->widget();
                    if (target && target->focusPolicy() != Qt::NoFocus) {
                        target->setFocus(forward ? Qt::TabFocusReason
                                                 : Qt::BacktabFocusReason);
                        for (QWidget *p = parentWidget(); p; p = p->parentWidget()) {
                            if (auto *sa = qobject_cast<QScrollArea *>(p)) {
                                sa->ensureWidgetVisible(target);
                                break;
                            }
                        }
                    }
                } else if (!forward) {
                    // Up past the first row — exit the card like Shift-Tab.
                    // Dialog-level key guards prevent plain Up from wrapping at
                    // the first focusable control.
                    focusNextPrevChild(false);
                } else {
                    // Down past the last row: only advance if there is actually
                    // a focusable widget below this card.  Without this guard,
                    // focusNextPrevChild(true) wraps back to the top of the
                    // dialog — that should only happen on Tab, not Down.
                    auto *from = qobject_cast<QWidget *>(obj);
                    const int cardBottom = mapToGlobal(QPoint(0, height())).y();
                    for (QWidget *w = from ? from->nextInFocusChain() : nullptr;
                         w && w != from; w = w->nextInFocusChain()) {
                        if (!isAncestorOf(w)
                                && w->focusPolicy() != Qt::NoFocus
                                && !w->isHidden()
                                && w->isVisibleTo(w->window())) {
                            if (w->mapToGlobal(QPoint(0, 0)).y() >= cardBottom)
                                focusNextPrevChild(true);
                            break;
                        }
                    }
                }
                return true;
            }
        }
    }

    return false;
}

void SettingsCard::leaveEvent(QEvent *)
{
    if (m_hoverIdx >= 0) { m_hoverIdx = -1; update(); }
}

void SettingsCard::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QPalette &pal  = palette();
    const bool      dark = pal.color(QPalette::Window).lightness() < 128;
    const QRectF    card = QRectF(rect()).adjusted(SC_SHADOW, SC_SHADOW,
                                                   -SC_SHADOW, -SC_SHADOW);

    // ── Drop shadow ───────────────────────────────────────────────────────────
    p.setPen(Qt::NoPen);
    for (int i = SC_SHADOW; i >= 1; --i) {
        const int alpha = qRound(7.0 * qreal(SC_SHADOW - i + 1) / SC_SHADOW);
        p.setBrush(QColor(0, 0, 0, dark ? alpha / 2 : alpha));
        const qreal r = SC_RADIUS + i * 0.4;
        p.drawRoundedRect(card.adjusted(-i, -(i-1), i, i), r, r);
    }

    // ── Card background ───────────────────────────────────────────────────────
    const QColor borderCol = dark ? QColor(255, 255, 255, 28) : QColor(0, 0, 0, 18);
    p.setPen(QPen(borderCol, 1));
    p.setBrush(pal.color(QPalette::Base));
    p.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), SC_RADIUS, SC_RADIUS);

    auto *lay       = static_cast<QVBoxLayout *>(layout());
    const int count = lay->count();

    // Helper: row-band top/bottom in widget coordinates
    auto bandTop = [&](int idx) -> qreal {
        const QWidget *w    = lay->itemAt(idx)->widget();
        const QWidget *prev = idx > 0 ? lay->itemAt(idx-1)->widget() : nullptr;
        return prev ? qreal(prev->geometry().bottom() + w->geometry().top()) / 2
                    : card.top();
    };
    auto bandBottom = [&](int idx) -> qreal {
        const QWidget *w    = lay->itemAt(idx)->widget();
        const QWidget *next = idx < count-1 ? lay->itemAt(idx+1)->widget() : nullptr;
        return next ? qreal(w->geometry().bottom() + next->geometry().top()) / 2
                    : card.bottom();
    };

    // ── Hover highlight ───────────────────────────────────────────────────────
    if (m_hoverIdx >= 0 && m_hoverIdx < count && lay->itemAt(m_hoverIdx)->widget()) {
        const qreal top    = bandTop(m_hoverIdx);
        const qreal bottom = bandBottom(m_hoverIdx);
        QPainterPath clip;
        clip.addRoundedRect(card, SC_RADIUS, SC_RADIUS);
        p.save();
        p.setClipPath(clip);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.fillRect(QRectF(card.left(), top, card.width(), bottom - top),
                   dark ? QColor(255, 255, 255, 15) : QColor(0, 0, 0, 8));
        p.restore();
    }

    // ── Separators between items ──────────────────────────────────────────────
    if (count >= 2) {
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QPen(pal.color(QPalette::Mid), 1));
        for (int i = 1; i < count; ++i) {
            const QWidget *a = lay->itemAt(i-1)->widget();
            const QWidget *b = lay->itemAt(i)->widget();
            if (!a || !b) continue;
            const int y = (a->geometry().bottom() + b->geometry().top()) / 2;
            p.drawLine(qRound(card.left()) + 1, y, qRound(card.right()) - 1, y);
        }
    }

    // ── Keyboard focus ring at row-band level ─────────────────────────────────
    if (m_focusIdx >= 0 && m_focusIdx < count && lay->itemAt(m_focusIdx)->widget()) {
        const qreal top    = bandTop(m_focusIdx);
        const qreal bottom = bandBottom(m_focusIdx);
        const QRectF ring(card.left() + 1.5, top    + 1.5,
                          card.width() - 3,  bottom - top - 3);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(pal.color(QPalette::Highlight), 2));
        p.setBrush(Qt::NoBrush);
        const bool roundTop = (m_focusIdx == 0);
        const bool roundBot = (m_focusIdx == count - 1);
        p.drawPath(focusRingPath(ring, roundTop, roundBot, SC_RADIUS - 1.5));
    }
}

// ── NavigationRow ─────────────────────────────────────────────────────────────

NavigationRow::NavigationRow(const QString &label, Icon icon, QWidget *parent)
    : QAbstractButton(parent)
{
    setText(label);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    const QString iconName = (icon == Icon::ExternalLink)
                             ? QLatin1String("actions/external-link-symbolic")
                             : QLatin1String("ui/go-next-symbolic");
    m_icon = recoloredIcon(iconName,
                           QApplication::palette().color(QPalette::WindowText),
                           ROW_ICON_SZ);
}

NavigationRow::NavigationRow(const QString &label, const QUrl &url, QWidget *parent)
    : NavigationRow(label, Icon::ExternalLink, parent)
{
    connect(this, &QAbstractButton::clicked,
            this, [url] { QDesktopServices::openUrl(url); });
}

void NavigationRow::setValueText(const QString &v)
{
    m_value = v;
    updateGeometry();
    update();
}

QSize NavigationRow::sizeHint() const
{
    return QSize(200, qMax(fontMetrics().height(), ROW_MIN_H) + 2 * ROW_VPAD);
}

void NavigationRow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QPalette &pal = palette();
    const QMargins  m   = contentsMargins();
    const QRect     r   = rect().adjusted(m.left(), m.top(), -m.right(), -m.bottom());

    // Content area — leave room for icon + right margin + small text gap
    const QRect contentR = r.adjusted(0, 0, -(ROW_ICON_RIGHT + ROW_ICON_SZ + 4), 0);

    p.setFont(font());
    if (!text().isEmpty() && !m_value.isEmpty()) {
        p.setPen(pal.color(QPalette::WindowText));
        p.drawText(contentR, Qt::AlignLeft | Qt::AlignVCenter, text());
        p.setPen(pal.color(QPalette::PlaceholderText));
        p.drawText(contentR, Qt::AlignRight | Qt::AlignVCenter, m_value);
    } else if (!text().isEmpty()) {
        p.setPen(pal.color(QPalette::WindowText));
        p.drawText(contentR, Qt::AlignLeft | Qt::AlignVCenter, text());
    } else if (!m_value.isEmpty()) {
        p.setPen(pal.color(QPalette::WindowText));
        p.drawText(contentR, Qt::AlignLeft | Qt::AlignVCenter, m_value);
    }

    if (!m_icon.isNull()) {
        const QRect iconRect(r.right() - ROW_ICON_RIGHT - ROW_ICON_SZ,
                             (r.height() - ROW_ICON_SZ) / 2,
                             ROW_ICON_SZ, ROW_ICON_SZ);
        m_icon.paint(&p, iconRect);
    }
}

// ── TextRow ───────────────────────────────────────────────────────────────────

TextRow::TextRow(const QString &text, QWidget *parent)
    : QWidget(parent), m_text(text)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize TextRow::sizeHint() const
{
    return QSize(200, qMax(fontMetrics().height(), ROW_MIN_H) + 2 * ROW_VPAD);
}

void TextRow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setPen(palette().color(QPalette::WindowText));
    p.setFont(font());
    p.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, m_text);
}

// ── SettingsToggle ────────────────────────────────────────────────────────────

SettingsToggle::SettingsToggle(const QString &text, QWidget *parent)
    : QAbstractButton(parent)
{
    setText(text);
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize SettingsToggle::sizeHint() const
{
    const QFontMetrics fm(font());
    return QSize(fm.horizontalAdvance(text()) + SSB_SPACING + PILL_W,
                 qMax(fm.height(), PILL_H) + 2 * ROW_VPAD);
}

void SettingsToggle::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette &pal = palette();
    const QRect     r   = rect();

    // Label
    const QFontMetrics fm(font());
    const int ty = (r.height() + fm.ascent() - fm.descent()) / 2;
    p.setPen(isEnabled() ? pal.color(QPalette::WindowText)
                         : pal.color(QPalette::Disabled, QPalette::WindowText));
    p.setFont(font());
    p.drawText(QPoint(0, ty), text());

    // Pill track
    const int    pillX = r.width() - PILL_W;
    const int    pillY = (r.height() - PILL_H) / 2;
    const QRectF pill(pillX, pillY, PILL_W, PILL_H);
    p.setPen(Qt::NoPen);
    p.setBrush(isChecked() ? pal.color(QPalette::Highlight) : pal.color(QPalette::Mid));
    p.drawRoundedRect(pill, PILL_H / 2.0, PILL_H / 2.0);

    // Thumb
    const int thumbD = PILL_H - 2 * THUMB_MARGIN;
    const int thumbX = isChecked() ? pillX + PILL_W - THUMB_MARGIN - thumbD
                                   : pillX + THUMB_MARGIN;
    p.setBrush(Qt::white);
    p.drawEllipse(QRect(thumbX, pillY + THUMB_MARGIN, thumbD, thumbD));
}

// ── StepSpinBox ───────────────────────────────────────────────────────────────

StepSpinBox::StepSpinBox(const QString &label, int min, int max, int step,
                         QWidget *parent)
    : QWidget(parent), m_label(label), m_value(min), m_min(min), m_max(max),
      m_step(step)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void StepSpinBox::setValue(int v)
{
    v = qBound(m_min, v, m_max);
    if (v == m_value) return;
    m_value = v;
    update();
    emit valueChanged(m_value);
}

QSize StepSpinBox::sizeHint() const
{
    const QFontMetrics fm(font());
    return QSize(fm.horizontalAdvance(m_label) + SSB_SPACING
                 + SSB_VAL_W + SSB_VAL_GAP + SSB_BTN_W * 2 + 1,
                 qMax(fm.height(), SSB_BTN_H) + 2 * ROW_VPAD);
}

QRect StepSpinBox::groupRect() const
{
    const int gy = (height() - SSB_BTN_H) / 2;
    return QRect(width() - SSB_BTN_W * 2 - 1, gy, SSB_BTN_W * 2 + 1, SSB_BTN_H);
}

QRect StepSpinBox::minusRect() const
{
    const QRect g = groupRect();
    return QRect(g.left(), g.top(), SSB_BTN_W, g.height());
}

QRect StepSpinBox::plusRect() const
{
    const QRect g = groupRect();
    return QRect(g.left() + SSB_BTN_W + 1, g.top(), SSB_BTN_W, g.height());
}

StepSpinBox::HitZone StepSpinBox::hitZone(const QPoint &pos) const
{
    if (minusRect().contains(pos)) return Minus;
    if (plusRect().contains(pos))  return Plus;
    return None;
}

void StepSpinBox::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette  &pal = palette();
    const QRect      r   = rect();
    const QFontMetrics fm(font());

    // Label
    const int ty = (r.height() + fm.ascent() - fm.descent()) / 2;
    p.setPen(pal.color(QPalette::WindowText));
    p.setFont(font());
    p.drawText(QPoint(0, ty), m_label);

    // Value text
    const QRect grp  = groupRect();
    const QRect valR(0, 0, grp.left() - SSB_VAL_GAP, r.height());
    p.drawText(valR, Qt::AlignRight | Qt::AlignVCenter, QString::number(m_value));

    // Button group background
    const QColor btnBg     = pal.color(QPalette::Button);
    const QColor btnBorder = pal.color(QPalette::Mid);
    const QRectF grpF      = QRectF(grp).adjusted(0, 0, -1, -1);
    p.setPen(QPen(btnBorder, 1));
    p.setBrush(btnBg);
    p.drawRoundedRect(grpF.adjusted(0.5, 0.5, -0.5, -0.5), SSB_RADIUS, SSB_RADIUS);

    // Hover / press overlays
    const bool   dark     = pal.color(QPalette::Window).lightness() < 128;
    const QColor hoverCol = dark ? QColor(255, 255, 255, 30) : QColor(0, 0, 0, 22);
    const QColor presCol  = dark ? QColor(255, 255, 255, 55) : QColor(0, 0, 0, 45);

    auto drawOverlay = [&](HitZone zone, const QRect &btnR) {
        if (m_pressed != zone && m_hover != zone) return;
        p.save();
        QPainterPath clip;
        clip.addRoundedRect(grpF, SSB_RADIUS, SSB_RADIUS);
        p.setClipPath(clip);
        p.setPen(Qt::NoPen);
        p.setBrush(m_pressed == zone ? presCol : hoverCol);
        p.drawRect(btnR);
        p.restore();
    };
    drawOverlay(Minus, minusRect());
    drawOverlay(Plus,  plusRect());

    // Divider
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(btnBorder);
    const int divX = grp.left() + SSB_BTN_W;
    p.drawLine(divX, grp.top() + 3, divX, grp.bottom() - 3);

    // Symbols
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(pal.color(QPalette::ButtonText));
    p.setFont(font());
    p.drawText(minusRect(), Qt::AlignCenter, "−");
    p.drawText(plusRect(),  Qt::AlignCenter, "+");
}

void StepSpinBox::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        m_pressed = hitZone(e->pos());
        update();
    }
}

void StepSpinBox::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        if (m_pressed != None && hitZone(e->pos()) == m_pressed) {
            if (m_pressed == Minus) setValue(m_value - m_step);
            else                    setValue(m_value + m_step);
        }
        m_pressed = None;
        m_hover   = hitZone(e->pos());
        update();
    }
}

void StepSpinBox::mouseMoveEvent(QMouseEvent *e)
{
    const HitZone h = hitZone(e->pos());
    if (h != m_hover) {
        m_hover = h;
        setCursor(h != None ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void StepSpinBox::leaveEvent(QEvent *)
{
    m_hover = m_pressed = None;
    unsetCursor();
    update();
}

void StepSpinBox::keyPressEvent(QKeyEvent *e)
{
    switch (e->key()) {
    case Qt::Key_Right: setValue(m_value + m_step); e->accept(); break;
    case Qt::Key_Left:  setValue(m_value - m_step); e->accept(); break;
    default: QWidget::keyPressEvent(e); break;
    }
}

// ── DangerButton ──────────────────────────────────────────────────────────────

DangerButton::DangerButton(const QString &text, QWidget *parent)
    : QAbstractButton(parent)
{
    setText(text);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize DangerButton::sizeHint() const
{
    const QFontMetrics fm(font());
    return QSize(fm.horizontalAdvance(text()), fm.height() + 2 * ROW_VPAD);
}

void DangerButton::paintEvent(QPaintEvent *)
{
    static const QColor kDanger("#e01b24");
    QPainter p(this);
    p.setFont(font());
    p.setPen(isEnabled() ? kDanger
                         : palette().color(QPalette::Disabled, QPalette::WindowText));
    p.drawText(rect(), Qt::AlignCenter, text());
}
