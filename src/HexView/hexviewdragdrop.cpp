#include "hexview.h"

#include <QApplication>
#include <QByteArray>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QMimeData>
#include <algorithm>
#include <climits>
#include <cstring>

namespace {

constexpr char kMimeSnapshot[]  = "application/x-hexview-snapshot";
constexpr char kMimeSource[]    = "application/x-hexview-source";
constexpr char kMimeSelection[] = "application/x-hexview-selection";
constexpr char kMimeOctet[]     = "application/octet-stream";

template <typename T>
QByteArray packValue(T value)
{
    QByteArray bytes(static_cast<int>(sizeof(T)), Qt::Uninitialized);
    std::memcpy(bytes.data(), &value, sizeof(T));
    return bytes;
}

template <typename T>
bool unpackValue(const QMimeData *mime, const char *format, T *value)
{
    if (!mime || !mime->hasFormat(QLatin1String(format)))
        return false;

    const QByteArray bytes = mime->data(QLatin1String(format));
    if (bytes.size() != static_cast<int>(sizeof(T)))
        return false;

    std::memcpy(value, bytes.constData(), sizeof(T));
    return true;
}

struct DragSelection {
    size_w start = 0;
    size_w length = 0;
};

Qt::DropActions internalDragActions(uint editMode)
{
    return editMode == HVMODE_INSERT
        ? (Qt::CopyAction | Qt::MoveAction)
        : Qt::CopyAction;
}

Qt::DropAction defaultInternalDragAction(uint editMode)
{
    return editMode == HVMODE_INSERT ? Qt::MoveAction : Qt::CopyAction;
}

Qt::DropAction dropActionForModifiers(Qt::DropActions actions,
                                      Qt::DropAction defaultAction,
                                      Qt::KeyboardModifiers modifiers)
{
    if ((modifiers & Qt::ControlModifier) && (actions & Qt::CopyAction))
        return Qt::CopyAction;

    if (actions & defaultAction)
        return defaultAction;

    if (actions & Qt::MoveAction)
        return Qt::MoveAction;

    if (actions & Qt::CopyAction)
        return Qt::CopyAction;

    return Qt::IgnoreAction;
}

Qt::KeyboardModifiers dragModifiers(const QDropEvent *event)
{
    Qt::KeyboardModifiers modifiers = event ? event->modifiers() : Qt::NoModifier;
#ifdef Q_OS_WIN
    modifiers |= QApplication::queryKeyboardModifiers();
#endif
    return modifiers;
}

Qt::DropAction internalDragAction(uint editMode, const QDropEvent *event = nullptr)
{
    return dropActionForModifiers(internalDragActions(editMode),
                                  defaultInternalDragAction(editMode),
                                  dragModifiers(event));
}

bool isInDropAutoScrollBand(const QRect &rect, const QPoint &pos)
{
    constexpr int kEdgeBand = 24;

    return pos.x() <= rect.left() + kEdgeBand ||
           pos.x() >= rect.right() - kEdgeBand ||
           pos.y() <= rect.top() + kEdgeBand ||
           pos.y() >= rect.bottom() - kEdgeBand;
}

} // namespace

HexSnapshot *HexView::createSnapshot(size_w start, size_w len) const
{
    if (!m_pDataSeq || len == 0)
        return nullptr;

    size_t desclen = 0;
    if (!m_pDataSeq->takesnapshot(start, len, nullptr, &desclen))
        return nullptr;

    auto *hss = new HexSnapshot();
    hss->m_desclist = new sequence::span_desc[desclen + 1];
    hss->m_count    = desclen;
    hss->m_length   = len;
    hss->m_source   = const_cast<HexView *>(this);

    if (!m_pDataSeq->takesnapshot(start, len, hss->m_desclist, &desclen)) {
        delete hss;
        return nullptr;
    }

    return hss;
}

