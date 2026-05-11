#include "focusnavigation.h"
#include "paletteswatch.h"
#include "slideoverlay.h"
#include "theme.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QAbstractButton>
#include <QPainter>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QResizeEvent>
#include <QScreen>
#include <QTimer>
#include <QToolButton>

class OverlayBackButton : public QAbstractButton
{
public:
    explicit OverlayBackButton(const QString &label, QWidget *parent = nullptr)
        : QAbstractButton(parent)
    {
        setText(label);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

        QFont f = font();
        f.setBold(true);
        setFont(f);

        const QColor fg = palette().color(QPalette::WindowText);
        m_icon = recoloredIcon("go-previous-symbolic", fg, kIconSz);
        setToolTip(label.isEmpty() ? tr("Back") : label);
    }

    QSize sizeHint() const override
    {
        const QFontMetrics fm(font());
        const int textW = text().isEmpty() ? 0 : fm.horizontalAdvance(text());
        const int gap = textW > 0 ? kGap : 0;
        return QSize(2 * kPadX + kIconSz + gap + textW,
                     qMax(kMinH, qMax(kIconSz, fm.height()) + 2 * kPadY));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const QPalette &pal = palette();
        if (isDown() || underMouse()) {
            p.setPen(Qt::NoPen);
            p.setBrush(isDown() ? pal.color(QPalette::Mid)
                                 : pal.color(QPalette::Button));
            p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
        }
        if (m_keyboardFocus) {
            p.setPen(QPen(pal.color(QPalette::Highlight), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 7, 7);
        }

        int x = kPadX;
        m_icon.paint(&p, QRect(x, (height() - kIconSz) / 2, kIconSz, kIconSz));
        x += kIconSz + (text().isEmpty() ? 0 : kGap);

        if (!text().isEmpty()) {
            p.setFont(font());
            p.setPen(pal.color(QPalette::WindowText));
            const QFontMetrics fm(font());
            const int ty = (height() + fm.ascent() - fm.descent()) / 2;
            p.drawText(QPoint(x, ty), text());
        }
    }

    void enterEvent(QEnterEvent *e) override { update(); QAbstractButton::enterEvent(e); }
    void leaveEvent(QEvent *e) override { update(); QAbstractButton::leaveEvent(e); }
    void focusInEvent(QFocusEvent *e) override
    {
        m_keyboardFocus = e->reason() == Qt::TabFocusReason
                       || e->reason() == Qt::BacktabFocusReason;
        update();
        QAbstractButton::focusInEvent(e);
    }
    void focusOutEvent(QFocusEvent *e) override
    {
        m_keyboardFocus = false;
        update();
        QAbstractButton::focusOutEvent(e);
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        const auto oldPolicy = focusPolicy();
        setFocusPolicy(Qt::NoFocus);
        QAbstractButton::mousePressEvent(e);
        setFocusPolicy(oldPolicy);
    }
    void keyPressEvent(QKeyEvent *e) override
    {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            click();
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right) {
            e->accept();
            return;
        }
        QAbstractButton::keyPressEvent(e);
    }

private:
    static constexpr int kIconSz = 16;
    static constexpr int kGap = 5;
    static constexpr int kPadX = 6+2;
    static constexpr int kPadY = 8;//3+4;
    static constexpr int kMinH = 28;
    QIcon m_icon;
    bool m_keyboardFocus = false;
};

static bool isInPaletteSwatchGrid(QWidget *widget)
{
    for (QWidget *w = widget; w; w = w->parentWidget()) {
        if (qobject_cast<PaletteSwatchGrid *>(w))
            return true;
    }
    return false;
}

// ── SlideOverlay ──────────────────────────────────────────────────────────────

