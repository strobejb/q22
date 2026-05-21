#include "hexview.h"
#include "theme.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QScrollBar>
#include <algorithm>

// ── Construction ──────────────────────────────────────────────────────────────

HexView::HexView(QWidget *parent)
    : //QWidget(parent)
    QAbstractScrollArea(parent)
{

    // Piece-table buffer
    m_pDataSeq = new sequence();
    m_pDataSeq->init();

    // Colour slots start invalid. realiseColour() is the only source of truth
    // for HexView defaults, so clearing a slot and never setting it are equal.

    // Font
    QFont defaultFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    defaultFont.setPointSize(13);
    setFont(defaultFont);


    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    viewport()->setAcceptDrops(true);
    viewport()->setMouseTracking(true);

    // Tag both scrollbars so the QSS arrow-button rules and ScrollBarArrowPainter
    // only activate on HexView scrollbars, not on unrelated scrollbars elsewhere
    // in the application (e.g. preferences dialog panels).
    verticalScrollBar()  ->setProperty("hexViewScrollBar", true);
    horizontalScrollBar()->setProperty("hexViewScrollBar", true);

    // Wire the QAbstractScrollArea scrollbars to the internal scroll positions.
    // Block signals when we set values programmatically (in setupScrollbars) to
    // avoid a redundant repaint on every metrics update.
    connect(verticalScrollBar(),   &QScrollBar::valueChanged, this, [this](int val) {
        m_nVScrollPos = (size_w)val;
        closeNoteEditor(/*save=*/true);
        repositionCaret();
        viewport()->update();
    });
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this](int val) {
        m_nHScrollPos = val;
        closeNoteEditor(/*save=*/true);
        updateResizeBarPos();
        repositionCaret();
        viewport()->update();
    });
    // Note: setupScrollbars() blocks valueChanged via QSignalBlocker, so the
    // lambdas above only fire on user scrollbar-drag.  Wheel/programmatic scrolling
    // goes through scroll() which calls closeNoteEditor directly.

    connect(&m_caretTimer, &QTimer::timeout, this, [this] {
        m_caretVisible = !m_caretVisible;
        if (m_nFontHeight > 0)
            // +1 on right and bottom: XOR-painted rects need 1px extra margin
            // to ensure the dirty region fully covers the inverted pixels.
            viewport()->update(QRect(m_nCaretX, m_nCaretY, 3, m_nFontHeight + 1));
    });

    m_scrollTimer.setSingleShot(false);
    connect(&m_scrollTimer, &QTimer::timeout, this, &HexView::onScrollTimer);

    connect(this, &HexView::lengthChanged, this, &HexView::onLengthChanged);

    // Clear hover state when a modal dialog blocks our window.
    qApp->installEventFilter(this);



    updateMetrics();
    recalcPositions();
    //setCaretPos((m_nAddressWidth + m_nHexPaddingLeft) * m_nFontHeight, 0);
    repositionCaret();
}

HexView::~HexView()
{
    delete m_pDataSeq;
    delete m_lastSnapshot;
    delete m_dragSnapshot;
}

