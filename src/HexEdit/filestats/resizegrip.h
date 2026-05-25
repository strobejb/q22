#ifndef FILESTATS_RESIZEGRIP_H
#define FILESTATS_RESIZEGRIP_H

#include <QWidget>

namespace filestats {

class SidePanelResizeGrip : public QWidget
{
public:
    explicit SidePanelResizeGrip(QWidget *parent = nullptr);

    void setActive(bool active);

protected:
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    bool m_hovered = false;
    bool m_active = false;
};

} // namespace filestats

#endif // FILESTATS_RESIZEGRIP_H