SlideOverlay::SlideOverlay(QWidget *parent)
    : QWidget(parent)
{
    Q_ASSERT(parent);

    // Fill with the window background so the panel looks solid during animation.
    setAutoFillBackground(true);

    // ── Animation ─────────────────────────────────────────────────────────────
    m_anim = new QPropertyAnimation(this, "pos", this);
    m_anim->setDuration(k_duration);

    // ── Back / cancel button ──────────────────────────────────────────────────
    m_backBtn = new QToolButton(this);
    m_backBtn->setIcon(QIcon::fromTheme(
        "go-previous-symbolic",
        QIcon(":/icons/hicolor/scalable/actions/go-previous-symbolic.svg")));
    m_backBtn->setProperty("iconThemeName", "go-previous-symbolic");
    m_backBtn->setIconSize(QSize(16, 16));
    m_backBtn->setAutoRaise(true);
    m_backBtn->setFixedSize(28, 28);
    m_backBtn->setToolTip(tr("Back"));
    m_backBtn->move(8, (k_headerH - 28) / 2);
    m_backBtn->hide();

    connect(m_backBtn, &QToolButton::clicked, this, [this]() {
        if (m_content) m_content->reject();
    });

    // Track parent resize so the overlay stays in sync.
    parent->installEventFilter(this);

    hide();
}

// True while a dialog is hosted (animating in, open, or animating out).
bool SlideOverlay::isActive() const { return m_content != nullptr; }

// Dismiss the hosted dialog immediately (equivalent to the back button).
void SlideOverlay::dismiss() { if (m_content) m_content->reject(); }


// ── public ────────────────────────────────────────────────────────────────────

void SlideOverlay::slideIn(QDialog *dlg, std::function<void(int)> onFinished,
                           bool resizeParent)
{
    if (m_content) return;  // already hosting a dialog

    m_content = dlg;

    // ── Embed as a plain child widget FIRST ───────────────────────────────────
    // Do this before any resize/show operations.  The dialog is currently a
    // hidden top-level window whose native handle (HWND on Windows) will be
    // destroyed and recreated when flags/parent change.  Doing it now — while
    // everything is still hidden — avoids any OS-level flash from that handle
    // transition.
    m_content->setWindowFlags(Qt::Widget);
    m_content->setParent(this);
    m_content->setMinimumSize(0, 0);    // allow layout to be smaller than hint
    m_content->installEventFilter(this); // suppress the dialog's own hide()

    // ── Query size hint after embedding (flags are now correct) ──────────────
    const QSize contentHint = m_content->sizeHint();

    // ── Hide Cancel / Close buttons — back chevron takes that role ────────────
    // Use buttons() + buttonRole() so we only touch QAbstractButton (complete
    // type); QDialogButtonBox::button() returns QPushButton* which may be
    // forward-declared and incomplete in this translation unit.
    const auto boxes = m_content->findChildren<QDialogButtonBox *>();
    for (auto *bb : boxes) {
        for (auto *btn : bb->buttons()) {
            const auto role = bb->buttonRole(btn);
            if (role == QDialogButtonBox::RejectRole ||
                role == QDialogButtonBox::DestructiveRole)
                btn->hide();
        }
    }

    // ── Back button: inline inside dialog header row, or floating strip ──────
    // If the dialog marks a QWidget with objectName "overlayHeader" and that
    // widget has a QHBoxLayout, we insert the back button as its first item.
    // If the header starts with a QLabel, the button absorbs that text so the
    // chevron and label share one hover/focus rectangle.
    // Otherwise the floating m_backBtn strip is used.
    m_inlineMode = false;
    const QColor fg   = palette().windowText().color();
    const bool   dark = palette().window().color().lightness() < 128;
    const QString btnSS = QString(
        "QToolButton{border:none;border-radius:6px;background:transparent;}"
        "QToolButton:hover{background:%1;}"
        "QToolButton:pressed{background:%2;}"
    ).arg(dark ? "rgba(255,255,255,0.15)" : "rgba(0,0,0,0.10)",
          dark ? "rgba(255,255,255,0.25)" : "rgba(0,0,0,0.18)");

    if (auto *headerWidget = m_content->findChild<QWidget *>(QLatin1String("overlayHeader"))) {
        if (auto *hlay = qobject_cast<QHBoxLayout *>(headerWidget->layout())) {
            QString labelText;
            if (hlay->count() > 0) {
                if (auto *first = hlay->itemAt(0)->widget()) {
                    if (auto *label = qobject_cast<QLabel *>(first)) {
                        labelText = label->text();
                        if (auto *item = hlay->takeAt(0))
                            delete item;
                        label->hide();
                        label->deleteLater();
                    }
                }
            }

            auto *inlineBtn = new OverlayBackButton(labelText, headerWidget);
            inlineBtn->setObjectName(QStringLiteral("overlayBackButton"));
            inlineBtn->installEventFilter(this);
            if (labelText.isEmpty()) {
                inlineBtn->setFixedSize(28, 28);
                inlineBtn->setStyleSheet(btnSS);
            }
            connect(inlineBtn, &QAbstractButton::clicked, this, [this]() {
                if (m_content) m_content->reject();
            });
            hlay->insertWidget(0, inlineBtn);
            m_inlineMode = true;
        }
    }
    if (!m_inlineMode) {
        m_backBtn->setIcon(recoloredIcon("go-previous-symbolic", fg, 16));
        m_backBtn->setStyleSheet(btnSS);
        m_backBtn->installEventFilter(this);
        m_backBtn->show();
        m_backBtn->raise();
    }

    // ── Optionally resize the parent to fit the child ─────────────────────────
    // Suppress repaints on the parent during the resize: it will be fully
    // covered by the overlay as soon as the animation starts, so no
    // intermediate resize state should ever be visible.
    if (resizeParent) {
        parentWidget()->setUpdatesEnabled(false);
        resizeParentToFit(contentHint);
        parentWidget()->setUpdatesEnabled(true);
    } else {
        m_didResizeParent = false;
    }

    // ── Size overlay, snap off-screen, THEN show ──────────────────────────────
    // Critically: move() is called before show() so the overlay is never
    // rendered at its resting position (0, 0) before the animation begins.
    syncToParent();
    move(-width(), chromeTopInset());
    show();
    raise();
    m_content->show();
    for (QWidget *child : m_content->findChildren<QWidget *>())
        child->installEventFilter(this);
    auto *tabScope = m_inlineMode ? static_cast<QWidget *>(m_content)
                                  : static_cast<QWidget *>(this);
    // The inline back button is inserted dynamically and overlay geometry is
    // finalized during show().  Rebuild the tab chain on the next turn so Qt's
    // normal traversal sees the actual visual order.
    QTimer::singleShot(0, tabScope, [tabScope]() {
        FocusNavigation::assignTabOrder(tabScope);
    });

    // ── Wire up finish / slide-out ────────────────────────────────────────────
    connect(dlg, &QDialog::finished, this,
            [this, onFinished](int result) {
                if (onFinished) onFinished(result);
                startSlideOut();
            },
            Qt::SingleShotConnection);

    startSlideIn();
}

