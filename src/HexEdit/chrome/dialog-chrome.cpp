#include "dialog-chrome.h"

#include "settings/settings.h"
#include "shadow-chrome.h"
#include "titlebar.h"

#include <QApplication>
#include <QAbstractButton>
#include <QBoxLayout>
#include <QBitmap>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QChildEvent>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPainter>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <QResizeEvent>
#include <QScrollBar>
#include <QAbstractScrollArea>
#include <QTimer>
#include <QVector>
#include <QWindow>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#  define DWMWA_WINDOW_CORNER_PREFERENCE 33
#  define DWMWCP_ROUND 2
#endif
#ifndef DWMWA_BORDER_COLOR
#  define DWMWA_BORDER_COLOR 34
#endif
#endif

static constexpr int kDialogResizeMargin = 5;

#ifdef Q_OS_WIN
static constexpr int kDialogShadowSize = 22;
static constexpr int kDialogCornerRadius = 10;
static constexpr int kDialogShadowMaxAlpha = 64;
static constexpr int kDialogFrameHitMargin = 7;

static ShadowChromeParams windowsDialogShadowParams()
{
    const bool dark = QApplication::palette().window().color().lightness() < 128;
    return {
        kDialogShadowSize,
        kDialogCornerRadius,
        kDialogShadowMaxAlpha,
        2.0,
        QColor(0, 0, 0),
        dark ? QColor(0x4A, 0x4A, 0x4A) : QColor(0xCD, 0xC7, 0xC2),
    };
}