bool HexView::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == window() && ev->type() == QEvent::WindowBlocked) {
        m_hoverBookmarkIdx = -1;
        m_hoverOnClose     = false;
        m_hoverOnEdit      = false;
        m_pressedOnClose   = false;
        m_pressedOnEdit    = false;
        viewport()->update();
        return false;
    }

    if (obj == viewport() && ev->type() == QEvent::Leave) {
        // Ignore Leave while a bookmark button grab is active — mouse events keep
        // arriving via the grab so hover/press state updates continue normally.
        if (QWidget::mouseGrabber() == viewport()) return false;
        if (m_hoverBookmarkIdx != -1 || m_hoverOnClose || m_hoverOnEdit || m_pressedOnClose || m_pressedOnEdit) {
            m_hoverBookmarkIdx = -1;
            m_hoverOnClose     = false;
            m_hoverOnEdit      = false;
            m_pressedOnClose   = false;
            m_pressedOnEdit    = false;
            viewport()->update();
        }
        viewport()->unsetCursor();
    }

    if (m_noteEditor && (obj == m_noteEditor || obj == m_noteEditor->viewport())) {
        // Forward wheel events to the hex view so the file scrolls normally
        // while editing a bookmark note.  Consume the event so the editor
        // (which has no scrollbars) never sees it.
        if (ev->type() == QEvent::Wheel) {
            closeNoteEditor(/*save=*/true);
            viewport()->setFocus();
            wheelEvent(static_cast<QWheelEvent *>(ev));
            return true;
        }

        // Right-click on the inline editor: show the bookmark edit/delete menu
        // instead of QPlainTextEdit's default context menu.
        if (ev->type() == QEvent::ContextMenu) {
            const auto *ce = static_cast<QContextMenuEvent *>(ev);
            const int bmIdx = m_noteEditorIdx;
            if (bmIdx >= 0 && bmIdx < m_bookmarks.size()) {
                QMenu bmMenu(this);
                themeMenu(&bmMenu);
                QAction *editAct   = bmMenu.addAction(tr("&Edit"));
                QAction *deleteAct = bmMenu.addAction(tr("&Delete"));
                QAction *act = bmMenu.exec(ce->globalPos());
                if (act == editAct)
                    emit bookmarkEditRequested(bmIdx);
                else if (act == deleteAct)
                    removeBookmark(bmIdx);
            }
            return true;   // always suppress the default QPlainTextEdit menu
        }

        if (obj == m_noteEditor) {
            if (ev->type() == QEvent::FocusOut) {
                closeNoteEditor(true);
                return false;
            }
            if (ev->type() == QEvent::KeyPress) {
                const auto *ke = static_cast<QKeyEvent *>(ev);
                if (ke->key() == Qt::Key_Escape) {
                    // If this was a brand-new bookmark (name was empty when the
                    // editor opened) and the user cancelled without saving any text,
                    // remove it — don't leave an empty placeholder in the list.
                    const bool deleteNew = m_noteEditorIsNew &&
                                          m_noteEditorIdx >= 0 &&
                                          m_noteEditorIdx < m_bookmarks.size() &&
                                          m_noteEditor->toPlainText().trimmed().isEmpty();
                    const int  idxToDelete = m_noteEditorIdx;
                    closeNoteEditor(false);
                    if (deleteNew) removeBookmark(idxToDelete);
                    viewport()->setFocus();
                    return true;
                }
                if (ke->key() == Qt::Key_Tab) {
                    const size_w bmOffset =
                        (m_noteEditorIdx >= 0 && m_noteEditorIdx < m_bookmarks.size())
                        ? m_bookmarks[m_noteEditorIdx].offset : m_nCursorOffset;
                    closeNoteEditor(/*save=*/true);
                    setCurPos(bmOffset);
                    setActivePane(1);   // ASCII column
                    viewport()->setFocus();
                    return true;
                }
            }
        }
        return false;
    }
    return QAbstractScrollArea::eventFilter(obj, ev);
}

// ── Public API ────────────────────────────────────────────────────────────────

bool HexView::initBuf(const uint8_t *buf, size_t len, bool copy, bool readonly)
{
    m_pDataSeq->clear();
    bool ok = m_pDataSeq->init(buf, len, copy);
    if (ok) {
        m_nCursorOffset   = 0;
        m_nSelectionStart = 0;
        m_nSelectionEnd   = 0;
        m_nVScrollPos     = 0;
        m_nHScrollPos     = 0;
        updateMetrics();
        viewport()->update();
    }
    return ok;
}

bool HexView::openFile(const QString &path, uint /*flags*/)
{
    m_pDataSeq->clear();
    bool ok = m_pDataSeq->open(path.toStdString());
    if (ok) {
        m_filePath        = path;
        m_nCursorOffset   = 0;
        m_nSelectionStart = 0;
        m_nSelectionEnd   = 0;
        m_nVScrollPos     = 0;
        m_nHScrollPos     = 0;
        recalcLayout();
        repositionCaret();
        viewport()->update();
    }
    return ok;
}

bool HexView::saveFile(const QString &path, uint /*flags*/)
{
    return m_pDataSeq->save(path.toStdString());
}

bool HexView::clearFile()
{
    bool ok = m_pDataSeq->clear();
    if (ok) {
        m_filePath.clear();
        m_nCursorOffset   = 0;
        m_nSelectionStart = 0;
        m_nSelectionEnd   = 0;
        m_nVScrollPos     = 0;
        m_nHScrollPos     = 0;
        emit lengthChanged(0);
        repositionCaret();
        viewport()->update();
    }
    return ok;
}

