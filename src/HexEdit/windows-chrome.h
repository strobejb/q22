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

// Resize hit-test strip width (pixels) used in MainWindow::eventFilter.
static constexpr int RESIZE_MARGIN = 5;

// Set rounded corners and pin the 1-px DWM border colour to the Adwaita
// palette (dark or light) rather than the system accent colour.
void applyWindows11Styling(HWND hwnd, bool dark);

#endif // Q_OS_WIN
#endif // WINDOWS_CHROME_H
