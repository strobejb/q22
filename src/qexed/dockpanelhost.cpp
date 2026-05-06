#include "dockpanelhost.h"

#include <QApplication>
#include <QChildEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>

DockPanelHost::DockPanelHost(QWidget *escapeFocusWidget, QWidget *parent)
    : QWidget(parent)
    , m_escapeFocusWidget(escapeFocusWidget)
{
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
}

void DockPanelHost::addPanel(QWidget *panel)
{
    if (!panel)
        return;

    panel->setParent(this);
    m_layout->addWidget(panel);
    installPanelFilters(panel);
}

bool DockPanelHost::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::ChildAdded) {
        auto *childEvent = static_cast<QChildEvent *>(event);
        if (childEvent->child() && childEvent->child()->isWidgetType()) {
            installPanelFilters(static_cast<QWidget *>(childEvent->child()));
        }
    }

    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        const bool tab = key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab;
        if (tab) {
            const bool backward = key->key() == Qt::Key_Backtab
                || (key->key() == Qt::Key_Tab
                    && key->modifiers().testFlag(Qt::ShiftModifier));
            const bool controlDown = key->modifiers().testFlag(Qt::ControlModifier);
            if (handleTab(!backward, controlDown)) {
                key->accept();
                return true;
            }
        }
    }

    if (event->type() == QEvent::Hide) {
        auto *widget = qobject_cast<QWidget *>(obj);
        if (widget && widget->parentWidget() == this)
            QTimer::singleShot(0, this, &DockPanelHost::focusPreferredVisiblePanel);
    }

    return QWidget::eventFilter(obj, event);
}

bool DockPanelHost::focusNextPrevChild(bool next)
{
    return handleTab(next, QApplication::keyboardModifiers().testFlag(Qt::ControlModifier));
}

bool DockPanelHost::handleTab(bool forward, bool controlDown)
{
    if (controlDown) {
        if (m_escapeFocusWidget) {
            m_escapeFocusWidget->setFocus(Qt::TabFocusReason);
            return true;
        }
        return false;
    }

    const QList<QWidget *> widgets = focusablePanelWidgets();
    if (widgets.isEmpty())
        return false;

    QWidget *current = QApplication::focusWidget();
    int idx = widgets.indexOf(current);
    idx = idx < 0 ? (forward ? -1 : 0) : idx;
    const int nextIdx = forward
        ? (idx + 1) % widgets.size()
        : (idx - 1 + widgets.size()) % widgets.size();
    widgets.at(nextIdx)->setFocus(forward ? Qt::TabFocusReason : Qt::BacktabFocusReason);
    return true;
}

QList<QWidget *> DockPanelHost::focusablePanelWidgets() const
{
    QList<QWidget *> widgets;
    for (QWidget *w = const_cast<DockPanelHost *>(this)->nextInFocusChain();
         w && w != this; w = w->nextInFocusChain()) {
        if (!isAncestorOf(w)
                || w->focusPolicy() == Qt::NoFocus
                || w->isHidden()
                || !w->isVisibleTo(const_cast<DockPanelHost *>(this))
                || w->rect().isEmpty())
            continue;
        widgets.append(w);
    }
    return widgets;
}

void DockPanelHost::focusPreferredVisiblePanel()
{
    for (int i = 0; i < m_layout->count(); ++i) {
        QWidget *panel = m_layout->itemAt(i)->widget();
        if (!panel || panel->isHidden() || !panel->isVisibleTo(this))
            continue;

        const auto edits = panel->findChildren<QLineEdit *>();
        for (QLineEdit *edit : edits) {
            if (edit->focusPolicy() != Qt::NoFocus
                    && !edit->isHidden()
                    && edit->isVisibleTo(panel)
                    && !edit->rect().isEmpty()) {
                edit->setFocus(Qt::TabFocusReason);
                edit->selectAll();
                return;
            }
        }
    }

    const QList<QWidget *> widgets = focusablePanelWidgets();
    if (!widgets.isEmpty())
        widgets.first()->setFocus(Qt::TabFocusReason);
}

void DockPanelHost::installPanelFilters(QWidget *panel)
{
    if (!panel || panel == this)
        return;

    panel->installEventFilter(this);
    const auto children = panel->findChildren<QWidget *>();
    for (QWidget *child : children)
        child->installEventFilter(this);
}