size_w HexView::selectionStart() const
{
    return std::min(m_nSelectionStart, m_nSelectionEnd);
}

size_w HexView::selectionEnd() const
{
    return std::max(m_nSelectionStart, m_nSelectionEnd);
}

size_w HexView::selectionSize() const
{
    return selectionEnd() - selectionStart();
}

size_w HexView::size() const
{
    return m_pDataSeq ? m_pDataSeq->size() : 0;
}

void HexView::setActivePane(int pane)
{
    if (pane == m_nWhichPane) return;
    m_nWhichPane = qBound(0, pane, 1);
    repositionCaret();
    viewport()->update();
}

QRgb HexView::getHexColour(uint index)
{
    if (index >= HVC_MAX_COLOURS) return 0;
    return realiseColour(static_cast<HvColorSlot>(index)).rgb();
}

bool HexView::setHexColour(HvColorSlot slot, QColor col)
{
    if (slot >= HVC_MAX_COLOURS) return false;
    m_ColourList[slot] = col;
    return true;
}

uint HexView::setStyle(uint mask, uint styles)
{
    uint old = m_nControlStyles;
    m_nControlStyles = (m_nControlStyles & ~mask) | (styles & mask);

    setGrouping(m_nBytesPerColumn);

    recalcPositions();
    updateMetrics();
    viewport()->update();
    return old;
}

uint HexView::getStyle(uint mask)
{
    return m_nControlStyles & mask;
}

bool HexView::checkStyle(uint flag) const
{
    return (m_nControlStyles & flag) != 0;
}

void HexView::setFont(const QFont &font, int hSpacing, int lineSpacing)
{
    m_font = font;

    m_font.setStyleHint(QFont::Monospace);
    //m_font.setStyleStrategy( QFont::ForceIntegerMetrics );
    m_font.setHintingPreference(QFont::PreferFullHinting);
    m_font.setFixedPitch(true);
    QWidget::setFont(m_font);

    m_rawFont = QRawFont::fromFont(m_font);//, QFontDatabase::PreferDefaultHinting);

    setFontSpacing(hSpacing, lineSpacing);
}

void HexView::setFontSpacing(int hSpacing, int lineSpacing)
{
    QFontMetrics fm(m_font);

    // Use the advance of a representative character rather than averageCharWidth()
    // (which averages over the whole glyph set and may differ from the advance
    // Qt uses when it lays out individual characters for a fixed-pitch font).
    m_nFontHeight = fm.height() + lineSpacing;
    m_nFontWidth  = fm.horizontalAdvance(QLatin1Char('0')) + hSpacing;

    updateMetrics();
}

int HexView::calcTotalWidth()
{
    int width = 0;

    width += checkStyle(HVS_ADDR_INVISIBLE)  ? 0 : m_nAddressWidth;
    width += checkStyle(HVS_HEX_INVISIBLE)   ? 0 : m_nHexPaddingLeft;
    width += checkStyle(HVS_HEX_INVISIBLE)   ? 0 : m_nHexWidth;
    width += checkStyle(HVS_ASCII_INVISIBLE) ? 0 : m_nHexPaddingRight;
    width += checkStyle(HVS_ASCII_INVISIBLE) ? 0 : m_nBytesPerLine;
    width += 1;
    width += noteStripExtraColumns();

    return width;
}
uint HexView::setGrouping(uint bytes)
{
    /*uint old = m_nBytesPerColumn;
    m_nBytesPerColumn = std::max(1u, bytes);*/

    int numcols;
    int unitwidth = unitWidth();

    if(bytes < 1 || bytes >= 32)
        return 0;

    m_nBytesPerColumn = bytes;

    numcols = m_nBytesPerLine / m_nBytesPerColumn;

    if(!checkStyle(HVS_HEX_INVISIBLE))
    {
        m_nHexWidth = (unitwidth * m_nBytesPerColumn + 1) * numcols - 1;

        // take into account partial columns
        if(m_nBytesPerLine % m_nBytesPerColumn)
            m_nHexWidth += (m_nBytesPerLine % m_nBytesPerColumn) * unitwidth + 1;
    }
    else
    {
        m_nHexWidth = 0;
    }

    m_nTotalWidth =  calcTotalWidth();

    updateMetrics();
    viewport()->update();
    return 0;
}


