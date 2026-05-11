#include "dialog-chrome.h"

#include "settings.h"
#include "titlebar.h"

#include <QApplication>
#include <QAbstractButton>
#include <QBoxLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QPointer>
#include <QTimer>
#include <QVector>
#include <QWindow>

#ifndef Q_OS_WIN
#include "linux-chrome.h"

#include <QChildEvent>
#include <QEnterEvent>
#include <QMouseEvent>

static Qt::Edges dialogEdgesFromPos(const QPoint &pos, const QRect &rect, int margin)
{
    Qt::Edges edges;
    if (pos.x() <= margin)                 edges |= Qt::LeftEdge;
    if (pos.x() >= rect.width() - margin)  edges |= Qt::RightEdge;
    if (pos.y() <= margin)                 edges |= Qt::TopEdge;
    if (pos.y() >= rect.height() - margin) edges |= Qt::BottomEdge;
    return edges;
}

static QCursor dialogCursorForEdges(Qt::Edges edges)
{
    const bool l = edges & Qt::LeftEdge;
    const bool r = edges & Qt::RightEdge;
    const bool t = edges & Qt::TopEdge;
    const bool b = edges & Qt::BottomEdge;
    if ((l && t) || (r && b)) return Qt::SizeFDiagCursor;
    if ((r && t) || (l && b)) return Qt::SizeBDiagCursor;
    if (l || r) return Qt::SizeHorCursor;
    if (t || b) return Qt::SizeVerCursor;
    return Qt::ArrowCursor;
}

class DialogChromeFilter : public QObject
{
public:
    DialogChromeFilter(QDialog *dialog, TitleBar *titleBar)
        : QObject(dialog), m_dialog(dialog), m_titleBar(titleBar)
    {
        for (QObject *child : dialog->children()) {
            if (auto *w = qobject_cast<QWidget *>(child))
                w->installEventFilter(this);
        }
    }

    ~DialogChromeFilter() override
    {
        if (m_inResizeZone)
            QApplication::restoreOverrideCursor();
    }

    bool eventFilter(QObject *obj, QEvent *event) override
    {
        auto *widget = qobject_cast<QWidget *>(obj);
        if (!widget || widget->window() != m_dialog)
            return false;

        switch (event->type()) {
        case QEvent::ChildAdded:
            if (obj == m_dialog) {
                if (auto *w = qobject_cast<QWidget *>(static_cast<QChildEvent *>(event)->child()))
                    w->installEventFilter(this);
            }
            break;
        case QEvent::WindowActivate:
        case QEvent::WindowDeactivate:
        case QEvent::ActivationChange:
            refreshTitleBarLater();
            break;
        case QEvent::MouseMove: {
            auto *me = static_cast<QMouseEvent *>(event);
            syncResizeCursor(me->globalPosition().toPoint());
            break;
        }
        case QEvent::Enter: {
            auto *ee = static_cast<QEnterEvent *>(event);
            syncResizeCursor(ee->globalPosition().toPoint());
            break;
        }
        case QEvent::Leave:
            clearResizeCursor();
            break;
        case QEvent::MouseButtonPress: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() != Qt::LeftButton || m_dialog->isMaximized())
                break;
            const int margin = kResizeMargin;
            const QPoint pos = m_dialog->mapFromGlobal(me->globalPosition().toPoint());
            if (const Qt::Edges edges = dialogEdgesFromPos(pos, m_dialog->rect(), margin);
                edges && m_dialog->windowHandle()) {
                m_dialog->windowHandle()->startSystemResize(edges);
                return true;
            }
            break;
        }
        default:
            break;
        }

        return false;
    }