bool HexView::startDrag()
{
    if (!m_pDataSeq || selectionSize() == 0)
        return false;

    const size_w start = selectionStart();
    const size_w len   = selectionSize();

    delete m_dragSnapshot;
    m_dragSnapshot = createSnapshot(start, len);
    m_dragOverlayLength = len;
    m_dragOverlayPos = m_dragStartPos;
    m_dragOverlayVisible = true;
    viewport()->update();

    auto *mime = new QMimeData();

    if (m_dragSnapshot) {
        HexSnapshot *snapshot = m_dragSnapshot;
        mime->setData(QLatin1String(kMimeSnapshot), packValue(snapshot));
    }

    HexView *source = this;
    mime->setData(QLatin1String(kMimeSource), packValue(source));
    mime->setData(QLatin1String(kMimeSelection), packValue(DragSelection{start, len}));

    if (len <= static_cast<size_w>(INT_MAX)) {
        QByteArray bytes(static_cast<int>(len), Qt::Uninitialized);
        m_pDataSeq->render(start, reinterpret_cast<uint8_t *>(bytes.data()), len);
        mime->setData(QLatin1String(kMimeOctet), bytes);
        mime->setText(QString::fromLatin1(bytes));
    }

    auto *drag = new QDrag(this);
    drag->setMimeData(mime);

    const Qt::DropActions actions = internalDragActions(m_nEditMode);
    const Qt::DropAction defaultAction = defaultInternalDragAction(m_nEditMode);
#ifdef Q_OS_WIN
    m_dragOverlayAction = internalDragAction(m_nEditMode);
#else
    m_dragOverlayAction = defaultAction;
#endif
    m_internalDragActive = true;
    // Wayland/Qt can report stale target-side proposed actions during an
    // internal drag, causing Ctrl-copy feedback to flicker back to Move while
    // the mouse moves.  Treat the source QDrag actionChanged signal as
    // authoritative and have target drag-move events accept that action below.
    connect(drag, &QDrag::actionChanged, this, [this](Qt::DropAction action) {
        if (action == Qt::IgnoreAction || action == m_dragOverlayAction)
            return;
        m_dragOverlayAction = action;
        viewport()->update();
    });

    drag->exec(actions, defaultAction);

    m_internalDragActive = false;
    endDragDropMode();
    delete m_dragSnapshot;
    m_dragSnapshot = nullptr;
    m_dragOverlayLength = 0;
    m_dragOverlayAction = Qt::IgnoreAction;
    return true;
}

bool HexView::canDropMimeData(const QMimeData *mime) const
{
    if (!mime || !m_pDataSeq || m_nEditMode == HVMODE_READONLY)
        return false;

    return mime->hasFormat(QLatin1String(kMimeSnapshot)) ||
           mime->hasFormat(QLatin1String(kMimeOctet)) ||
           mime->hasText();
}

void HexView::updateDropCaret(const QPoint &pos)
{
    if (m_dragOverlayLength > 0) {
        m_dragOverlayVisible = true;
        m_dragOverlayPos = pos + QPoint(14, 14);
    }

    int x = pos.x();
    int y = pos.y();
    int pane = m_nWhichPane;

    m_nCursorOffset = offsetFromPhysCoord(x, y, &pane, &x, &y, &m_nSubItem);
    m_nWhichPane = pane;
    m_nSubItem = 0;

    m_caretVisible = true;
    positionCaret(x, y, m_nWhichPane);
    emit cursorChanged(m_nCursorOffset);
    viewport()->update();
}

void HexView::endDragDropMode()
{
    const bool wasDropTargeting = m_nSelectionMode == SEL_DRAGDROP;

    if (m_scrollTimer.isActive())
        m_scrollTimer.stop();

    if (m_nSelectionMode == SEL_DRAGDROP)
        m_nSelectionMode = SEL_NONE;

    if (m_dragOverlayVisible) {
        m_dragOverlayVisible = false;
        m_dragOverlayAction = Qt::IgnoreAction;
        viewport()->update();
    }

    if (wasDropTargeting)
        emit selectionChanged(selectionStart(), selectionEnd());
}

bool HexView::dropMimeData(const QMimeData *mime, Qt::DropAction action)
{
    if (!canDropMimeData(mime))
        return false;

    HexView *source = nullptr;
    unpackValue(mime, kMimeSource, &source);

    DragSelection dragSel;
    const bool hasDragSelection = unpackValue(mime, kMimeSelection, &dragSel);
    const bool sameSource = (source == this);
    const bool moveWithinThisView =
        sameSource && action == Qt::MoveAction && m_nEditMode == HVMODE_INSERT &&
        hasDragSelection && dragSel.length > 0;

    size_w dropOffset = m_nCursorOffset;
    if (moveWithinThisView &&
        dropOffset >= dragSel.start &&
        dropOffset < dragSel.start + dragSel.length)
        return false;

    HexSnapshot *hss = nullptr;
    if (unpackValue(mime, kMimeSnapshot, &hss) && hss && hss->m_source == this) {
        m_pDataSeq->group();

        if (moveWithinThisView) {
            m_pDataSeq->erase(dragSel.start, dragSel.length);
            if (dropOffset > dragSel.start)
                dropOffset -= dragSel.length;
        }

        const size_w offset = dropOffset;
        m_nCursorOffset = offset;

        if (m_nEditMode == HVMODE_OVERWRITE)
            m_pDataSeq->replace_snapshot(offset, hss->m_length,
                                         hss->m_desclist, hss->m_count);
        else
            m_pDataSeq->insert_snapshot(offset, hss->m_length,
                                        hss->m_desclist, hss->m_count);

        m_nSelectionStart = offset;
        m_nCursorOffset   = offset + hss->m_length;
        m_nSelectionEnd   = m_nCursorOffset;

        m_pDataSeq->ungroup();

        const uint method =
            m_nEditMode == HVMODE_INSERT ? HVMETHOD_INSERT : HVMETHOD_OVERWRITE;
        fireChanged(offset, hss->m_length, method);
        m_pDataSeq->breakopt();
        return true;
    }

    QByteArray bytes;
    if (mime->hasFormat(QLatin1String(kMimeOctet)))
        bytes = mime->data(QLatin1String(kMimeOctet));
    else if (mime->hasText())
        bytes = mime->text().toLatin1();

    if (bytes.isEmpty())
        return false;

    if (moveWithinThisView) {
        m_pDataSeq->group();
        m_pDataSeq->erase(dragSel.start, dragSel.length);
        if (dropOffset > dragSel.start)
            dropOffset -= dragSel.length;
        m_nCursorOffset = dropOffset;
        m_nSelectionStart = dropOffset;
        m_nSelectionEnd = dropOffset;
    }

    const size_w inserted = enterData(reinterpret_cast<uint8_t *>(bytes.data()),
                                      static_cast<size_w>(bytes.size()),
                                      true, false, true);

    if (moveWithinThisView)
        m_pDataSeq->ungroup();

    if (inserted > 0)
        m_pDataSeq->breakopt();

    return inserted > 0;
}