void HexView::setPadding(int left, int right)
{
    left  = std::max(left, 0);
    right = std::max(right, 0);
    left  = std::min(left, 20);
    right = std::min(right, 20);

    m_nHexPaddingLeft  = left;
    m_nHexPaddingRight = right;

    updateMetrics();
    setupScrollbars();
    viewport()->update();
}


void HexView::refreshWindow()
{
    viewport()->update();
}

// ── Metrics ───────────────────────────────────────────────────────────────────

int HexView::unitWidth() const
{
    switch (m_nControlStyles & HVS_FORMAT_MASK) {
    case HVS_FORMAT_DEC: return 3;
    case HVS_FORMAT_OCT: return 3;
    case HVS_FORMAT_BIN: return 8;
    default:             return 2; // HEX
    }
}

void HexView::emitResize()
{
    this->resize(this->geometry().width(), this->geometry().height());
}
uint HexView::setLineLen(uint nLineLen)
{
    m_nBytesPerLine = nLineLen;
    recalcPositions();
    emitResize();
    return m_nBytesPerLine;
}

void HexView::updateResizeBarPos()
{
    m_nResizeBarPos = (-m_nHScrollPos * m_nFontWidth+(m_nTotalWidth -
                                                        m_nBytesPerLine - 1) * m_nFontWidth
                       - ((m_nHexPaddingRight*m_nFontWidth)/2));

    m_nResizeBarPos = -m_nHScrollPos;
    m_nResizeBarPos += checkStyle(HVS_ADDR_INVISIBLE) ? 0 : m_nAddressWidth;
    m_nResizeBarPos += checkStyle(HVS_HEX_INVISIBLE)  ? 0 : m_nHexPaddingLeft;
    m_nResizeBarPos += checkStyle(HVS_HEX_INVISIBLE)  ? 0 : m_nHexWidth;

    if(checkStyle(HVS_HEX_INVISIBLE) == true)
    {
        m_nResizeBarPos += m_nBytesPerLine;
        m_nResizeBarPos += m_nHexPaddingRight;
    }

    m_nResizeBarPos *= m_nFontWidth;
    m_nResizeBarPos += (m_nHexPaddingRight * m_nFontWidth)/2;
}

void HexView::onLengthChanged(size_w nNewLength)
{
    char buf[40];

    if(nNewLength % m_nBytesPerLine == 0 && nNewLength > 0)
        nNewLength--;

    if(checkStyle( HVS_ADDR_DEC ))
        m_nAddressDigits = std::max(10, snprintf(buf, 40, " %llu", (unsigned long long)nNewLength));
    else
        m_nAddressDigits = std::max(8, snprintf(buf, 40, " %llX", (unsigned long long)nNewLength));

    m_nAddressWidth = m_nAddressDigits;

    if(checkStyle( HVS_ADDR_MIDCOLON ) && !checkStyle( HVS_ADDR_DEC ))
        m_nAddressWidth++;

    if(checkStyle( HVS_ADDR_ENDCOLON ))
        m_nAddressWidth++;

    // leading space
    m_nAddressWidth++;

    recalcLayout();
}

void HexView::recalcLayout()
{
    if (m_nFontHeight > 0)
        m_nWindowLines = (int)std::min(
            (unsigned)(viewport()->height() / m_nFontHeight),
            (unsigned)numFileLines(m_pDataSeq->size()));
    if (m_nFontWidth > 0)
        m_nWindowColumns = std::min(viewport()->width() / m_nFontWidth, m_nTotalWidth);
    setupScrollbars();
}

//void HexView::recalcPositions()
//{
    /*RECT rect;
    //GetClientRect(m_hWnd, &rect);

    OnLengthChange(m_pDataSeq->size());

    m_nDataShift %= m_nBytesPerLine;
    SetGrouping(m_nBytesPerColumn);

    m_nWindowColumns = min((rect.right-rect.left) / m_nFontWidth, m_nTotalWidth);

    UpdateResizeBarPos();

    if(m_nVScrollPos > 0)
        PinToOffset(m_nVScrollPinned);*/

