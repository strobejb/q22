#include "slideoverlay.h"
#include "theme.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QEvent>
#include <QKeyEvent>
#include <QAbstractButton>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QResizeEvent>
#include <QScreen>
#include <QToolButton>

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
    // widget has a QHBoxLayout, we insert the back button as its first item
    // and position the dialog flush to the top of the overlay (no strip).
    // Otherwise the floating m_backBtn strip is used.
    m_inlineMode = false;
    const QColor fg = palette().windowText().color();
    if (auto *headerWidget = m_content->findChild<QWidget *>(QLatin1String("overlayHeader"))) {
        if (auto *hlay = qobject_cast<QHBoxLayout *>(headerWidget->layout())) {
            auto *inlineBtn = new QToolButton(headerWidget);
            inlineBtn->setIcon(recoloredIcon("go-previous-symbolic", fg, 16));
            inlineBtn->setProperty("iconThemeName", "go-previous-symbolic");
            inlineBtn->setIconSize(QSize(16, 16));
            inlineBtn->setAutoRaise(true);
            inlineBtn->setFixedSize(28, 28);
            inlineBtn->setToolTip(tr("Back"));
            connect(inlineBtn, &QToolButton::clicked, this, [this]() {
                if (m_content) m_content->reject();
            });
            hlay->insertWidget(0, inlineBtn);
            m_inlineMode = true;
        }
    }
    if (!m_inlineMode) {
        m_backBtn->setIcon(recoloredIcon("go-previous-symbolic", fg, 16));
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
    move(-width(), 0);
    show();
    raise();
    m_content->show();
    m_content->setFocus();

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
    par->resize(par->width(), newH);
    par->setFixedHeight(newH);
}

void SlideOverlay::restoreParentSize()
{
    QWidget *par = parentWidget();
    par->setFixedHeight(QWIDGETSIZE_MAX);
    par->resize(m_savedParentSize);
    par->setFixedHeight(m_savedParentSize.height());
}

void SlideOverlay::syncToParent()
{
    const QSize sz = parentWidget()->size();
    resize(sz);
    if (m_content) {
        if (m_inlineMode)
            m_content->setGeometry(0, 0, sz.width(), sz.height());
        else
            m_content->setGeometry(0, k_headerH, sz.width(), sz.height() - k_headerH);
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
    m_anim->setEndValue(QPoint(0, 0));
    m_anim->start();
}

void SlideOverlay::startSlideOut()
{
    m_anim->stop();
    m_anim->disconnect();
    m_anim->setEasingCurve(QEasingCurve::InCubic);
    m_anim->setStartValue(pos());
    m_anim->setEndValue(QPoint(-parentWidget()->width(), 0));  // exit to the left

    connect(m_anim, &QPropertyAnimation::finished, this, [this]() {
        hide();
        move(0, 0);     // reset position for the next slideIn

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
    // Keep overlay in sync whenever the parent dialog is resized.
    if (obj == parentWidget() && event->type() == QEvent::Resize) {
        if (isVisible()) {
            const QSize sz = static_cast<QResizeEvent *>(event)->size();
            resize(sz);
            if (m_content) {
                if (m_inlineMode)
                    m_content->setGeometry(0, 0, sz.width(), sz.height());
                else
                    m_content->setGeometry(0, k_headerH,
                                           sz.width(), sz.height() - k_headerH);
            }
            // Update the slide-out end position if currently animating.
            if (m_anim->state() == QAbstractAnimation::Running)
                m_anim->setEndValue(QPoint(-sz.width(), 0));
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