void HexView::dragEnterEvent(QDragEnterEvent *event)
{
    if (!canDropMimeData(event->mimeData())) {
        event->ignore();
        return;
    }

    viewport()->setFocus();
    m_nSelectionMode = SEL_DRAGDROP;
    updateDropCaret(event->position().toPoint());

    if (m_internalDragActive && m_dragOverlayAction != Qt::IgnoreAction) {
#ifdef Q_OS_WIN
        m_dragOverlayAction = internalDragAction(m_nEditMode, event);
#endif
        event->setDropAction(m_dragOverlayAction);
        event->accept();
    } else {
        m_dragOverlayAction = event->proposedAction();
        event->acceptProposedAction();
    }
}

bool HexView::viewportEvent(QEvent *event)
{
    switch (event->type()) {
    case QEvent::DragEnter:
        dragEnterEvent(static_cast<QDragEnterEvent *>(event));
        return event->isAccepted();
    case QEvent::DragMove:
        dragMoveEvent(static_cast<QDragMoveEvent *>(event));
        return event->isAccepted();
    case QEvent::DragLeave:
        dragLeaveEvent(static_cast<QDragLeaveEvent *>(event));
        return event->isAccepted();
    case QEvent::Drop:
        dropEvent(static_cast<QDropEvent *>(event));
        return event->isAccepted();
    default:
        return QAbstractScrollArea::viewportEvent(event);
    }
}

void HexView::dragMoveEvent(QDragMoveEvent *event)
{
    if (!canDropMimeData(event->mimeData())) {
        event->ignore();
        return;
    }

    updateDropCaret(event->position().toPoint());

    QRect rect(0, 0, viewport()->width(), viewport()->height());
    if (m_nFontHeight > 0)
        rect.setBottom(rect.bottom() - rect.bottom() % m_nFontHeight);

    if (isInDropAutoScrollBand(rect, event->position().toPoint())) {
        if (!m_scrollTimer.isActive()) {
            m_nScrollCounter = 0;
            m_scrollTimer.start(30);
        }
    } else if (m_scrollTimer.isActive()) {
        m_scrollTimer.stop();
    }

    if (m_internalDragActive && m_dragOverlayAction != Qt::IgnoreAction) {
#ifdef Q_OS_WIN
        m_dragOverlayAction = internalDragAction(m_nEditMode, event);
#endif
        event->setDropAction(m_dragOverlayAction);
        event->accept();
    } else {
        m_dragOverlayAction = event->proposedAction();
        event->acceptProposedAction();
    }
}

void HexView::dragLeaveEvent(QDragLeaveEvent *event)
{
    m_dragOverlayVisible = false;
    endDragDropMode();
    repositionCaret();
    viewport()->update();
    event->accept();
}

void HexView::dropEvent(QDropEvent *event)
{
    updateDropCaret(event->position().toPoint());

    const Qt::DropAction action =
        (m_internalDragActive && m_dragOverlayAction != Qt::IgnoreAction)
#ifdef Q_OS_WIN
            ? internalDragAction(m_nEditMode, event)
#else
            ? m_dragOverlayAction
#endif
            : event->proposedAction();
    m_dragOverlayAction = action;
    event->setDropAction(action);

    if (dropMimeData(event->mimeData(), action))
        event->accept();
    else
        event->ignore();

    endDragDropMode();
}