private:
    void syncResizeCursor(const QPoint &globalPos)
    {
        if (!m_dialog || m_dialog->isMaximized() || m_dialog->isFullScreen()) {
            clearResizeCursor();
            return;
        }
        const int margin = kResizeMargin;
        const QPoint pos = m_dialog->mapFromGlobal(globalPos);
        const Qt::Edges edges = dialogEdgesFromPos(pos, m_dialog->rect(), margin);
        if (edges) {
            if (!m_inResizeZone) {
                m_inResizeZone = true;
                QApplication::setOverrideCursor(dialogCursorForEdges(edges));
            } else {
                QApplication::changeOverrideCursor(dialogCursorForEdges(edges));
            }
        } else {
            clearResizeCursor();
        }
    }

    void clearResizeCursor()
    {
        if (!m_inResizeZone)
            return;
        m_inResizeZone = false;
        QApplication::restoreOverrideCursor();
    }

    void refreshTitleBarLater()
    {
        QPointer<TitleBar> titleBar = m_titleBar;
        QTimer::singleShot(0, m_dialog, [titleBar]() {
            if (titleBar)
                titleBar->refreshStylesheet();
        });
    }

    QDialog *m_dialog = nullptr;
    TitleBar *m_titleBar = nullptr;
    bool m_inResizeZone = false;
};

static void expandFixedHeightForTitleBar(QDialog *dlg, int titleBarHeight)
{
    if (!dlg || titleBarHeight <= 0)
        return;

    const bool fixedHeight = dlg->minimumHeight() == dlg->maximumHeight();
    if (fixedHeight)
        dlg->setFixedHeight(dlg->height() + titleBarHeight);
}

static void reparentLayoutItemWidgets(QLayoutItem *item, QWidget *parent)
{
    if (!item || !parent)
        return;
    if (QWidget *w = item->widget()) {
        w->setParent(parent);
        return;
    }
    if (QLayout *layout = item->layout()) {
        for (int i = 0; i < layout->count(); ++i)
            reparentLayoutItemWidgets(layout->itemAt(i), parent);
    }
}

static bool insertDialogChromeIntoGrid(QDialog *dlg, QGridLayout *grid, TitleBar *titleBar)
{
    struct Item {
        QLayoutItem *item = nullptr;
        int row = 0;
        int col = 0;
        int rowSpan = 1;
        int colSpan = 1;
        Qt::Alignment alignment = {};
    };

    const QMargins margins = grid->contentsMargins();

    QVector<Item> items;
    items.reserve(grid->count());
    for (int i = 0; i < grid->count(); ++i) {
        int row = 0, col = 0, rowSpan = 1, colSpan = 1;
        grid->getItemPosition(i, &row, &col, &rowSpan, &colSpan);
        items.append({grid->itemAt(i), row, col, rowSpan, colSpan,
                      grid->itemAt(i)->alignment()});
    }
    while (grid->takeAt(0)) {}

    for (const Item &it : items) {
        grid->addItem(it.item, it.row + 2, it.col, it.rowSpan, it.colSpan, it.alignment);
    }

    int minCol = 0, maxCol = 0;
    bool haveItems = false;
    for (const Item &it : items) {
        minCol = haveItems ? qMin(minCol, it.col) : it.col;
        maxCol = haveItems ? qMax(maxCol, it.col + qMax(1, it.colSpan) - 1)
                           : it.col + qMax(1, it.colSpan) - 1;
        haveItems = true;
    }
    grid->addWidget(titleBar, 0, minCol, 1, qMax(1, maxCol - minCol + 1));
    grid->setRowMinimumHeight(1, margins.top());
    if (qobject_cast<QFileDialog *>(dlg)) {
        for (auto *box : dlg->findChildren<QDialogButtonBox *>()) {
            for (QAbstractButton *button : box->buttons())
                button->setIcon(QIcon());
        }

        const int padX = qMax(18, qMax(margins.left(), margins.right()));
        const int padBottom = qMax(16, margins.bottom());
        grid->setContentsMargins(padX, 0, padX, padBottom);
        grid->setRowMinimumHeight(1, qMax(14, margins.top()));
        const QSize hint = dlg->sizeHint();
        dlg->setMinimumSize(qMax(dlg->minimumWidth(), hint.width() + 24),
                            qMax(dlg->minimumHeight(), hint.height() + 12));
    } else {
        grid->setContentsMargins(margins.left(), 0, margins.right(), margins.bottom());
    }
    expandFixedHeightForTitleBar(dlg, titleBar->height());
    return true;
}

