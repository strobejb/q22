#ifndef WINDOWS_CHROME_H
#define WINDOWS_CHROME_H

// Windows-only DWM / non-client-area chrome helpers.
// All declarations are guarded so non-Windows TUs can include this header
// without pulling in any Windows dependencies.

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>

// Windows 11 SDK attributes — define locally so the build works with older
// SDKs and MinGW headers that don't yet declare them.
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#  define DWMWA_WINDOW_CORNER_PREFERENCE 33
#  define DWMWCP_ROUND 2
#endif
#ifndef DWMWA_BORDER_COLOR
#  define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#  define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// Resize hit-test strip width (pixels) used in MainWindow::eventFilter.
static constexpr int RESIZE_MARGIN = 5;

// Set rounded corners and pin the 1-px DWM border colour to the Adwaita
// palette (dark or light) rather than the system accent colour.
void applyWindows11Styling(HWND hwnd, bool dark);

// Apply DWMWA_USE_IMMERSIVE_DARK_MODE to w based on the current colour scheme.
// No-op for frameless windows (they have no native title bar).
void applyDwmDarkMode(QWidget *w);

// Install the global event filter that calls applyDwmDarkMode whenever any
// top-level window is shown.  Safe to call multiple times — installs once.
void installDarkModeFilter();

// Deferred sweep: schedule applyDwmDarkMode for all currently visible
// top-level windows via QTimer::singleShot(0) so it runs after Qt's own
// QEvent::ApplicationPaletteChange handling (which can reset the attribute).
// Call this at the end of every applyAdwaitaTheme() invocation.
void sweepDwmDarkMode();

#endif // Q_OS_WIN
#endif // WINDOWS_CHROME_H
