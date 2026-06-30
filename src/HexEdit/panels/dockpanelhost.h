#ifndef DOCKPANELHOST_H
#define DOCKPANELHOST_H

#include <QList>
#include <QWidget>

class QVBoxLayout;
class HexView;

class DockPanelHost : public QWidget
{
public:
    explicit DockPanelHost(HexView *hexView, QWidget *parent = nullptr);

    void addPanel(QWidget *panel);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    bool focusNextPrevChild(bool next) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    bool handleTab(bool forward, bool controlDown);
    QList<QWidget *> focusablePanelWidgets() const;
    void focusPreferredVisiblePanel();
    void installPanelFilters(QWidget *panel);
    void updateRowWidthCaps();

    QVBoxLayout *m_layout = nullptr;
    HexView *m_hexView = nullptr;
};

#endif // DOCKPANELHOST_H
