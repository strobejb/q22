#include "theme.h"
#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QMenu>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleHints>

#ifdef Q_OS_WIN
#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPixmap>

QIcon segoeIcon(uint codePoint, const QColor &color, int logicalPx)
{
    static const QString s_family = []() -> QString {
        const auto &fams = QFontDatabase::families();
        for (const QString &f : {QStringLiteral("Segoe Fluent Icons"),
                                  QStringLiteral("Segoe MDL2 Assets")})
            if (fams.contains(f)) return f;
        return {};
    }();
    if (s_family.isEmpty()) return {};

    // QPainter on a DPR-aware pixmap works in *logical* coordinates;
    // font size and draw rect must be in logical px (not physical px).
    const qreal dpr    = qApp->devicePixelRatio();
    const int   physPx = qRound(logicalPx * dpr);
    QFont font(s_family);
    font.setPixelSize(logicalPx);
    QPixmap pm(physPx, physPx);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setFont(font);
    p.setPen(color);
    p.drawText(QRect(0, 0, logicalPx, logicalPx), Qt::AlignCenter,
               QString(QChar(codePoint)));
    return QIcon(pm);
}
#endif

// ── Palette ───────────────────────────────────────────────────────────────────

static void applyPalette(bool dark)
{
    QPalette p;

    if (!dark) {
        p.setColor(QPalette::Window,          QColor("#fafafa"));
        p.setColor(QPalette::WindowText,      QColor("#2e3436"));
        p.setColor(QPalette::Base,            QColor("#ffffff"));
        p.setColor(QPalette::AlternateBase,   QColor("#f6f5f4"));
        p.setColor(QPalette::Text,            QColor("#2e3436"));
        p.setColor(QPalette::BrightText,      QColor("#ffffff"));
        p.setColor(QPalette::Button,          QColor("#e0dedb"));
        p.setColor(QPalette::ButtonText,      QColor("#2e3436"));
        p.setColor(QPalette::Highlight,       QColor("#3584e4"));
        p.setColor(QPalette::HighlightedText, QColor("#ffffff"));
        p.setColor(QPalette::Link,            QColor("#1a73e8"));
        p.setColor(QPalette::LinkVisited,     QColor("#865e3c"));
        p.setColor(QPalette::Light,           QColor("#ffffff"));
        p.setColor(QPalette::Midlight,        QColor("#ebebeb"));
        p.setColor(QPalette::Mid,             QColor("#cdc7c2"));
        p.setColor(QPalette::Dark,            QColor("#bfbab5"));
        p.setColor(QPalette::Shadow,          QColor("#9a9996"));
        p.setColor(QPalette::ToolTipBase,     QColor("#1e1e1e"));
        p.setColor(QPalette::ToolTipText,     QColor("#f6f5f4"));
        p.setColor(QPalette::PlaceholderText, QColor("#9a9996"));
        p.setColor(QPalette::Inactive, QPalette::Highlight,       QColor("#b0adb0"));
        p.setColor(QPalette::Inactive, QPalette::HighlightedText, QColor("#2e3436"));
        p.setColor(QPalette::Disabled, QPalette::WindowText,      QColor("#9a9996"));
        p.setColor(QPalette::Disabled, QPalette::Text,            QColor("#9a9996"));
        p.setColor(QPalette::Disabled, QPalette::ButtonText,      QColor("#9a9996"));
        p.setColor(QPalette::Disabled, QPalette::Highlight,       QColor("#c0beba"));
        p.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor("#9a9996"));
    } else {
        p.setColor(QPalette::Window,          QColor("#242424"));
        p.setColor(QPalette::WindowText,      QColor("#deddda"));
        p.setColor(QPalette::Base,            QColor("#1e1e1e"));
        p.setColor(QPalette::AlternateBase,   QColor("#2a2a2a"));
        p.setColor(QPalette::Text,            QColor("#deddda"));
        p.setColor(QPalette::BrightText,      QColor("#ffffff"));
        p.setColor(QPalette::Button,          QColor("#3d3d3d"));
        p.setColor(QPalette::ButtonText,      QColor("#deddda"));
        p.setColor(QPalette::Highlight,       QColor("#3584e4"));
        p.setColor(QPalette::HighlightedText, QColor("#ffffff"));
        p.setColor(QPalette::Link,            QColor("#78aeed"));
        p.setColor(QPalette::LinkVisited,     QColor("#c49a6c"));
        p.setColor(QPalette::Light,           QColor("#4a4a4a"));
        p.setColor(QPalette::Midlight,        QColor("#3a3a3a"));
        p.setColor(QPalette::Mid,             QColor("#3d3d3d"));
        p.setColor(QPalette::Dark,            QColor("#202020"));
        p.setColor(QPalette::Shadow,          QColor("#141414"));
        p.setColor(QPalette::ToolTipBase,     QColor("#f6f5f4"));
        p.setColor(QPalette::ToolTipText,     QColor("#2e3436"));
        p.setColor(QPalette::PlaceholderText, QColor("#6c6c6c"));
        p.setColor(QPalette::Inactive, QPalette::Highlight,       QColor("#4a4a52"));
        p.setColor(QPalette::Inactive, QPalette::HighlightedText, QColor("#deddda"));
        p.setColor(QPalette::Disabled, QPalette::WindowText,      QColor("#6c6c6c"));
        p.setColor(QPalette::Disabled, QPalette::Text,            QColor("#6c6c6c"));
        p.setColor(QPalette::Disabled, QPalette::ButtonText,      QColor("#6c6c6c"));
        p.setColor(QPalette::Disabled, QPalette::Highlight,       QColor("#4a4a4a"));
        p.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor("#6c6c6c"));
    }

    QApplication::setPalette(p);
}

