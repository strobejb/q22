//
//  windows-chrome.cpp — DWM / non-client-area chrome for Windows
//
//  Implements the Windows-specific window-frame and chrome-colour logic that
//  would otherwise clutter mainwindow.cpp.  All code in this file is compiled
//  only on Windows (the #ifdef wraps the whole body).
//
//  Includes are placed OUTSIDE the Q_OS_WIN guard so that
//  mainwindow.h is always parsed in the real compilation context.  On Linux
//  Q_OS_WIN is not defined, paintEvent/applyShadowMargin are declared instead
//  of nativeEvent/updateWinChromeColors, and the definitions block below is
//  skipped consistently.  clangd sees the same picture regardless of platform.
//

#include "windows-chrome.h"
#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "theme.h"
#include "finddialog.h"
#include "gotodialog.h"
#include "titlebar.h"

#include <QApplication>
#include <QPalette>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>

// ── applyWindows11Styling ─────────────────────────────────────────────────────
//
// The window is created as WS_OVERLAPPEDWINDOW (no Qt::FramelessWindowHint on
// Windows), so DWM already owns the rounded corners, accent border, and
// drop-shadow.  This function makes the corner preference explicit so a future
// style change can't reset it, and pins the 1-px DWM border to Adwaita palette
// colours instead of the system accent colour.

void applyWindows11Styling(HWND hwnd, bool dark)
{
    // Rounded corners — Win11 Build 22000+ enables this for WS_OVERLAPPEDWINDOW
    // by default, but be explicit so a style-change can't reset it.
    DWORD cornerPref = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                          &cornerPref, sizeof(cornerPref));

    // Override the 1-pixel DWM border with Adwaita palette colours instead of
    // the system accent colour.  COLORREF format is 0x00BBGGRR.
    // Light: #cdc7c2 → R=0xCD G=0xC7 B=0xC2
    // Dark:  #4a4a4a → R=G=B=0x4A
    COLORREF borderColor = dark ? 0x004A4A4A : 0x00C2C7CD;
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR,
                          &borderColor, sizeof(borderColor));
}

// ── MainWindow::updateWinChromeColors ─────────────────────────────────────────
//
// Updates the status bar and inline dialog backgrounds to match the title bar
// chrome colour, switching between the focused (Mica/accent) and unfocused
// (neutral grey) states to mirror Windows 11's native window behaviour.

void MainWindow::updateWinChromeColors()
{
    const bool active = isActiveWindow();

    // Prefer the toolbar colour override (status-bar / chrome-panel specific).
    // Fall back to windowsChromeBg() which handles the window-panel override and
    // the hardcoded Win11 neutrals.  The same active/inactive dimming ratios used
    // in windowsChromeBg() are applied here (3 % darker for light, 41 % lighter
    // for dark) so focus/unfocus transitions look consistent.
    const QColor tbOver = uiColourOverrides().toolbar;
    const QColor bg     = tbOver.isValid()
        ? (active ? tbOver
                  : (tbOver.lightness() >= 128 ? tbOver.darker(103)
                                               : tbOver.lighter(141)))
        : windowsChromeBg(active);

    const bool dark = bg.lightness() < 128;
    // Re-apply DWMWA_BORDER_COLOR so the 1-px DWM border matches the new scheme.
    // applyWindows11Styling is also called in showEvent (first paint), but it
    // must be repeated here because DWM caches the border colour independently
    // of DWMWA_USE_IMMERSIVE_DARK_MODE and only updates it when explicitly told.
    applyWindows11Styling(reinterpret_cast<HWND>(winId()), dark);

    // Apply the same DWM attributes to every other visible top-level window
    // (preferences dialog, bookmark dialog, …).  DWMWA_USE_IMMERSIVE_DARK_MODE
    // is swept by applyAdwaitaTheme, but DWMWA_BORDER_COLOR is not — without
    // this loop each dialog retains the 1-px hairline colour from the scheme
    // that was active when it was first shown.
    for (QWidget *w : QApplication::topLevelWidgets()) {
        if (w == this || !w->isVisible()) continue;
        if (w->windowFlags() & Qt::FramelessWindowHint) continue;
        applyWindows11Styling(reinterpret_cast<HWND>(w->winId()), dark);
    }

    const QColor comboHoverBg = dark ? bg.lighter(130) : bg.darker(107);

    // Status bar palette drives both the bar background and the combo colours:
    //   Window role → QStatusBar { background: palette(window); }            (normal)
    //                  QStatusBar QComboBox { background: palette(window); }  (normal)
    //   Button role → QStatusBar QComboBox:hover { background: palette(button); }
    //                  QStatusBar QComboBox:focus { background: palette(button); }
    // Both roles resolve from this palette because all QSS rules use descendant
    // selectors ("QStatusBar QComboBox …"), so Qt looks up palette() on the
    // ancestor (status bar), not the combo itself.
    QPalette sbPal;
    sbPal.setColor(QPalette::Window, bg);
    sbPal.setColor(QPalette::Button, comboHoverBg);
    ui->statusbar->setPalette(sbPal);
    ui->statusbar->setStyleSheet(QString());

    // FindDialog and GotoDialog use WA_StyledBackground — updating their
    // Window palette role is enough to repaint without touching their stylesheets.
    QPalette p;
    p.setColor(QPalette::Window, bg);
    m_findDialog->setPalette(p);
    m_gotoDialog->setPalette(p);
}

// ── MainWindow::nativeEvent ───────────────────────────────────────────────────
//
// Collapses the non-client area to zero when using a custom title bar:
// the title-bar and resize-border chrome never appear, but DWM still sees
// WS_THICKFRAME and applies its rounded corners, accent border, and drop-shadow.

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    MSG *msg = reinterpret_cast<MSG *>(message);
    if (m_useCustomTitleBar && msg->message == WM_NCCALCSIZE && msg->wParam == TRUE) {
        // When maximized, Windows extends the window rect slightly off-screen
        // (by the frame thickness) to hide the thick-frame border.  Without
        // compensation our client area would bleed under the taskbar.  Trim it
        // back by the DPI-aware frame size so the content stays on-screen.
        if (IsZoomed(msg->hwnd)) {
            auto *params = reinterpret_cast<NCCALCSIZE_PARAMS *>(msg->lParam);
            UINT dpi = GetDpiForWindow(msg->hwnd);
            int  bx  = GetSystemMetricsForDpi(SM_CXFRAME, dpi)
                     + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            int  by  = GetSystemMetricsForDpi(SM_CYFRAME, dpi)
                     + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            params->rgrc[0].left   += bx;
            params->rgrc[0].right  -= bx;
            params->rgrc[0].top    += by;
            params->rgrc[0].bottom -= by;
        }
        *result = 0;
        return true;
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}

#endif // Q_OS_WIN