// ── private helpers ───────────────────────────────────────────────────────────

void SlideOverlay::resizeParentToFit(const QSize &contentHint)
{
    QWidget *par = parentWidget();
    m_savedParentSize  = par->size();
    m_didResizeParent  = true;

    const int wantH = contentHint.height() + k_headerH;
    const int maxH  = par->screen()
                          ? par->screen()->availableGeometry().height() - 80
                          : wantH;
    const int newH  = qMin(wantH, maxH);

    // Unlock any fixed-height constraint set by PreferencesDialog::showEvent,
    // resize, then re-lock at the new height so the window isn't manually
    // resizable while the overlay is open.
    par->setFixedHeight(QWIDGETSIZE_MAX);
    par->resize(par->width(), newH + chromeTopInset());
    par->setFixedHeight(newH);
}

void SlideOverlay::restoreParentSize()
{
    QWidget *par = parentWidget();
    par->setFixedHeight(QWIDGETSIZE_MAX);
    par->resize(m_savedParentSize);
    par->setFixedHeight(m_savedParentSize.height());
}

int SlideOverlay::chromeTopInset() const
{
    QWidget *par = parentWidget();
    return par ? qMax(0, par->property("_qexedDialogChromeHeight").toInt()) : 0;
}

void SlideOverlay::syncToParent()
{
    const QSize sz = parentWidget()->size();
    const int top = chromeTopInset();
    setGeometry(x(), top, sz.width(), qMax(0, sz.height() - top));
    if (m_content) {
        if (m_inlineMode)
            m_content->setGeometry(0, 0, width(), height());
        else
            m_content->setGeometry(0, k_headerH, width(), height() - k_headerH);
    }
}

