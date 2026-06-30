#ifndef DOCKPANELROW_H
#define DOCKPANELROW_H

#include <QWidget>

class QHBoxLayout;

class DockPanelRow : public QWidget
{
public:
    explicit DockPanelRow(QWidget *parent = nullptr);

    void adoptFrom(QHBoxLayout *source);
    void replaceWidget(QWidget *from, QWidget *to);
    void setControlAlignment(QWidget *widget);
    void setContentMaximumWidth(int width);
    int contentSpacing() const;
    static int inputHeight(const QWidget *reference);

    QHBoxLayout *boxLayout() const { return m_layout; }

private:
    void prepareControl(QWidget *widget, bool fixedHorizontal = false);

    QHBoxLayout *m_layout = nullptr;
    QWidget     *m_contentWidget = nullptr;
    QHBoxLayout *m_contentLayout = nullptr;
};

#endif // DOCKPANELROW_H
