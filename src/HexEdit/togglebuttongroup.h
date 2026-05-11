#ifndef TOGGLEBUTTONGROUP_H
#define TOGGLEBUTTONGROUP_H

#include <QIcon>
#include <QList>
#include <QWidget>

class QMouseEvent;
class QVariantAnimation;

class ToggleButtonGroup : public QWidget
{
    Q_OBJECT
public:
    explicit ToggleButtonGroup(const QList<QIcon> &icons = {}, QWidget *parent = nullptr);

    void addButton(const QIcon &icon);
    int mode() const { return m_mode; }
    void setMode(int mode);

    QSize sizeHint() const override;

signals:
    void modeChanged(int mode);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;

private:
    int slotAt(const QPoint &pos) const;
    int slotWidth() const { return kSlotW; }
    QIcon recolored(const QIcon &icon, const QColor &color) const;

    static constexpr int kPad    = 3;
    static constexpr int kSlotW  = 36;
    static constexpr int kSlotH  = 28;
    static constexpr int kIconSz = 16;
    static constexpr int kRadius = 7;
    static constexpr int kInnerR = 5;

    int   m_mode      = 0;
    int   m_hovered   = -1;
    int   m_pressed   = -1;
    qreal m_indicatorX;
    QList<QIcon> m_icons;
    QVariantAnimation *m_anim = nullptr;
};

#endif // TOGGLEBUTTONGROUP_H