//}
void HexView::updateMetrics()
{
    recalcLayout();
    refreshWindow();
    repositionCaret();
    return;
    QFontMetrics fm(m_font);
    // Use the advance of a representative character rather than averageCharWidth()
    // (which averages over the whole glyph set and may differ from the advance
    // Qt uses when it lays out individual characters for a fixed-pitch font).
    m_nFontWidth  = fm.horizontalAdvance(QLatin1Char('0'));
    m_nFontHeight = fm.height();

    // Address digits — enough for the current file size
    size_w sz = m_pDataSeq ? m_pDataSeq->size() : 0;
    m_nAddressDigits = 8;
    if (sz > 0xFFFFFFFFull) m_nAddressDigits = 16;

    // Address column: " " + digits [+ ":"]
    m_nAddressWidth = 1 + m_nAddressDigits;
    if (checkStyle(HVS_ADDR_ENDCOLON)) m_nAddressWidth++;
    if (checkStyle(HVS_ADDR_INVISIBLE)) m_nAddressWidth = 0;

    // Hex column width: bytes * unit + inter-group spaces
    int groups = m_nBytesPerLine / m_nBytesPerColumn;
    m_nHexWidth = (checkStyle(HVS_HEX_INVISIBLE)) ? 0 :
        m_nBytesPerLine * unitWidth() + (groups - 1);

    m_nTotalWidth = m_nAddressWidth
                  + m_nHexPaddingLeft
                  + m_nHexWidth
                  + m_nHexPaddingRight
                  + (checkStyle(HVS_ASCII_INVISIBLE) ? 0 : m_nBytesPerLine);

    m_nWindowLines   = m_nFontHeight > 0 ? viewport()->height() / m_nFontHeight : 0;
    m_nWindowColumns = m_nFontWidth  > 0 ? viewport()->width()  / m_nFontWidth  : 0;

    // Resize bar at end of hex column
    m_nResizeBarPos = (m_nAddressWidth + m_nHexPaddingLeft + m_nHexWidth
                       - m_nHScrollPos) * m_nFontWidth;

    // Update scroll max
    if (m_pDataSeq) {
        size_w lines = (m_pDataSeq->size() + m_nDataShift + m_nBytesPerLine - 1)
                       / m_nBytesPerLine;
        m_nVScrollMax = lines > 0 ? lines - 1 : 0;
    }
    m_nHScrollMax = std::max(0, m_nTotalWidth - m_nWindowColumns);
}

int HexView::logToPhyXCoord(int x, int pane) const
{
    int offset	  = 0;
    int xpos	  = x;
    int unitwidth = unitWidth();

    switch(pane)
    {
    case 0:		// hex

        // if at the end of line need to adjust
        // but only if we are also at end of a "block"
        if((x == m_nBytesPerLine) && (m_nBytesPerLine % m_nBytesPerColumn) == 0)
            offset = -1;

        x = (x * unitwidth) + (x / m_nBytesPerColumn);

        x -= m_nHScrollPos;
        x += checkStyle(HVS_ADDR_INVISIBLE) ? 0 : m_nAddressWidth;
        x += m_nHexPaddingLeft;
        x += offset;

        if(m_nSelectionStart < m_nSelectionEnd && offset == 0 &&
            (xpos % m_nBytesPerColumn) == 0 && xpos > 0)
            x--;

        return x * m_nFontWidth;

    case 1:		// asc

        x -= m_nHScrollPos;
        x += checkStyle(HVS_ADDR_INVISIBLE) ? 0 : m_nAddressWidth;
        x += checkStyle(HVS_HEX_INVISIBLE) ? 0 : m_nHexPaddingLeft;
        x += checkStyle(HVS_HEX_INVISIBLE) ? 0 : m_nHexWidth;

        x += checkStyle(HVS_ASCII_INVISIBLE) ? 0 : m_nHexPaddingRight;

        x -= checkStyle(HVS_ASCII_INVISIBLE) ? xpos : 0;

        return x * m_nFontWidth;

    default:
        return 0;
    }
}

// ── Qt event handlers ─────────────────────────────────────────────────────────