static bool insertDialogChromeIntoBox(QDialog *dlg, QBoxLayout *box, TitleBar *titleBar)
{
    const auto direction = box->direction();
    if (direction != QBoxLayout::TopToBottom && direction != QBoxLayout::BottomToTop)
        return false;

    struct Item {
        QLayoutItem *item = nullptr;
        int stretch = 0;
        Qt::Alignment alignment = {};
    };

    QVector<Item> items;
    items.reserve(box->count());
    for (int i = 0; i < box->count(); ++i)
        items.append({box->itemAt(i), box->stretch(i), box->itemAt(i)->alignment()});
    while (box->takeAt(0)) {}

    const QMargins margins = box->contentsMargins();
    const int spacing = box->spacing();
    auto *content = new QWidget(dlg);
    auto *contentBox = new QVBoxLayout(content);
    contentBox->setContentsMargins(margins);
    contentBox->setSpacing(spacing);
    for (const Item &it : items) {
        reparentLayoutItemWidgets(it.item, content);
        contentBox->addItem(it.item);
        contentBox->setStretch(contentBox->count() - 1, it.stretch);
        if (it.alignment) {
            if (QWidget *w = it.item->widget())
                contentBox->setAlignment(w, it.alignment);
            else if (QLayout *layout = it.item->layout())
                contentBox->setAlignment(layout, it.alignment);
        }
    }

    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(0);
    if (direction == QBoxLayout::TopToBottom)
        box->insertWidget(0, titleBar);
    else
        box->addWidget(titleBar);
    box->addWidget(content, 1);
    expandFixedHeightForTitleBar(dlg, titleBar->height());
    return true;
}

static bool insertDialogChromeIntoAbsoluteDialog(QDialog *dlg, TitleBar *titleBar)
{
    const int titleBarHeight = titleBar->height();
    if (titleBarHeight <= 0)
        return false;

    const QSize oldSize = dlg->size();
    titleBar->setParent(dlg);
    titleBar->setGeometry(0, 0, oldSize.width(), titleBarHeight);

    for (QObject *child : dlg->children()) {
        auto *w = qobject_cast<QWidget *>(child);
        if (!w || w == titleBar || w->parentWidget() != dlg)
            continue;
        w->move(w->x(), w->y() + titleBarHeight);
    }

    dlg->resize(oldSize.width(), oldSize.height() + titleBarHeight);
    if (dlg->minimumHeight() > 0)
        dlg->setMinimumHeight(dlg->minimumHeight() + titleBarHeight);
    if (dlg->maximumHeight() < QWIDGETSIZE_MAX)
        dlg->setMaximumHeight(dlg->maximumHeight() + titleBarHeight);
    return true;
}
#endif

void installDialogChrome(QDialog *dlg)
{
    if (!dlg || dlg->property("_qexedDialogChromeInstalled").toBool())
        return;

#ifndef Q_OS_WIN
    if (AppSettings::prefNativeDialogs())
        return;

    TitleBarOptions opts;
    opts.showFileMenu = false;
    opts.showSearchMenu = false;
    opts.showViewMenu = false;
    opts.showMinimize = false;
    opts.showMaximize = false;
    opts.allowMaximizeOnDoubleClick = false;
    opts.compact = true;

    auto *titleBar = new TitleBar(dlg, opts);
    bool inserted = false;
    if (QLayout *layout = dlg->layout()) {
        if (auto *grid = qobject_cast<QGridLayout *>(layout))
            inserted = insertDialogChromeIntoGrid(dlg, grid, titleBar);
        else if (auto *box = qobject_cast<QBoxLayout *>(layout))
            inserted = insertDialogChromeIntoBox(dlg, box, titleBar);
    } else {
        inserted = insertDialogChromeIntoAbsoluteDialog(dlg, titleBar);
    }

    if (!inserted) {
        titleBar->deleteLater();
        return;
    }

    dlg->setProperty("_qexedDialogChromeInstalled", true);
    dlg->setProperty("_qexedDialogChromeHeight", titleBar->height());
    dlg->setWindowFlag(Qt::FramelessWindowHint, true);

    dlg->installEventFilter(new DialogChromeFilter(dlg, titleBar));
#else
    Q_UNUSED(dlg);
#endif
}
