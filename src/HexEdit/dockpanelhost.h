#ifndef DOCKPANELHOST_H
#define DOCKPANELHOST_H

#include <QList>
#include <QWidget>

class QVBoxLayout;

class DockPanelHost : public QWidget
{
public:
    explicit DockPanelHost(QWidget *escapeFocusWidget, QWidget *parent = nullptr);

    void addPanel(QWidget *panel);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    bool focusNextPrevChild(bool next) override;

private:
    bool handleTab(bool forward, bool controlDown);
    QList<QWidget *> focusablePanelWidgets() const;
    void focusPreferredVisiblePanel();
    void installPanelFilters(QWidget *panel);

    QVBoxLayout *m_layout = nullptr;
    QWidget *m_escapeFocusWidget = nullptr;
};

#endif // DOCKPANELHOST_H