void HexView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);

    if (checkStyle(HVS_FITTOWINDOW) && m_nFontWidth > 0) {

        int logwidth = viewport()->width() / m_nFontWidth;
        int prevbpl  = m_nBytesPerLine;
        int uw       = unitWidth();

        logwidth -= checkStyle(HVS_ADDR_INVISIBLE) ? 0 : m_nAddressWidth;

        if (checkStyle(HVS_HEX_INVISIBLE))
        {
            logwidth -= m_nHexPaddingRight + 1;
            m_nBytesPerLine = logwidth;
        }
        else if (checkStyle(HVS_ASCII_INVISIBLE))
        {
            logwidth -= m_nHexPaddingLeft;
            m_nBytesPerLine = (logwidth * m_nBytesPerColumn) /
                              (m_nBytesPerColumn * uw + 1);
        }
        else
        {
            logwidth -= m_nHexPaddingLeft + m_nHexPaddingRight;
            m_nBytesPerLine = (logwidth * m_nBytesPerColumn) /
                              (m_nBytesPerColumn * uw + m_nBytesPerColumn + 1);
        }

        int minunit = checkStyle(HVS_FORCE_FIXEDCOLS) ? m_nBytesPerColumn : 1;
        m_nBytesPerLine -= m_nBytesPerLine % m_nBytesPerColumn;
        m_nBytesPerLine  = std::max(m_nBytesPerLine, minunit);

        if (m_nBytesPerLine != prevbpl) {
            if (m_nVScrollPos > 0)
                pinToOffset(m_nVScrollPinned);
            m_nHScrollPos = 0;
        }
    }

    m_nWindowLines   = (int)std::min((unsigned)viewport()->height() / m_nFontHeight, numFileLines(m_pDataSeq->size()));
    m_nWindowColumns = (int)std::min(viewport()->width() / m_nFontWidth, m_nTotalWidth);

    if (pinToBottomCorner())
        repositionCaret();

    //updateMetrics();
    setupScrollbars();

}

void HexView::focusInEvent(QFocusEvent *event)
{
    QWidget::focusInEvent(event);
    m_caretVisible = true;
    m_caretTimer.start(QApplication::cursorFlashTime() / 2);
    viewport()->update();
}

void HexView::focusOutEvent(QFocusEvent *event)
{
    QWidget::focusOutEvent(event);
    m_caretTimer.stop();
    m_caretVisible = false;
    viewport()->update();
}

// ── Bookmarks (stubs) ─────────────────────────────────────────────────────────

// ── Internal change notification ──────────────────────────────────────────────

void HexView::fireChanged(size_w offset, size_w length, uint method)
{
    if (m_fRedrawChanges) {
        updateMetrics();
        scrollToCaret();
        viewport()->update();
    }
    if (method == HVMETHOD_INSERT || method == HVMETHOD_DELETE)
        emit lengthChanged(m_pDataSeq->size());
    emit contentChanged(offset, length, method);
}

bool HexView::allowChange(size_w /*offset*/, size_w /*length*/, uint /*method*/)
{
    return true;
}

// ── Data entry ────────────────────────────────────────────────────────────────

size_w HexView::enterData(uint8_t *pSource, size_w nLength,
                           bool fAdvanceCaret, bool fReplaceSelection, bool fSelectData)
{
    size_w offset = m_nCursorOffset;

    if (m_nEditMode == HVMODE_READONLY) return 0;
    if (!pSource) return 0;

    if (m_nEditMode != HVMODE_INSERT)
        fReplaceSelection = false;
    if (fReplaceSelection && selectionSize() == 0)
        fReplaceSelection = false;

    if (fReplaceSelection) {
        m_pDataSeq->group();
        m_pDataSeq->erase(selectionStart(), selectionSize());
    }

    if (selectionSize() > 0 &&
        (m_nCursorOffset == m_nSelectionStart || m_nCursorOffset == m_nSelectionEnd))
        m_nCursorOffset = selectionStart();

    if (m_nEditMode == HVMODE_OVERWRITE)
        m_pDataSeq->replace(m_nCursorOffset, pSource, nLength);
    else
        m_pDataSeq->insert(m_nCursorOffset, pSource, nLength);

    if (fSelectData) {
        m_nSelectionStart = m_nCursorOffset;
        m_nCursorOffset  += nLength;
        m_nSelectionEnd   = m_nCursorOffset;
    } else {
        if (fAdvanceCaret)
            m_nCursorOffset += nLength;
        m_nSelectionStart = m_nCursorOffset;
        m_nSelectionEnd   = m_nCursorOffset;
    }

    if (fReplaceSelection)
        m_pDataSeq->ungroup();

    if (m_fRedrawChanges)
        fireChanged(offset, nLength,
                    m_nEditMode == HVMODE_INSERT ? HVMETHOD_INSERT : HVMETHOD_OVERWRITE);

    return nLength;
}

