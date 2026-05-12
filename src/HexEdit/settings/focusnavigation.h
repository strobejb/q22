#ifndef FOCUSNAVIGATION_H
#define FOCUSNAVIGATION_H

class QWidget;

namespace FocusNavigation {

enum class Direction {
    Up,
    Down,
};

bool hasFocusableWidget(QWidget *scope, QWidget *current, Direction direction);

// Rebuild Qt's tab chain from the visible layout positions under scope.
// This is called after dynamic dialogs/overlays have been laid out so Tab can
// use Qt's normal focus traversal instead of widget-specific edge handling.
// Qt's chain is already circular; callers must not add an explicit last->first
// link because setTabOrder() mutates chain order rather than only closing it.
void assignTabOrder(QWidget *scope);
void ensureFocusedWidgetVisible(QWidget *widget);

} // namespace FocusNavigation

#endif // FOCUSNAVIGATION_H
