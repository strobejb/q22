#include "dockpanelrow.h"

#include <QHBoxLayout>
#include <QLayoutItem>
#include <QSizePolicy>
#include <QtMath>

DockPanelRow::DockPanelRow(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(6, 4, 6, 4);
    m_layout->setSpacing(6);
}

void DockPanelRow::adoptFrom(QHBoxLayout *source)
{
    if (!source)
        return;

    while (source->count() > 0) {
        const int stretch = source->stretch(0);
        QLayoutItem *item = source->takeAt(0);
        if (!item)
            continue;

        if (QWidget *widget = item->widget()) {
            const bool fixedHorizontal = widget->sizePolicy().horizontalPolicy() == QSizePolicy::Fixed
                                      || widget->maximumWidth() < QWIDGETSIZE_MAX;
            widget->setParent(this);
            prepareControl(widget, fixedHorizontal);
            m_layout->addWidget(widget, stretch, Qt::AlignVCenter);
            delete item;
        } else {
            m_layout->addItem(item);
            if (stretch > 0)
                m_layout->setStretch(m_layout->count() - 1, stretch);
        }
    }
}

void DockPanelRow::replaceWidget(QWidget *from, QWidget *to)
{
    if (!from || !to)
        return;

    const int index = m_layout->indexOf(from);
    if (index < 0)
        return;

    const int stretch = m_layout->stretch(index);
    QLayoutItem *oldItem = m_layout->takeAt(index);
    delete oldItem;

    to->setParent(this);
    prepareControl(to, true);
    m_layout->insertWidget(index, to, stretch, Qt::AlignVCenter);
}

void DockPanelRow::setControlAlignment(QWidget *widget)
{
    if (!widget)
        return;

    prepareControl(widget);
    m_layout->setAlignment(widget, Qt::AlignVCenter);
}


const int DEFAULT_PANEL_INPUT_HEIGHT = 36;
int DockPanelRow::inputHeight(const QWidget *reference)
{
    if (!reference)
        return DEFAULT_PANEL_INPUT_HEIGHT;

    return qMax(DEFAULT_PANEL_INPUT_HEIGHT, reference->fontMetrics().height() + 14);
}

void DockPanelRow::prepareControl(QWidget *widget, bool fixedHorizontal)
{
    if (!widget)
        return;

    QSizePolicy policy = widget->sizePolicy();
    policy.setVerticalPolicy(QSizePolicy::Fixed);
    if (fixedHorizontal)
        policy.setHorizontalPolicy(QSizePolicy::Fixed);
    widget->setSizePolicy(policy);
}