size_t HexView::getData(size_w offset, uint8_t *buf, size_t len)
{
    return m_pDataSeq->render(offset, buf, len);
}

size_t HexView::setData(size_w offset, uint8_t *buf, size_t len)
{
    invalidateRange(offset, offset + len);
    return m_pDataSeq->replace(offset, buf, len);
}

size_w HexView::setDataAdv(const uint8_t *buf, size_t len)
{
    return enterData(const_cast<uint8_t *>(buf), (size_w)len,
                     /*fAdvanceCaret=*/true,
                     /*fReplaceSelection=*/false,
                     /*fSelectData=*/false);
}

void HexView::padToAddress(size_w addr)
{
    size_w filesize = m_pDataSeq ? m_pDataSeq->size() : 0;
    if (addr > filesize) {
        const size_w  count = addr - filesize;
        const uint8_t zero  = 0;
        m_pDataSeq->group();
        m_pDataSeq->insert(filesize, zero, count);
        m_pDataSeq->ungroup();
        fireChanged(filesize, count, HVMETHOD_INSERT);
    }
    setCurPos(addr);
}

void HexView::beginGroup()
{
    if (m_pDataSeq) m_pDataSeq->group();
}

void HexView::endGroup()
{
    if (m_pDataSeq) m_pDataSeq->ungroup();
}

size_t HexView::fillData(uint8_t *buf, size_t buflen, size_w len)
{
    m_pDataSeq->group();
    bool oldRedraw = m_fRedrawChanges;
    m_fRedrawChanges = false;

    size_w remaining = len;
    while (remaining > 0) {
        size_w chunk = std::min((size_w)buflen, remaining);
        enterData(buf, chunk, true, true, false);
        remaining -= chunk;
    }

    m_fRedrawChanges = oldRedraw;
    size_w off = std::min(selectionStart(), m_nCursorOffset);
    fireChanged(off, len, HVMETHOD_OVERWRITE);

    m_pDataSeq->ungroup();
    return 0;
}

// ── Cursor / selection setters ────────────────────────────────────────────────

bool HexView::setCurSel(size_w selStart, size_w selEnd)
{
    size_w sz = m_pDataSeq ? m_pDataSeq->size() : 0;
    if (selStart > sz || selEnd > sz) return false;
    if (selStart == m_nSelectionStart && selEnd == m_nSelectionEnd) return false;

    invalidateRange(m_nSelectionStart, m_nSelectionEnd);
    m_nSelectionStart = selStart;
    m_nSelectionEnd   = selEnd;
    m_nSubItem        = 0;

    if (m_nCursorOffset != selEnd) {
        m_nCursorOffset = selEnd;
        scrollToCaret();
        invalidateRange(selStart, selEnd);
    }
    return true;
}

bool HexView::setCurPos(size_w pos)
{
    if (!m_pDataSeq || pos > m_pDataSeq->size()) return false;
    if (m_nCursorOffset != pos) {
        m_nCursorOffset = pos;
        m_nSubItem = 0;
        if (m_nSelectionEnd != m_nSelectionStart) {
            m_nSelectionEnd = m_nSelectionStart = pos;
            viewport()->update();
        }
        scrollToCaret();
    }
    return true;
}

bool HexView::setSelStart(size_w pos)
{
    if (!m_pDataSeq || pos > m_pDataSeq->size()) return false;
    m_nSelectionStart = pos;
    return true;
}

bool HexView::setSelEnd(size_w pos)
{
    if (!m_pDataSeq || pos > m_pDataSeq->size()) return false;
    m_nSelectionEnd = pos;
    if (m_nCursorOffset != pos) {
        m_nCursorOffset = pos;
        scrollToCaret();
    }
    return true;
}

void HexView::selectAll()
{
    m_nSelectionStart = 0;
    m_nSelectionEnd   = m_pDataSeq ? m_pDataSeq->size() : 0;
    m_nCursorOffset   = m_nSelectionEnd;
    if (m_nBytesPerLine > 0 && m_nCursorOffset % m_nBytesPerLine == 0)
        m_fCursorAdjustment = true;

    size_w oldpos = m_nVScrollPos;
    scrollToCaret();
    if (oldpos == m_nVScrollPos)
        viewport()->update();
}