class DialogBorderOverlay : public QWidget
{
public:
    explicit DialogBorderOverlay(QDialog *dialog)
        : QWidget(dialog)
    {
        setObjectName(QStringLiteral("DialogBorderOverlay"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAutoFillBackground(false);
        setGeometry(dialog->rect());
        raise();
        dialog->installEventFilter(this);
        for (QObject *child : dialog->children()) {
            if (auto *w = qobject_cast<QWidget *>(child); w && w != this)
                w->installEventFilter(this);
        }
    }

    bool eventFilter(QObject *obj, QEvent *event) override
    {
        if (obj == parentWidget()) {
            if (event->type() == QEvent::Resize) {
                auto *resize = static_cast<QResizeEvent *>(event);
                setGeometry(QRect(QPoint(0, 0), resize->size()));
                raise();
                update();
            } else if (event->type() == QEvent::ChildAdded) {
                if (auto *w = qobject_cast<QWidget *>(static_cast<QChildEvent *>(event)->child());
                    w && w != this) {
                    w->installEventFilter(this);
                }
            }
        } else if (obj != this && event->type() == QEvent::Paint) {
            raise();
            update();
        }
        return false;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        auto *dialog = qobject_cast<QDialog *>(parentWidget());
        if (!dialog || dialog->isMaximized() || dialog->isFullScreen())
            return;

        const ShadowChromeParams params = windowsDialogShadowParams();
        if (!params.borderColor.isValid())
            return;

        const QRectF content = shadowContentRect(QRectF(rect()), params);
        if (!content.isValid())
            return;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPen pen(params.borderColor, 1);
        pen.setCosmetic(true);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(content.adjusted(0.5, 0.5, -0.5, -0.5),
                          params.cornerRadius - 0.5,
                          params.cornerRadius - 0.5);
    }
};

class DialogShadowFrame : public QWidget
{
public:
    explicit DialogShadowFrame(QDialog *dialog)
        : QWidget(dialog)
    {
        setObjectName(QStringLiteral("DialogShadowFrame"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAutoFillBackground(false);
        setGeometry(dialog->rect());
        lower();
        dialog->installEventFilter(this);
    }

    bool eventFilter(QObject *obj, QEvent *event) override
    {
        if (obj == parentWidget() && event->type() == QEvent::Resize) {
            auto *resize = static_cast<QResizeEvent *>(event);
            setGeometry(QRect(QPoint(0, 0), resize->size()));
            lower();
            update();
        }
        return false;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        auto *dialog = qobject_cast<QDialog *>(parentWidget());
        if (!dialog)
            return;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(rect(), Qt::transparent);

        paintShadowedRoundedWindow(p, dialog, rect(), windowsDialogShadowParams(), true);
    }
};

static void applyWindowsDialogChrome(QWidget *w)
{
    if (!w || !w->windowHandle())
        return;
    if (w->property("_qexedDialogShadowInstalled").toBool())
        return;

    HWND hwnd = reinterpret_cast<HWND>(w->winId());

    DWORD cornerPref = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                          &cornerPref, sizeof(cornerPref));

    const bool dark = w->palette().window().color().lightness() < 128;
    COLORREF borderColor = dark ? 0x004A4A4A : 0x00C2C7CD;
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR,
                          &borderColor, sizeof(borderColor));

    // The shadow for frameless dialogs is self-painted by DialogShadowFrame.
    // Do not extend a DWM frame here: it creates a second rounded border around
    // the outer shadow window instead of the dialog content.
}

static void applyRoundedDialogMask(QWidget *w)
{
    if (w && w->property("_qexedDialogShadowInstalled").toBool()) {
        // Self-shadowed frameless dialogs use WA_TranslucentBackground and a
        // painted shadow frame.  A native window region clips the soft shadow
        // and can leave a hard rounded outline around the outer shadow bounds.
        w->clearMask();
        return;
    }
    if (!w || w->isMaximized() || w->isFullScreen()) {
        if (w) w->clearMask();
        return;
    }

    QBitmap mask(w->size());
    mask.fill(Qt::color0);
    QPainter p(&mask);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(Qt::color1);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(w->rect(), kDialogCornerRadius, kDialogCornerRadius);
    w->setMask(mask);
}

static void addWindowsDialogShadow(QDialog *dlg)
{
    if (!dlg || dlg->property("_qexedDialogShadowInstalled").toBool())
        return;

    dlg->setAttribute(Qt::WA_TranslucentBackground, true);
    dlg->setAutoFillBackground(false);
    dlg->setProperty("_qexedDialogShadowLeft", kDialogShadowSize);
    dlg->setProperty("_qexedDialogShadowTop", kDialogShadowSize);
    dlg->setProperty("_qexedDialogShadowRight", kDialogShadowSize);
    dlg->setProperty("_qexedDialogShadowBottom", kDialogShadowSize);

    const QMargins m = dlg->contentsMargins();
    dlg->setContentsMargins(m.left() + kDialogShadowSize,
                            m.top() + kDialogShadowSize,
                            m.right() + kDialogShadowSize,
                            m.bottom() + kDialogShadowSize);

    const QSize shadowSize(kDialogShadowSize * 2, kDialogShadowSize * 2);
    if (!dlg->property("_qexedDialogShadowSizeApplied").toBool()) {
        if (!dlg->size().isEmpty())
            dlg->resize(dlg->size() + shadowSize);
        if (dlg->minimumWidth() > 0 || dlg->minimumHeight() > 0)
            dlg->setMinimumSize(dlg->minimumSize() + shadowSize);
        if (dlg->maximumWidth() < QWIDGETSIZE_MAX || dlg->maximumHeight() < QWIDGETSIZE_MAX)
            dlg->setMaximumSize(dlg->maximumSize() + shadowSize);
        dlg->setProperty("_qexedDialogShadowSizeApplied", true);
    }

    auto *frame = new DialogShadowFrame(dlg);
    frame->show();
    frame->lower();
    auto *clipper = new ShadowCornerClipper(dlg, windowsDialogShadowParams());
    clipper->setObjectName(QStringLiteral("DialogCornerClipper"));
    clipper->show();
    clipper->raise();
    auto *border = new DialogBorderOverlay(dlg);
    border->show();
    border->raise();
    dlg->setProperty("_qexedDialogShadowInstalled", true);
}
#endif

static QRect dialogResizeFrame(const QDialog *dialog)
{
#ifdef Q_OS_WIN
    const int left = dialog ? dialog->property("_qexedDialogShadowLeft").toInt() : 0;
    const int top = dialog ? dialog->property("_qexedDialogShadowTop").toInt() : 0;
    const int right = dialog ? dialog->property("_qexedDialogShadowRight").toInt() : 0;
    const int bottom = dialog ? dialog->property("_qexedDialogShadowBottom").toInt() : 0;
    if (left || top || right || bottom)
        return dialog->rect().adjusted(left, top, -right, -bottom);
#else
    Q_UNUSED(dialog);
#endif
    return dialog ? dialog->rect() : QRect();
}

static Qt::Edges dialogEdgesFromFramePos(const QPoint &pos, const QRect &frame, int margin)
{
    Qt::Edges edges;
    if (!frame.isValid())
        return edges;
    if (pos.x() >= frame.left() && pos.x() <= frame.left() + margin) edges |= Qt::LeftEdge;
    if (pos.x() <= frame.right() && pos.x() >= frame.right() - margin) edges |= Qt::RightEdge;
    if (pos.y() >= frame.top() && pos.y() <= frame.top() + margin) edges |= Qt::TopEdge;
    if (pos.y() <= frame.bottom() && pos.y() >= frame.bottom() - margin) edges |= Qt::BottomEdge;
    return edges;
}

static int dialogEffectiveResizeMargin(const QDialog *dialog)
{
#ifdef Q_OS_WIN
    if (dialog && dialog->property("_qexedDialogShadowLeft").toInt() > 0)
        return kDialogFrameHitMargin;
#else
    Q_UNUSED(dialog);
#endif
    return kDialogResizeMargin;
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

static bool dialogCanResizeHorizontally(const QDialog *dialog)
{
    if (!dialog)
        return false;
#ifdef Q_OS_WIN
    if (dialog->windowFlags() & Qt::MSWindowsFixedSizeDialogHint)
        return false;
#endif
    return dialog->minimumWidth() < dialog->maximumWidth();
}

static bool dialogCanResizeVertically(const QDialog *dialog)
{
    if (!dialog)
        return false;
#ifdef Q_OS_WIN
    if (dialog->windowFlags() & Qt::MSWindowsFixedSizeDialogHint)
        return false;
#endif
    return dialog->minimumHeight() < dialog->maximumHeight();
}

static Qt::Edges dialogResizableEdges(const QDialog *dialog, Qt::Edges edges)
{
    if (!dialogCanResizeHorizontally(dialog))
        edges &= ~(Qt::LeftEdge | Qt::RightEdge);
    if (!dialogCanResizeVertically(dialog))
        edges &= ~(Qt::TopEdge | Qt::BottomEdge);
    return edges;
}

class DialogChromeFilter : public QObject
{
public:
    DialogChromeFilter(QDialog *dialog, TitleBar *titleBar)
        : QObject(dialog), m_dialog(dialog), m_titleBar(titleBar)
    {
        installOnWidget(dialog);
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
#ifdef Q_OS_WIN
        case QEvent::PlatformSurface: {
            auto *surfaceEvent = static_cast<QPlatformSurfaceEvent *>(event);
            if (surfaceEvent->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated) {
                applyRoundedDialogMask(m_dialog);
                applyWindowsDialogChrome(m_dialog);
            }
            break;
        }
#endif
        case QEvent::ChildAdded:
            if (auto *w = qobject_cast<QWidget *>(static_cast<QChildEvent *>(event)->child()))
                installOnWidget(w);
            break;
        case QEvent::WindowActivate:
        case QEvent::WindowDeactivate:
        case QEvent::ActivationChange:
            refreshTitleBarLater();
            break;
        case QEvent::Show:
            refreshWindowShapeLater();
            break;
        case QEvent::Resize:
            refreshWindowShapeLater();
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
            if (isResizeExcludedWidget(widget))
                break;
            const int margin = dialogEffectiveResizeMargin(m_dialog);
            const QPoint pos = m_dialog->mapFromGlobal(me->globalPosition().toPoint());
            if (const Qt::Edges edges = dialogResizableEdges(
                    m_dialog, dialogEdgesFromFramePos(pos, dialogResizeFrame(m_dialog), margin));
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
    bool isResizeExcludedWidget(QWidget *widget) const
    {
        for (QWidget *w = widget; w && w != m_dialog; w = w->parentWidget()) {
            if (qobject_cast<QScrollBar *>(w) || qobject_cast<QAbstractScrollArea *>(w))
                return true;
        }
        return false;
    }

    void installOnWidget(QWidget *widget)
    {
        if (!widget || widget->window() != m_dialog)
            return;

        widget->setMouseTracking(true);
        widget->installEventFilter(this);
        const auto children = widget->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
        for (QWidget *child : children)
            installOnWidget(child);
    }

    void syncResizeCursor(const QPoint &globalPos)
    {
        if (!m_dialog || m_dialog->isMaximized() || m_dialog->isFullScreen()) {
            clearResizeCursor();
            return;
        }
        const int margin = dialogEffectiveResizeMargin(m_dialog);
        const QPoint pos = m_dialog->mapFromGlobal(globalPos);
        const Qt::Edges edges = dialogResizableEdges(
            m_dialog, dialogEdgesFromFramePos(pos, dialogResizeFrame(m_dialog), margin));
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

    void refreshWindowShapeLater()
    {
#ifdef Q_OS_WIN
        QPointer<QDialog> dialog = m_dialog;
        QTimer::singleShot(0, m_dialog, [dialog]() {
            if (!dialog)
                return;
            applyRoundedDialogMask(dialog);
            applyWindowsDialogChrome(dialog);
        });
#endif
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

    int minCol = 0, maxCol = 0;
    bool haveItems = false;
    for (const Item &it : items) {
        minCol = haveItems ? qMin(minCol, it.col) : it.col;
        maxCol = haveItems ? qMax(maxCol, it.col + qMax(1, it.colSpan) - 1)
                           : it.col + qMax(1, it.colSpan) - 1;
        haveItems = true;
    }

    const bool fullWidthTitleBar =
        qobject_cast<QFileDialog *>(dlg)
#ifdef Q_OS_WIN
        || qobject_cast<QMessageBox *>(dlg)
#endif
        ;

    if (fullWidthTitleBar) {
        const int padX = qMax(18, qMax(margins.left(), margins.right()));

        for (const Item &it : items) {
            grid->addItem(it.item, it.row + 2, it.col + 1,
                          it.rowSpan, it.colSpan, it.alignment);
        }

        grid->addWidget(titleBar, 0, 0, 1, qMax(1, maxCol + 3));
        grid->setColumnMinimumWidth(0, padX);
        grid->setColumnMinimumWidth(maxCol + 2, padX);
        grid->setRowMinimumHeight(1, qMax(14, margins.top()));

        for (auto *box : dlg->findChildren<QDialogButtonBox *>()) {
            for (QAbstractButton *button : box->buttons())
                button->setIcon(QIcon());
        }

        const int padBottom = qMax(16, margins.bottom());
        grid->setContentsMargins(0, 0, 0, padBottom);
        const QSize hint = dlg->sizeHint();
        dlg->setMinimumSize(qMax(dlg->minimumWidth(), hint.width() + 24),
                            qMax(dlg->minimumHeight(), hint.height() + 12));
    } else {
        for (const Item &it : items) {
            grid->addItem(it.item, it.row + 2, it.col,
                          it.rowSpan, it.colSpan, it.alignment);
        }

        grid->addWidget(titleBar, 0, minCol, 1, qMax(1, maxCol - minCol + 1));
        grid->setRowMinimumHeight(1, margins.top());
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
#ifdef Q_OS_WIN
    // Absolute-positioned dialogs do not participate in QWidget contents
    // margins.  Place their chrome and children inside the same content rect
    // that the Windows dialog shadow painter uses; otherwise the titlebar is
    // drawn in the transparent shadow margin.
    constexpr int frameInset = kDialogShadowSize;
#else
    constexpr int frameInset = 0;
#endif
    titleBar->setGeometry(frameInset, frameInset, oldSize.width(), titleBarHeight);

    for (QObject *child : dlg->children()) {
        auto *w = qobject_cast<QWidget *>(child);
        if (!w || w == titleBar || w->parentWidget() != dlg)
            continue;
        w->move(w->x() + frameInset, w->y() + frameInset + titleBarHeight);
    }

    dlg->resize(oldSize.width(), oldSize.height() + titleBarHeight);
    if (dlg->minimumHeight() > 0)
        dlg->setMinimumHeight(dlg->minimumHeight() + titleBarHeight);
    if (dlg->maximumHeight() < QWIDGETSIZE_MAX)
        dlg->setMaximumHeight(dlg->maximumHeight() + titleBarHeight);
    return true;
}
void installDialogChrome(QDialog *dlg)
{
    if (!dlg || dlg->property("_qexedDialogChromeInstalled").toBool())
        return;

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
#ifdef Q_OS_WIN
    opts.leftAlignTitle = true;
#endif

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
#ifdef Q_OS_WIN
    addWindowsDialogShadow(dlg);
#endif

    dlg->installEventFilter(new DialogChromeFilter(dlg, titleBar));
}
