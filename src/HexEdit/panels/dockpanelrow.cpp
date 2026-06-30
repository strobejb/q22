#include "dockpanelrow.h"

#include <QHBoxLayout>
#include <QLayoutItem>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QtMath>

namespace {

bool isTrailingCloseSpacer(QHBoxLayout *layout)
{
    if (!layout || layout->count() < 2)
        return false;

    QLayoutItem *first = layout->itemAt(0);
    QSpacerItem *spacer = first ? first->spacerItem() : nullptr;
    if (!spacer || spacer->sizePolicy().horizontalPolicy() == QSizePolicy::Fixed)
        return false;

    for (int i = 1; i < layout->count(); ++i) {
        QLayoutItem *item = layout->itemAt(i);
        if (!item)
            continue;
        if (item->spacerItem())
            continue;
        QWidget *widget = item->widget();
        return widget && widget->objectName() == QStringLiteral("btnClose");
    }

    return false;
}

} // namespace

DockPanelRow::DockPanelRow(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(6, 4, 6, 4);
    m_layout->setSpacing(6);

    m_contentWidget = new QWidget(this);
    m_contentWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_contentLayout = new QHBoxLayout(m_contentWidget);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(6);
    m_layout->addWidget(m_contentWidget, 1);
}

void DockPanelRow::adoptFrom(QHBoxLayout *source)
{
    if (!source)
        return;

    bool trailingStretchAdded = false;
    while (source->count() > 0) {
        const int stretch = source->stretch(0);
        const bool trailingCloseSpacer = isTrailingCloseSpacer(source);
        QLayoutItem *item = source->takeAt(0);
        if (!item)
            continue;

        if (QWidget *widget = item->widget()) {
            const bool fixedHorizontal = widget->sizePolicy().horizontalPolicy() == QSizePolicy::Fixed
                                      || widget->maximumWidth() < QWIDGETSIZE_MAX;
            widget->setParent(this);
            prepareControl(widget, fixedHorizontal);
            if (widget->objectName() == QStringLiteral("btnClose")) {
                if (!trailingStretchAdded) {
                    m_layout->addStretch();
                    trailingStretchAdded = true;
                }
                m_layout->addWidget(widget, 0, Qt::AlignVCenter);
            } else {
                m_contentLayout->addWidget(widget, stretch, Qt::AlignVCenter);
            }
            delete item;
        } else {
            if (trailingCloseSpacer) {
                if (!trailingStretchAdded) {
                    m_layout->addStretch();
                    trailingStretchAdded = true;
                }
                delete item;
            } else {
                m_contentLayout->addItem(item);
                if (stretch > 0)
                    m_contentLayout->setStretch(m_contentLayout->count() - 1, stretch);
            }
        }
    }
}

void DockPanelRow::replaceWidget(QWidget *from, QWidget *to)
{
    if (!from || !to)
        return;

    QHBoxLayout *layout = m_contentLayout;
    int index = layout->indexOf(from);
    if (index < 0) {
        layout = m_layout;
        index = layout->indexOf(from);
    }
    if (index < 0)
        return;

    const int stretch = layout->stretch(index);
    QLayoutItem *oldItem = layout->takeAt(index);
    delete oldItem;

    to->setParent(this);
    prepareControl(to, true);
    layout->insertWidget(index, to, stretch, Qt::AlignVCenter);
}

void DockPanelRow::setControlAlignment(QWidget *widget)
{
    if (!widget)
        return;

    prepareControl(widget);
    if (m_contentLayout->indexOf(widget) >= 0)
        m_contentLayout->setAlignment(widget, Qt::AlignVCenter);
    else
        m_layout->setAlignment(widget, Qt::AlignVCenter);
}

void DockPanelRow::setContentMaximumWidth(int width)
{
    if (!m_contentWidget)
        return;

    if (width > 0)
        m_contentWidget->setMaximumWidth(qMax(m_contentWidget->minimumSizeHint().width(), width));
    else
        m_contentWidget->setMaximumWidth(QWIDGETSIZE_MAX);
}

int DockPanelRow::contentSpacing() const
{
    return m_contentLayout ? m_contentLayout->spacing() : 0;
}

const int DEFAULT_PANEL_INPUT_HEIGHT = 36;
int DockPanelRow::inputHeight(const QWidget *reference)
{
    int minHeight = DEFAULT_PANEL_INPUT_HEIGHT;
#ifndef Q_OS_WIN
    // GNOME/Adwaita gives the status bar controls a slightly taller native
    // metric than Windows. Keep docked panel controls visually in scale.
    minHeight += 6;
#endif
    if (!reference)
        return minHeight;

    return qMax(minHeight, reference->fontMetrics().height() + 14);
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
