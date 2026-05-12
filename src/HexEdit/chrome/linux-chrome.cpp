//
//  linux-chrome.cpp - CSD / shadow / frame chrome for non-Windows platforms
//
//  Platform policy lives here: GNOME/Mutter gets compositor shadows, KDE/KWin
//  gets a self-painted shadow. The self-shadow paint and corner repair are
//  shared with Windows dialog chrome through shadow-chrome.cpp.
//

#include "linux-chrome.h"
#include "mainwindow.h"

#include <QPainter>

#ifndef Q_OS_WIN

bool isKDE()
{
    static const bool kde =
        qgetenv("XDG_CURRENT_DESKTOP").toUpper().contains("KDE");
    return kde;
}

static ShadowChromeParams kdeShadowParams()
{
    return {
        kShadowSize,
        kCornerRadius,
        80,
        2.0,
        QColor(0, 0, 0),
        QColor(),
    };
}

CornerClipper::CornerClipper(QWidget *parent)
    : ShadowCornerClipper(parent, kdeShadowParams())
{}

void MainWindow::paintEvent(QPaintEvent *)
{
    if (!m_useCustomTitleBar)
        return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);

    if (!isKDE()) {
        // Non-KDE (GNOME, etc.): the compositor provides the shadow. Just
        // paint the rounded window background so corner pixels stay transparent.
        p.setBrush(palette().window());
        if (isMaximized() || isFullScreen())
            p.drawRect(rect());
        else
            p.drawRoundedRect(rect(), kCornerRadius, kCornerRadius);
        return;
    }

    // KDE: self-drawn drop shadow in the transparent margin. The compositor
    // decision stays here; the paint math is shared with Windows dialogs.
    paintShadowedRoundedWindow(p, this, rect(), kdeShadowParams(), true);
}

void MainWindow::applyShadowMargin()
{
    if (!isKDE())
        return;
    if (m_useCustomTitleBar && !isMaximized() && !isFullScreen())
        setContentsMargins(kShadowSize, kShadowSize, kShadowSize, kShadowSize);
    else
        setContentsMargins(0, 0, 0, 0);
}

#endif // Q_OS_WIN
