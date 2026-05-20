#pragma once

#include "theme.h"

#include <functional>
#include <QList>
#include <QPointer>
#include <QWidget>
#include <QWidgetAction>

// ─── ThemePickerWidget ────────────────────────────────────────────────────────
// Three circles (Light / System / Dark) embedded as a QWidgetAction at the top
// of the Tools menu.  No Q_OBJECT needed — uses a std::function callback.

class ThemePickerWidget : public QWidget
{
public:
    static constexpr int VPAD     = 14;  // vertical padding above & below
    static constexpr int TARGET_D = 52;  // desired circle diameter
    static constexpr int GAP      = 12;  // fixed gap between circle edges

    ThemePickerWidget(ColorScheme current,
                      std::function<void(ColorScheme)> cb,
                      QWidget *parent = nullptr);

    QSize sizeHint() const override;
    void setCurrent(ColorScheme s);

    // Always derived from the actual rendered height so it stays in sync.
    int d() const { return height() - 2 * VPAD; }

protected:
    void paintEvent(QPaintEvent *) override;
    bool event(QEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;

private:
    QRect circleRect(int i) const;
    int   hitCircle(QPoint pos) const;

    ColorScheme                      m_current;
    int                              m_hovered = -1;
    std::function<void(ColorScheme)> m_cb;
};

// ─── ThemePickerAction ────────────────────────────────────────────────────────
// QWidgetAction subclass: overrides createWidget so every menu that receives
// this action gets its own properly-parented ThemePickerWidget instance.
// (setDefaultWidget only works for the *first* container added; subsequent
// containers get null from the default createWidget implementation.)

class ThemePickerAction : public QWidgetAction
{
public:
    ThemePickerAction(ColorScheme current,
                      std::function<void(ColorScheme)> cb,
                      QObject *parent = nullptr);

    void setCurrent(ColorScheme current);

protected:
    QWidget *createWidget(QWidget *parent) override;

private:
    ColorScheme                        m_current;
    std::function<void(ColorScheme)>   m_cb;
    QList<QPointer<ThemePickerWidget>> m_widgets;
};