void SlideOverlay::startSlideIn()
{
    // The overlay has already been move()d to (-width(), 0) by slideIn()
    // before show() was called, so no snap is needed here.
    m_anim->stop();
    m_anim->disconnect();
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    m_anim->setStartValue(pos());   // animate from current (already off-screen) position
    m_anim->setEndValue(QPoint(0, chromeTopInset()));
    m_anim->start();
}

void SlideOverlay::startSlideOut()
{
    m_anim->stop();
    m_anim->disconnect();
    m_anim->setEasingCurve(QEasingCurve::InCubic);
    m_anim->setStartValue(pos());
    m_anim->setEndValue(QPoint(-parentWidget()->width(), chromeTopInset()));  // exit to the left

    connect(m_anim, &QPropertyAnimation::finished, this, [this]() {
        hide();
        move(0, chromeTopInset());     // reset position for the next slideIn

        m_backBtn->hide();
        m_inlineMode = false;

        if (m_content) {
            m_content->deleteLater();
            m_content = nullptr;
        }

        if (m_didResizeParent) {
            restoreParentSize();
            m_didResizeParent = false;
        }
    });

    m_anim->start();
}

// ── event filter ─────────────────────────────────────────────────────────────

bool SlideOverlay::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::FocusIn) {
        auto *w = qobject_cast<QWidget *>(obj);
        if (m_content && w && (w == m_content || m_content->isAncestorOf(w) || w == m_backBtn))
            FocusNavigation::ensureFocusedWidgetVisible(w);
    }

    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        auto *w = qobject_cast<QWidget *>(obj);
        if (m_content
                && w
                && w->objectName() == QLatin1String("overlayBackButton")
                && (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab)) {
            if (auto *grid = m_content->findChild<PaletteSwatchGrid *>()) {
                const bool backward = key->key() == Qt::Key_Backtab
                    || (key->key() == Qt::Key_Tab
                        && key->modifiers().testFlag(Qt::ShiftModifier));
                if (backward)
                    grid->focusLast(Qt::BacktabFocusReason);
                else
                    grid->focusFirst(Qt::TabFocusReason);
                key->accept();
                return true;
            }
        }

        const bool up = key->key() == Qt::Key_Up;
        const bool down = key->key() == Qt::Key_Down;
        if ((up || down)
                && m_content
                && w
                && (w == m_content || m_content->isAncestorOf(w) || w == m_backBtn)
                && !isInPaletteSwatchGrid(w)
                && !FocusNavigation::hasFocusableWidget(
                    this, w, up ? FocusNavigation::Direction::Up
                                : FocusNavigation::Direction::Down)) {
            return true;
        }
    }

    // Keep overlay in sync whenever the parent dialog is resized.
    if (obj == parentWidget() && event->type() == QEvent::Resize) {
        if (isVisible()) {
            syncToParent();
            if (m_content) {
                if (m_inlineMode)
                    m_content->setGeometry(0, 0, width(), height());
                else
                    m_content->setGeometry(0, k_headerH,
                                           width(), height() - k_headerH);
            }
            // Update the slide-out end position if currently animating.
            if (m_anim->state() == QAbstractAnimation::Running)
                m_anim->setEndValue(QPoint(-width(), chromeTopInset()));
        }
        return false;   // don't consume — parent still needs to process it
    }

    // Suppress the embedded dialog's own hide() so the overlay controls when
    // the panel disappears (we hide after the slide-out animation finishes).
    if (obj == m_content && event->type() == QEvent::Hide)
        return true;

    return QWidget::eventFilter(obj, event);
}

void SlideOverlay::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && m_content)
        m_content->reject();
    else
        QWidget::keyPressEvent(event);
}
