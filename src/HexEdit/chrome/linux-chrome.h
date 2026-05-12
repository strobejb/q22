#ifndef LINUX_CHROME_H
#define LINUX_CHROME_H

// Linux / non-Windows CSD (Client-Side Decoration) helpers.
// All declarations are guarded so Windows TUs can include this header
// without pulling in any Linux-specific dependencies.

#ifndef Q_OS_WIN

#include "shadow-chrome.h"

#include <QWidget>

// ── Shadow / resize constants ─────────────────────────────────────────────────
// KDE: the window draws its own drop-shadow (Firefox-style: transparent margin
// + painted gradient, no KWin API required).  The margin also acts as the
// edge-resize hit zone so the user can grab the window from the shadow.
// Non-KDE compositors (GNOME, …) provide the shadow; only a narrow resize
// strip is needed.
static constexpr int kShadowSize   = 18; // KDE: shadow margin (also the resize zone)
static constexpr int kCornerRadius = 10; // must match paintEvent's drawRoundedRect
static constexpr int kResizeMargin =  5; // non-KDE: narrow strip at the window edge

// Returns true when running inside a KDE Plasma session.  KDE draws compositor
// shadows only via KWindowEffects, so the app must paint its own shadow (the
// gradient-in-transparent-margin approach).  On GNOME and other compositors the
// WM provides a shadow automatically — painting our own would double it.
bool isKDE();

// ── CornerClipper ─────────────────────────────────────────────────────────────
// Transparent overlay that restores the shadow gradient in the four corner
// triangles of the content rect after child widgets have overdrawn them with
// opaque backgrounds.
//
// MainWindow::paintEvent paints the shadow rings (including the corner areas)
// before children paint.  Child widgets subsequently paint their full rects,
// overwriting those corners with opaque pixels.  The CornerClipper re-runs the
// same shadow loop (clipped to the corner-triangle path) using
// CompositionMode_Source to overwrite whatever the children painted.
//
// It is raised to the top of the z-order so it paints last, and installs event
// filters on every sibling so it can re-run its paintEvent in the same
// backing-store flush cycle whenever a sibling repaints.
// Used only on KDE (self-drawn shadow).
class CornerClipper : public ShadowCornerClipper
{
public:
    explicit CornerClipper(QWidget *parent);
};

#endif // Q_OS_WIN
#endif // LINUX_CHROME_H
