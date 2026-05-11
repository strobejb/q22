//
//  linux-chrome.cpp — CSD / shadow / frame chrome for non-Windows platforms
//
//  Implements the Linux/GNOME/KDE-specific window-frame helpers that would
//  otherwise clutter mainwindow.cpp.  All code in this file is compiled
//  only on non-Windows platforms (the #ifndef guard wraps the whole body).
//
//  Includes are placed OUTSIDE the #ifndef Q_OS_WIN guard so that
//  mainwindow.h is always parsed in the real compilation context.  On Windows
//  Q_OS_WIN is defined, paintEvent/applyShadowMargin are not declared in
//  MainWindow, and the definitions block below is skipped consistently.
//  clangd sees the same picture regardless of which platform it targets.
//

#include "linux-chrome.h"
#include "mainwindow.h"

#include <QPainter>
#include <QPainterPath>

#ifndef Q_OS_WIN

// ── isKDE ─────────────────────────────────────────────────────────────────────
//
// Returns true when running inside a KDE Plasma session.  Cached after the
// first call — the desktop environment never changes mid-session.

bool isKDE()
{
    static const bool kde =
        qgetenv("XDG_CURRENT_DESKTOP").toUpper().contains("KDE");
    return kde;
}

// ── CornerClipper ─────────────────────────────────────────────────────────────

CornerClipper::CornerClipper(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    setGeometry(parent->rect());
    raise();
    parent->installEventFilter(this);
    // Watch all existing direct children so we re-clear after their repaints.
    for (QObject *child : parent->children()) {
        if (auto *w = qobject_cast<QWidget *>(child); w && w != this)
            w->installEventFilter(this);
    }
}

bool CornerClipper::eventFilter(QObject *obj, QEvent *e)
{
    if (obj == parentWidget()) {
        if (e->type() == QEvent::Resize) {
            const auto *re = static_cast<QResizeEvent *>(e);
            setGeometry(QRect(QPoint(0, 0), re->size()));
            raise();
        } else if (e->type() == QEvent::ChildAdded) {
            // A new child was added — watch it too.
            if (auto *w = qobject_cast<QWidget *>(
                    static_cast<QChildEvent *>(e)->child());
                    w && w != this)
                w->installEventFilter(this);
        }
    } else if (obj != this && e->type() == QEvent::Paint) {
        // A sibling just painted and may have overdrawn our cleared corners.
        // Re-raise and queue a re-clear in the same backing-store pass.
        // update() is deferred but safe: Qt hasn't flushed the backing store
        // yet when Paint is dispatched, so the clipper will be included in
        // the same flush cycle (painted last due to its raised z-order).
        raise();
        update();
    }

    return false;
}

void CornerClipper::paintEvent(QPaintEvent *)
{
    auto *w = parentWidget();
    if (!w || w->isMaximized() || w->isFullScreen()) return;

    const QRectF content = QRectF(rect()).adjusted(
        kShadowSize, kShadowSize, -kShadowSize, -kShadowSize);
    if (content.isEmpty()) return;

    // The corner triangles (inside content rect, outside rounded rect) were
    // painted with the correct shadow gradient by MainWindow::paintEvent, but
    // child widgets then overdrew them with opaque backgrounds.  We restore
    // the shadow by re-running the same shadow loop, clipped to only those
    // triangle areas.  CompositionMode_Source overwrites whatever the child
    // widgets drew with the original shadow colour.
    QPainterPath contentPath, roundedPath;
    contentPath.addRect(content);
    roundedPath.addRoundedRect(content, kCornerRadius, kCornerRadius);
    const QPainterPath corners = contentPath - roundedPath;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setClipPath(corners);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.setPen(Qt::NoPen);

    for (int dist = kShadowSize; dist >= 1; --dist) {
        const qreal t   = qreal(kShadowSize - dist) / (kShadowSize - 1);
        const int alpha = int(80 * t * t);
        const QRectF r  = content.adjusted(-dist, -dist, dist, dist);
        p.setBrush(QColor(0, 0, 0, alpha));
        p.drawRoundedRect(r, kCornerRadius + dist, kCornerRadius + dist);
    }
}

// ── MainWindow::paintEvent ────────────────────────────────────────────────────
//
// Paints the window background.  On non-KDE compositors only a rounded-rect
// background is needed (the WM provides the shadow).  On KDE a full gradient
// shadow is painted into the transparent kShadowSize margin first, then the
// window background is drawn on top.

void MainWindow::paintEvent(QPaintEvent *)
{
    if (!m_useCustomTitleBar) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);

    if (!isKDE()) {
        // Non-KDE (GNOME, …): the compositor provides the shadow.  Just paint
        // the window background as a rounded rect so the corner pixels stay
        // transparent — that gives the window its rounded shape.
        p.setBrush(palette().window());
        if (isMaximized() || isFullScreen())
            p.drawRect(rect());
        else
            p.drawRoundedRect(rect(), kCornerRadius, kCornerRadius);
        return;
    }

    // KDE: self-drawn drop shadow (Firefox / GTK CSD style).
    // The window is expanded by kShadowSize on all sides (via setContentsMargins
    // in applyShadowMargin).  We paint a gradient shadow into that transparent
    // margin; KWin composites it normally — no KWin API required.
    const QRectF full    = QRectF(rect());
    const QRectF content = full.adjusted(kShadowSize, kShadowSize,
                                         -kShadowSize, -kShadowSize);

    if (isMaximized() || isFullScreen()) {
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.setBrush(palette().window());
        p.drawRect(full);
        return;
    }

    p.setCompositionMode(QPainter::CompositionMode_Source);
    for (int dist = kShadowSize; dist >= 1; --dist) {
        const qreal t     = qreal(kShadowSize - dist) / (kShadowSize - 1);
        const int   alpha = int(80 * t * t);
        const QRectF r    = content.adjusted(-dist, -dist, dist, dist);
        const qreal  rad  = kCornerRadius + dist;
        p.setBrush(QColor(0, 0, 0, alpha));
        p.drawRoundedRect(r, rad, rad);
    }

    p.setBrush(palette().window());
    p.drawRoundedRect(content, kCornerRadius, kCornerRadius);
}

// ── MainWindow::applyShadowMargin ─────────────────────────────────────────────
//
// KDE only: expand the window contents by kShadowSize on all sides so
// paintEvent() can paint a gradient shadow in the transparent margin.
// On non-KDE compositors (GNOME, …) the WM provides the shadow; adding a
// margin would cause a double shadow, so leave margins at zero.

void MainWindow::applyShadowMargin()
{
    if (!isKDE()) return;
    if (m_useCustomTitleBar && !isMaximized() && !isFullScreen())
        setContentsMargins(kShadowSize, kShadowSize, kShadowSize, kShadowSize);
    else
        setContentsMargins(0, 0, 0, 0);
}

#endif // Q_OS_WIN