// ── Stylesheet ────────────────────────────────────────────────────────────────

static QString buildStylesheet(bool dark)
{
    // Named colour tokens
    const QString accent      = "#3584e4";
    const QString accentHover = "#4a91e8";
    const QString accentFg    = "#ffffff";
    const QString danger      = "#e01b24";

    QString fg, border, btnBg, btnHover, btnActive,
            menuBg, inputBg,
            scrollHandle, scrollHover,
            statusBg, statusComboHover;

    if (!dark) {
        fg              = "#2e3436";
        border          = "#cdc7c2";
        btnBg           = "#e0dedb";
        btnHover        = "#d3d1ce";
        btnActive       = "#c8c6c3";
        menuBg          = "#ffffff";
        inputBg         = "#ffffff";
        scrollHandle    = "#cdc7c2";
        scrollHover     = "#9a9996";
        statusBg        = "#f6f5f4";
        statusComboHover= "#e8e6e3";
    } else {
        fg              = "#deddda";
        border          = "#4a4a4a";
        btnBg           = "#3d3d3d";
        btnHover        = "#484848";
        btnActive       = "#525252";
        menuBg          = "#2e2e2e";
        inputBg         = "#1e1e1e";
        scrollHandle    = "#5a5a5a";
        scrollHover     = "#787878";
        statusBg        = "#2a2a2a";
        statusComboHover= "#3a3a3a";
    }

    // Use a raw template with named tokens replaced below
    static const char TMPL[] = R"(

/* ── Global ──────────────────────────────────────────────────── */
QWidget {
    color: {fg};
}

/* ── Push buttons ────────────────────────────────────────────── */
QPushButton {
    background: {btnBg};
    border: 1px solid {border};
    border-radius: 6px;
    padding: 5px 16px;
    min-width: 80px;
}
QPushButton:hover   { background: {btnHover}; }
QPushButton:pressed { background: {btnActive}; border-color: {accent}; }
QPushButton:disabled { color: palette(disabled, windowtext); }
QPushButton[default="true"] {
    background: {accent};
    color: {accentFg};
    border-color: {accent};
}
QPushButton[default="true"]:hover   { background: {accentHover}; }
QPushButton[default="true"]:pressed { background: {accent}; }

/* ── Menus ───────────────────────────────────────────────────── */
QMenu {
    background: {menuBg};
    border: 1px solid {border};
    border-radius: 8px;
    padding: 6px 0;
}
QMenu::item {
    padding: 6px 28px 6px 32px;
    min-width: 180px;
    border-radius: 4px;
    margin: 1px 4px;
}
QMenu::item:selected {
    background: palette(highlight);
    color: palette(highlighted-text);
}
QMenu::item:disabled { color: palette(disabled, windowtext); }
QMenu::separator {
    height: 1px;
    background: {border};
    margin: 4px 8px;
}
QMenu::icon {
    width: 16px;
    height: 16px;
    margin-left: 20px;
}
QMenu::indicator {
    width: 16px;
    height: 16px;
    margin-left: 8px;
}

/* ── ComboBox ────────────────────────────────────────────────── */
QComboBox {
    background: {inputBg};
    border: 1px solid {border};
    border-radius: 6px;
    padding: 3px 8px;
    selection-background-color: {accent};
    selection-color: {accentFg};
}
QComboBox:hover { border-color: palette(shadow); }
QComboBox:focus { border: 2px solid {accent}; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox QAbstractItemView {
    background: {menuBg};
    border: 1px solid {border};
    border-radius: 6px;
    selection-background-color: {accent};
    selection-color: {accentFg};
    outline: none;
    padding: 4px;
}

/* ── Scroll bars ─────────────────────────────────────────────── */
QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 2px;
}
QScrollBar::handle:vertical {
    background: {scrollHandle};
    border-radius: 5px;
    min-height: 24px;
}
QScrollBar::handle:vertical:hover  { background: {scrollHover}; }
QScrollBar::add-line:vertical,
QScrollBar::sub-line:vertical      { height: 0; }
QScrollBar::add-page:vertical,
QScrollBar::sub-page:vertical      { background: transparent; }

QScrollBar:horizontal {
    background: transparent;
    height: 10px;
    margin: 2px;
}
QScrollBar::handle:horizontal {
    background: {scrollHandle};
    border-radius: 5px;
    min-width: 24px;
}
QScrollBar::handle:horizontal:hover  { background: {scrollHover}; }
QScrollBar::add-line:horizontal,
QScrollBar::sub-line:horizontal      { width: 0; }
QScrollBar::add-page:horizontal,
QScrollBar::sub-page:horizontal      { background: transparent; }

/* ── Status bar ──────────────────────────────────────────────── */
QStatusBar {
    background: {statusBg};
    border-top: 1px solid {border};
    padding: 6px 0;
}
QStatusBar QComboBox {
    border: 1px solid transparent;
    background: transparent;
    border-radius: 4px;
}
QStatusBar QComboBox:hover { background: {statusComboHover}; }

/* ── Misc ────────────────────────────────────────────────────── */
QAbstractScrollArea { border: none; }
#HexView { border-top: 1px solid {border}; }
QToolTip {
    background: palette(tooltip-base);
    color: palette(tooltip-text);
    border: 1px solid {border};
    border-radius: 4px;
    padding: 4px 8px;
}

)";

    QString ss = QString::fromLatin1(TMPL);
    ss.replace("{fg}",               fg);
    ss.replace("{border}",           border);
    ss.replace("{btnBg}",            btnBg);
    ss.replace("{btnHover}",         btnHover);
    ss.replace("{btnActive}",        btnActive);
    ss.replace("{menuBg}",           menuBg);
    ss.replace("{inputBg}",          inputBg);
    ss.replace("{scrollHandle}",     scrollHandle);
    ss.replace("{scrollHover}",      scrollHover);
    ss.replace("{statusBg}",         statusBg);
    ss.replace("{statusComboHover}", statusComboHover);
    ss.replace("{accent}",           accent);
    ss.replace("{accentHover}",      accentHover);
    ss.replace("{accentFg}",         accentFg);
    ss.replace("{danger}",           danger);
    return ss;
}

// ── Public entry point ────────────────────────────────────────────────────────

void themeMenu(QMenu *menu)
{
    // A frameless, transparent window lets the QSS border-radius actually clip
    // the corners.  Qt::Popup is preserved so click-outside still closes the menu.
    menu->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu->setAttribute(Qt::WA_TranslucentBackground);
}

void applyAdwaitaTheme()
{
    // Fusion is always available — use it as the base style
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    // Qt 6.5+ colour scheme hint (reads from GNOME / portal settings)
    bool dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;

    applyPalette(dark);
    qApp->setStyleSheet(buildStylesheet(dark));

    // Prefer Cantarell (GNOME's UI font) if available, otherwise leave as-is.
    // Preserve the system point size — only switch the family.
    if (QFontDatabase::families().contains("Cantarell")) {
        QFont f = QApplication::font();
        f.setFamily("Cantarell");
        QApplication::setFont(f);
    }
}
