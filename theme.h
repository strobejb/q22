#ifndef THEME_H
#define THEME_H

// Applies Adwaita Light or Dark (auto-detected from Qt colour scheme hint)
// using the built-in Fusion style + palette + QSS.  No system packages needed.
void applyAdwaitaTheme();

class QMenu;
// Call on every QMenu after construction so the stylesheet (including
// border-radius) renders correctly via a transparent frameless window.
void themeMenu(QMenu *menu);

#ifdef Q_OS_WIN
class QIcon;
class QColor;
// Render a single Segoe MDL2 Assets / Segoe Fluent Icons glyph as a
// DPR-aware QIcon.  Returns a null QIcon if neither font is available.
// logicalPx is the font/draw size in logical (device-independent) pixels.
QIcon segoeIcon(uint codePoint, const QColor &color, int logicalPx = 14);
#endif

#endif // THEME_H
