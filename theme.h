#ifndef THEME_H
#define THEME_H

// Applies Adwaita Light or Dark (auto-detected from Qt colour scheme hint)
// using the built-in Fusion style + palette + QSS.  No system packages needed.
void applyAdwaitaTheme();

class QMenu;
// Call on every QMenu after construction so the stylesheet (including
// border-radius) renders correctly via a transparent frameless window.
void themeMenu(QMenu *menu);

#endif // THEME_H
