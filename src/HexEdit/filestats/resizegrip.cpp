#include "filestats/resizegrip.h"

#include "theme.h"

#include <QEnterEvent>
#include <QEvent>
#include <QPainter>
#include <QPalette>

namespace filestats {
namespace {

static constexpr int kGripWidth = 7;

} // namespace

SidePanelResizeGrip::SidePanelResizeGrip(QWidget *parent)
    : QWidget(parent)
{
    setCursor(Qt::SizeHorCursor);
    setAcceptDrops(true);
    setProperty("fileInfoResizeHandle", true);
    setFixedWidth(kGripWidth);
}

void SidePanelResizeGrip::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    update();
}

void SidePanelResizeGrip::enterEvent(QEnterEvent *event)
{
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void SidePanelResizeGrip::leaveEvent(QEvent *event)
{
    m_hovered = false;
    update();
    QWidget::leaveEvent(event);
}

void SidePanelResizeGrip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    if (m_hovered || m_active) {
        QColor col = m_active ? palette().color(QPalette::Dark)
                              : palette().color(QPalette::Mid);
        col.setAlphaF(m_active ? 0.48 : 0.30);
        p.fillRect(rect(), col);
    }

    p.fillRect(QRect(0, 0, 1, height()), themeBorderColor());
}

} // namespace filestats
