#include "focusnavigation.h"
#include "paletteswatch.h"

#include <algorithm>

#include <QAbstractScrollArea>
#include <QDialog>
#include <QPoint>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QWidget>

namespace FocusNavigation {

static bool isFocusableInScope(QWidget *scope, QWidget *candidate, QWidget *current)
{
    return candidate
        && candidate != current
        && candidate->focusPolicy() != Qt::NoFocus
        && !candidate->isHidden()
        && candidate->isVisibleTo(scope)
        && !candidate->rect().isEmpty();
}

static bool hasAncestorPaletteGrid(QWidget *widget)
{
    for (QWidget *parent = widget ? widget->parentWidget() : nullptr;
         parent; parent = parent->parentWidget()) {
        if (qobject_cast<PaletteSwatchGrid *>(parent))
            return true;
    }
    return false;
}

static bool isScrollAreaViewport(QWidget *widget)
{
    auto *parentScroll = qobject_cast<QAbstractScrollArea *>(widget ? widget->parentWidget() : nullptr);
    return parentScroll && parentScroll->viewport() == widget;
}

static bool shouldOwnFocusPolicy(QWidget *widget)
{
    return widget
        && !qobject_cast<QDialog *>(widget)
        && !qobject_cast<QAbstractScrollArea *>(widget)
        && !qobject_cast<QScrollBar *>(widget)
        && !isScrollAreaViewport(widget)
        && !widget->objectName().startsWith(QLatin1String("qt_"));
}

bool hasFocusableWidget(QWidget *scope, QWidget *current, Direction direction)
{
    if (!scope || !current)
        return true;

    const int currentTop = current->mapToGlobal(QPoint(0, 0)).y();
    const int currentBottom = current->mapToGlobal(QPoint(0, current->height())).y();
    const auto widgets = scope->findChildren<QWidget *>();
    for (QWidget *widget : widgets) {
        if (!isFocusableInScope(scope, widget, current))
            continue;

        if (direction == Direction::Up) {
            const int bottom = widget->mapToGlobal(QPoint(0, widget->height())).y();
            if (bottom <= currentTop)
                return true;
        } else {
            const int top = widget->mapToGlobal(QPoint(0, 0)).y();
            if (top >= currentBottom)
                return true;
        }
    }
    return false;
}

void assignTabOrder(QWidget *scope)
{
    if (!scope)
        return;

    struct TabBlock {
        QPoint pos;
        QList<QWidget *> widgets;
    };

    QList<TabBlock> blocks;
    const auto children = scope->findChildren<QWidget *>();
    for (QWidget *widget : children) {
        if (auto *grid = qobject_cast<PaletteSwatchGrid *>(widget)) {
            // Palette swatches are individual tab stops, but the grid must stay
            // contiguous in the global order.  Treat it as one visual block and
            // splice the grid's own row-major list into the sorted chain.
            if (!grid->isHidden() && grid->isVisibleTo(scope)) {
                const auto gridWidgets = grid->tabOrderWidgets();
                if (!gridWidgets.isEmpty())
                    blocks.append({grid->mapToGlobal(QPoint(0, 0)), gridWidgets});
            }
            continue;
        }
        if (hasAncestorPaletteGrid(widget))
            continue;
        if (!isFocusableInScope(scope, widget, nullptr))
            continue;
        // Ignore container/implementation widgets; only user-facing controls
        // should appear in the chain.  In particular, scroll-area viewports can
        // otherwise become invisible tab stops.
        if (qobject_cast<QDialog *>(widget)
                || qobject_cast<QAbstractScrollArea *>(widget)
                || qobject_cast<QScrollBar *>(widget)
                || isScrollAreaViewport(widget))
            continue;
        if (widget->window() != scope->window())
            continue;
        if (widget->objectName().startsWith(QLatin1String("qt_")))
            continue;
        blocks.append({widget->mapToGlobal(QPoint(0, 0)), {widget}});
    }

    std::sort(blocks.begin(), blocks.end(), [](const TabBlock &a, const TabBlock &b) {
        if (a.pos.y() != b.pos.y())
            return a.pos.y() < b.pos.y();
        return a.pos.x() < b.pos.x();
    });

    for (TabBlock &block : blocks) {
        block.widgets.erase(std::remove_if(block.widgets.begin(), block.widgets.end(), [](QWidget *widget) {
            return !widget
                || widget->focusPolicy() == Qt::NoFocus
                || widget->isHidden()
                || widget->rect().isEmpty();
        }), block.widgets.end());
    }
    blocks.erase(std::remove_if(blocks.begin(), blocks.end(), [](const TabBlock &block) {
        return block.widgets.isEmpty();
    }), blocks.end());

    QList<QWidget *> widgets;
    for (const TabBlock &block : blocks)
        widgets.append(block.widgets);
    QSet<QWidget *> assigned(widgets.cbegin(), widgets.cend());
    for (QWidget *widget : children) {
        if (!shouldOwnFocusPolicy(widget) || hasAncestorPaletteGrid(widget))
            continue;
        if (!assigned.contains(widget))
            widget->setFocusPolicy(Qt::NoFocus);
    }

    for (const TabBlock &block : blocks) {
        for (int i = 1; i < block.widgets.size(); ++i)
            QWidget::setTabOrder(block.widgets.at(i - 1), block.widgets.at(i));
    }
    for (int i = 1; i < blocks.size(); ++i)
        QWidget::setTabOrder(blocks.at(i - 1).widgets.last(), blocks.at(i).widgets.first());
}

void ensureFocusedWidgetVisible(QWidget *widget)
{
    if (!widget)
        return;

    for (QWidget *parent = widget->parentWidget(); parent; parent = parent->parentWidget()) {
        if (auto *scroll = qobject_cast<QScrollArea *>(parent)) {
            scroll->ensureWidgetVisible(widget);
            return;
        }
    }
}

} // namespace FocusNavigation
