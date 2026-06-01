#ifndef HEXVIEW_H
#define HEXVIEW_H

#include <QColor>
#include <QWidget>
#include <QFont>
#include <QRawFont>

#include <QList>
#include <QTimer>
#include <QVector>
#include <cstdint>
#include <QAbstractScrollArea>
#include "sequence.h"
#include "hexviewbookmark.h"

// ── Colour slot indices ───────────────────────────────────────────────────────
//
// IMPORTANT: the SEL variant of each content slot must immediately follow its
// normal variant (e.g. HVC_HEXODD+1 == HVC_HEXODDSEL) — the draw code uses
// arithmetic increment to switch from normal to selected FG.  Do not reorder.
enum HvColorSlot {
    HVC_BACKGROUND = 0,
    HVC_SELECTION,          // selection BG, focused   (→ QPalette::Active Highlight)
    HVC_SELECTION_INACTIVE, // selection BG, unfocused (→ QPalette::Inactive Highlight)
    HVC_SELTEXT,            // selection FG, focused   (→ QPalette::Active HighlightedText)
    HVC_SELTEXT_INACTIVE,   // selection FG, unfocused (→ QPalette::Inactive HighlightedText)
    HVC_ADDRESS,
    HVC_HEXODD,
    HVC_HEXODDSEL,          // must be HVC_HEXODD + 1
    HVC_HEXEVEN,
    HVC_HEXEVENSEL,         // must be HVC_HEXEVEN + 1
    HVC_ASCII,
    HVC_ASCIISEL,           // must be HVC_ASCII + 1
    HVC_MODIFY,
    HVC_MODIFYSEL,          // must be HVC_MODIFY + 1
    HVC_RESIZEBAR,
    HVC_MATCHED,
    HVC_MATCHEDSEL,         // must be HVC_MATCHED + 1
    HVC_HIGHLIGHT,          // generic highlight BG (bookmarks, search matches)
    HVC_BOOKMARK1,
    HVC_BOOKMARK2,
    HVC_BOOKMARK3,
    HVC_BOOKMARK4,
    HVC_BOOKMARK5,
    HVC_BOOKMARK6,
    HVC_BOOKMARK7,
    HVC_BOOKSEL,
    HVC_BOOKMARK1_FG,       // bookmark FG for colour 1; qexed applies auto-contrast
    HVC_BOOKMARK2_FG,       // must stay in the same order as HVC_BOOKMARK1..7
    HVC_BOOKMARK3_FG,
    HVC_BOOKMARK4_FG,
    HVC_BOOKMARK5_FG,
    HVC_BOOKMARK6_FG,
    HVC_BOOKMARK7_FG,

    HVC_MAX_COLOURS
};

// ── Style flags ───────────────────────────────────────────────────────────────
#define HVS_FORMAT_HEX      0x0000
#define HVS_FORMAT_DEC      0x0001
#define HVS_FORMAT_OCT      0x0002
#define HVS_FORMAT_BIN      0x0003
#define HVS_FORMAT_MASK     0x0003

#define HVS_ADDR_HEX        0x0000
#define HVS_ADDR_VISIBLE    0x0000
#define HVS_ADDR_DEC        0x0004
#define HVS_ADDR_ENDCOLON   0x0010
#define HVS_ADDR_MIDCOLON   0x0020
#define HVS_ADDR_INVISIBLE  0x0040

#define HVS_ENDIAN_LITTLE   0x0000
#define HVS_ENDIAN_BIG      0x0400

#define HVS_ASCII_VISIBLE   0x0000
#define HVS_ASCII_SHOWCTRLS 0x0800
#define HVS_ASCII_SHOWEXTD  0x1000
#define HVS_ASCII_INVISIBLE 0x2000

#define HVS_FORCE_FIXEDCOLS 0x0080
#define HVS_FIXED_EDITMODE  0x0100
#define HVS_DISABLE_UNDO    0x0200
#define HVS_ALWAYSDELETE    0x4000
#define HVS_LOWERCASEHEX    0x010000
#define HVS_FITTOWINDOW     0x020000
#define HVS_SHOWMODS        0x040000
#define HVS_REPLACESEL      0x080000
#define HVS_ENABLEDRAGDROP  0x100000
#define HVS_HEX_INVISIBLE   0x8000
#define HVS_RESIZEBAR           0x40000000
#define HVS_INVERTSELECTION     0x80000000
#define HVS_SELECTION_OVERRIDES 0x00200000  // selection BG overrides all other highlights
#define HVS_SEARCH_HIGHLIGHT_ALL 0x08000000  // highlight all visible matches for the active search pattern

// Bookmark display behaviour
#define HVS_BOOKMARK_EXPAND_LONE   0x00400000  // lone (uncontested) bookmarks always show as full strips
#define HVS_BOOKMARK_EXPAND_CURSOR 0x00800000  // expand when cursor/selection enters the range (on release)
#define HVS_BOOKMARK_NESTED       0x01000000  // allow overlapping bookmarks; when off, new overlaps redirect to existing
#define HVS_BOOKMARK_SELECTION_HIGHLIGHTS 0x02000000  // selecting a bookmark also selects its byte range in the hex view
#define HVS_BOOKMARK_EXPAND_ALWAYS 0x04000000  // every conflict group keeps one bookmark expanded

// Edit modes
#define HVMODE_READONLY     0
#define HVMODE_INSERT       1
#define HVMODE_OVERWRITE    2

// Content-change methods
#define HVMETHOD_INSERT     1
#define HVMETHOD_OVERWRITE  2
#define HVMETHOD_DELETE     3

// Open-file flags
#define HVOF_DEFAULT        0
#define HVOF_READONLY       1
#define HVOF_QUICKLOAD      2
#define HVOF_QUICKSAVE      4

// Find flags
#define HVFF_SCOPE_SELECTION    0x0001
#define HVFF_CASE_INSENSITIVE   0x0002
#define HVFF_BACKWARD           0x0004
#define HVFF_WRAP_AROUND        0x0008

enum HvFindResult {
    HVFR_NotFound,
    HVFR_Found,
    HVFR_FoundWrapped,
    HVFR_Cancelled,
};

// Hit-test regions
enum HitTestRegion : uint {
    HVHT_NONE               = 0x00,
    HVHT_MAIN               = 0x01,
    HVHT_SELECTION          = 0x02,
    HVHT_RESIZE             = 0x10,
    HVHT_RESIZE0            = 0x20 | HVHT_RESIZE,
    HVHT_BOOKMARK           = 0x100,  // full-strip body
    HVHT_BOOKMARK_CLOSE     = 0x200,
    HVHT_BOOKMARK_EDIT      = 0x400,
    HVHT_BOOKMARK_COLLAPSED = 0x800   // collapsed single-line strip body
};

// ── Data structures ───────────────────────────────────────────────────────────
struct HEXCOL {
    QRgb colFG;
    QRgb colBG;
};
typedef HEXCOL ATTR;

enum SELMODE { SEL_NONE, SEL_NORMAL, SEL_MARGIN, SEL_DRAGDROP };

// ── HexSnapshot ───────────────────────────────────────────────────────────────
// A zero-copy snapshot of a contiguous range of the sequence's span-table.
// Used for in-process clipboard paste without materialising the bytes.
// Analogous to Win32 HexSnapShot in HexViewInternal.h.
struct HexSnapshot {
    sequence::span_desc *m_desclist = nullptr;
    size_t               m_count    = 0;
    size_w               m_length   = 0;
    class HexView       *m_source   = nullptr;  // originating widget — validated on paste

    HexSnapshot() = default;
    ~HexSnapshot() { delete[] m_desclist; }

    // Non-copyable
    HexSnapshot(const HexSnapshot &)            = delete;
    HexSnapshot &operator=(const HexSnapshot &) = delete;
};

class QPlainTextEdit;
class QPalette;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;

// ── HexView widget ────────────────────────────────────────────────────────────
//class HexView : public QWidget
class HexView : public QAbstractScrollArea
{
    Q_OBJECT

public:
    enum class BookmarkButtonAction {
        None,
        Settings,
        Close,
    };

    struct BookmarkButtonLayout {
        BookmarkButtonAction topRight = BookmarkButtonAction::Settings;
        BookmarkButtonAction bottomRight = BookmarkButtonAction::Close;
    };

    explicit HexView(QWidget *parent = nullptr);
    ~HexView();

    bool   initBuf(const uint8_t *buf, size_t len, bool copy, bool readonly);
    bool   openFile(const QString &path, uint flags = HVOF_DEFAULT);
    bool   saveFile(const QString &path, uint flags = HVOF_DEFAULT);
    bool   clearFile();

    size_w selectionStart() const;
    size_w selectionEnd()   const;
    size_w selectionSize()  const;
    size_w size()           const;
    int    activePane()     const { return m_nWhichPane; }  // 0=hex, 1=ascii
    void   setActivePane(int pane);                         // 0=hex, 1=ascii

    QRgb   getHexColour(uint index);
    bool   setHexColour(HvColorSlot slot, QColor col);
    static QColor defaultColourForSlot(HvColorSlot slot, const QPalette &pal);

    uint   setStyle(uint mask, uint styles);
    uint   getStyle(uint mask = ~0u);
    bool   checkStyle(uint flag) const;

    uint   setGrouping(uint bytes);
    uint   getGrouping() const { return m_nBytesPerColumn; }
    uint   setLineLen(uint len);
    uint   getLineLen() const   { return m_nBytesPerLine;  }
    void   setPadding(int left, int right);


    void   refreshWindow();

    // Undo / redo
    bool   undo();
    bool   redo();
    bool   canUndo() const;
    bool   canRedo() const;
    void   beginGroup();
    void   endGroup();

    // Cursor / selection
    bool   setCurSel(size_w selStart, size_w selEnd);
    bool   setCurPos(size_w pos);
    bool   setSelStart(size_w pos);
    bool   setSelEnd(size_w pos);
    void   selectAll();

    // Scroll
    bool   scrollTo(size_w offset);
    bool   scrollTop(size_w offset);
    bool   scrollCenter(size_w offset);
    bool   scrollCenterIfOffScreen(size_w offset, size_w length = 1);
    void   scrollHEnd();    // scroll to the far right (note strips visible)
    void   scrollHStart();  // scroll to the far left  (address column visible)

    // Data access
    size_t getData(size_w offset, uint8_t *buf, size_t len);
    size_t setData(size_w offset, uint8_t *buf, size_t len);

    // Import helpers: write at current cursor and advance, and zero-pad up to an address
    size_w setDataAdv(const uint8_t *buf, size_t len);
    void   padToAddress(size_w addr);

    // Find
    bool   findInit(const uint8_t *pat, size_t length);
    bool   findNext(size_w *result, uint options = 0);
    HvFindResult findNextEx(size_w *result, uint options = 0);
    void   cancelFind()      { m_findCancelled = true; }
    bool   isFindCancelled() const { return m_findCancelled; }

    // State accessors
    size_w  cursorOffset() const { return m_nCursorOffset; }
    uint    editMode()     const { return m_nEditMode; }
    QString filePath()     const { return m_filePath; }
    void   setEditMode(uint mode) { m_nEditMode = mode; viewport()->update(); emit editModeChanged(mode); }

    // Clipboard
    bool   copy();
    bool   cut();
    bool   paste();
    bool   clear();

    void   setFont(const QFont &font, int hSpacing=0, int lineSpacing=0);
    void   setFontSpacing(int hSpacing, int lineSpacing);

    // Bookmarks
    void   addBookmark(const Bookmark &bm);
    void   removeBookmark(int idx);
    void   replaceBookmark(int idx, const Bookmark &bm);
    void   setBookmarks(const QList<Bookmark> &bookmarks);
    const QList<Bookmark> &bookmarks() const { return m_bookmarks; }
    void   openNoteEditor(int bmIdx, QPoint clickPos = {-1,-1});
    void   addBookmarkInline();   // add/open bookmark at cursor / selection
    void   setBookmarkButtonLayout(const BookmarkButtonLayout &layout);
    BookmarkButtonLayout bookmarkButtonLayout() const { return m_bookmarkButtonLayout; }

    // Provide an external menu to use instead of the built-in context menu.
    // Pass nullptr to restore the built-in behaviour.  Ownership stays with
    // the caller; HexView only holds a non-owning pointer.
    void   setContextMenu(QMenu *menu) { m_contextMenu = menu; }
    void   setBookmarkContextMenuExternallyHandled(bool on) { m_bookmarkContextMenuExternallyHandled = on; }


    // Geometry of a bookmark note strip.
    struct NoteStripGeom {
        QRect   rect;       // full rounded rect (background + border area)
        QRect   textRect;   // inset text area (editor should overlay this exactly)
        QRect   rangeRect;  // range label area below text (address + byte count)
        QRect   topButtonRect;
        QRect   bottomButtonRect;
        QString rangeText;  // e.g. "0x1A3F  (16 bytes)"
        int     tipY  = 0;  // screen Y of the triangle tip (arrow point into the hex area)
        bool    valid = false;
    };

    // Layout state for a single bookmark, computed by computeBookmarkLayout().
    // Determines which of two visual states the strip is rendered in:
    //   inConflict=false             → normal (always shown as full strip)
    //   inConflict=true, isActive=true  → full strip (caret is within this bookmark's range)
    //   inConflict=true, isActive=false → tab only (thin coloured bar + triangle pointer)
    struct BmLayout {
        bool inConflict = false;
        bool isActive   = true;   // always true when inConflict=false
        bool hidden     = false;  // non-active member whose collapsed strip overlaps the active full strip; draw nothing
    };

signals:
    void cursorChanged(size_w offset);
    void selectionChanged(size_w start, size_w end);
    void editModeChanged(uint mode);
    void contentChanged(size_w offset, size_w length, uint method);
    void lengthChanged(size_w length);
    void lineLengthChanged(uint bytesPerLine);
    void findProgress(size_w pos, size_w len, double mbPerSec);
    void paneFocusRequested();   // Ctrl+Tab: caller should focus Find/Goto panel
    void bookmarksChanged();          // bookmark added, removed, or replaced
    void bookmarkEditRequested(int idx); // kept for compatibility — no longer emitted (dead)
    void bookmarkContextRequested(int idx, QRect globalRect); // right-click on bookmark/editor
    void bookmarkSettingsRequested(int idx, QRect btnGlobal); // bookmark tool button

public:
    // Mark a bookmark's gear button as "popup open" so it stays visually pressed.
    // Pass -1 to clear.  Safe to call from outside HexView (e.g. MainWindow).
    void setBookmarkPopupIdx(int idx) { m_bookmarkPopupIdx = idx; viewport()->update(); }
    int  bookmarkPopupIdx()    const  { return m_bookmarkPopupIdx; }

    // Force a specific bookmark to be shown as the full strip in its conflict group,
    // regardless of cursor position.  The pin auto-expires when the cursor leaves the
    // bookmark's byte range, so normal navigation clears it naturally.
    void expandBookmark(int idx)         { m_expandedBookmarkIdx = idx; viewport()->update(); }

protected:
    void paintEvent(QPaintEvent *event)        override;
    void resizeEvent(QResizeEvent *event)      override;
    void focusInEvent(QFocusEvent *event)      override;
    void focusOutEvent(QFocusEvent *event)     override;
    void keyPressEvent(QKeyEvent *event)       override;
    bool focusNextPrevChild(bool)              override { return false; }
    bool eventFilter(QObject *obj, QEvent *ev) override;
    void mousePressEvent(QMouseEvent *event)   override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event)    override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event)        override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool viewportEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onScrollTimer();
    void onLengthChanged(size_w length);

private:
    // ── Rendering ────────────────────────────────────────────────────────────
    QColor realiseColour(HvColorSlot slot) const;
    QColor contrastColourFor(const QColor &background,
                             const QColor &candidateA,
                             const QColor &candidateB) const;
    int    unitWidth() const;
    int    logToPhyXCoord(int x, int pane) const;
    void   updateResizeBarPos();
    void   updateMetrics();
    void   recalcPositions();
    int    calcTotalWidth();
    int    noteStripExtraColumns() const; // extra h-scroll columns for bookmark note strips
    void   emitResize();
    void   recalcLayout();

    size_t formatAddress(size_w addr, char *buf, size_t buflen);
    size_t formatHexUnit(uint8_t *data, char *buf, size_t buflen);
    size_t formatLine(uint8_t *data, size_t length, size_w offset, size_t dataShift,
                      char *szBuf, size_t nBufLen,
                      ATTR *attrList, seqchar_info *infobuf,
                      const QList<Bookmark> &matchHighlights,
                      bool fIncSelection);

    bool   getHighlightCol(size_w offset, int pane,
                           const QList<Bookmark> &highlights,
                           HEXCOL *col1, HEXCOL *col2);

    QList<Bookmark> identifySearchPatterns(const uint8_t *data, size_t len, size_w bufBaseOffset);

    // Find (Boyer-Moore)
    bool   searchCompile(const uint8_t *pat, size_t length);
    int    searchBlock(const uint8_t *block, int start, int length, int *partial, bool matchCase) const;
    void   queryProgressNotify(size_w pos, size_w len, double mbPerSec);
    int    paintLine(QPainter &painter, size_w nLineNo,
                     uint8_t *data, size_t datalen, seqchar_info *infobuf, size_t datashift,
                     const QList<Bookmark> &matchHighlights);
    void   paintCaret(QPainter &painter);
    void   paintDragOverlay(QPainter &painter);
    void   drawVLine(QPainter &painter, const QRect &paintRect, QRgb col, int pos);
    void   invalidateRange(size_w start, size_w finish);
    void drawTextFixed(QPainter &p, QPoint origin,
                       const QString &text, const QRawFont &rawFont, int cellWidth);


    // ── Bookmarks ─────────────────────────────────────────────────────────────
    int           findBookmark(size_w startoff, size_w endoff) const;
    NoteStripGeom noteStripGeom(const Bookmark &bm) const;
    QRect         bookmarkButtonRect(const NoteStripGeom &geom, BookmarkButtonAction action) const;
    HitTestRegion hitTestForBookmarkButtonAction(BookmarkButtonAction action) const;
    QRect         noteCollapsedRect(const Bookmark &bm) const;
    void          drawNoteStrip(QPainter &painter, const Bookmark &bm, const BmLayout &bml);
    int           noteStripFullHeight(const Bookmark &bm) const;
    QVector<BmLayout> computeBookmarkLayout(bool treatMouseAsReleased = false);
    void          closeNoteEditor(bool save);
    QFont         noteFont() const;


    // ── Coordinate / caret helpers ────────────────────────────────────────────
    int    getLogicalX(int x, int *pane, int *subitem = nullptr) const;
    int    getLogicalY(int y) const;
    void   caretPosFromOffset(size_w offset, int *x, int *y) const;
    size_w offsetFromPhysCoord(int mx, int my, int *pane = nullptr,
                               int *lx = nullptr, int *ly = nullptr,
                               int *subitem = nullptr);


    void   setCaretPos(int x, int y);
    void   positionCaret(int x, int y, int pane);
    void   repositionCaret();
    void   scrollToCaret();

    // ── Hit testing ───────────────────────────────────────────────────────────
    HitTestRegion hitTest(int x, int y, int *bookmarkIdx = nullptr);
    bool   isOverResizeBar(int x) const;
    bool   hasAppFocus() const;

    // ── Scroll ────────────────────────────────────────────────────────────────
    void   scroll(int dx, int dy);
    void   setupScrollbars();
    void   updatePinnedOffset();
    void   pinToOffset(size_w offset);
    bool   pinToBottomCorner();

    size_w numFileLines(size_w length) const;

    // ── Editing ───────────────────────────────────────────────────────────────
    void   fireChanged(size_w offset, size_w length, uint method);
    bool   allowChange(size_w offset, size_w length, uint method);
    size_w enterData(uint8_t *pSource, size_w nLength,
                     bool fAdvanceCaret, bool fReplaceSelection, bool fSelectData);
    size_t fillData(uint8_t *buf, size_t buflen, size_w len);
    bool   forwardDelete();
    bool   backDelete();
    void   onChar(uint nChar);

    // ── Clipboard ─────────────────────────────────────────────────────────────
    bool   onCopy();
    bool   onCut();
    bool   onPaste();
    bool   onClear();
    bool   startDrag();
    bool   canDropMimeData(const QMimeData *mime) const;
    bool   dropMimeData(const QMimeData *mime, Qt::DropAction action);
    void   updateDropCaret(const QPoint &pos);
    void   endDragDropMode();
    HexSnapshot *createSnapshot(size_w start, size_w len) const;
    HexSnapshot *m_lastSnapshot = nullptr;  // kept alive while our data is on the clipboard
    HexSnapshot *m_dragSnapshot = nullptr;  // kept alive only while QDrag::exec() is running
    bool         m_dragOverlayVisible = false;
    QPoint       m_dragOverlayPos;
    size_w       m_dragOverlayLength = 0;
    Qt::DropAction m_dragOverlayAction = Qt::IgnoreAction;
    bool         m_internalDragActive = false;

    // ── State ─────────────────────────────────────────────────────────────────
    QMenu      *m_contextMenu   = nullptr;  // nullptr → use built-in menu
    sequence    *m_pDataSeq     = nullptr;
    QString      m_filePath;

    uint    m_nControlStyles    = 0;
    uint    m_nEditMode         = HVMODE_OVERWRITE;

    // Cursor & selection
    size_w  m_nCursorOffset     = 0;
    int     m_nCaretX           = 0;
    int     m_nCaretY           = 0;
    int     m_nWhichPane        = 0;   // 0=hex, 1=ascii
    int     m_nSubItem          = 0;
    bool    m_fCursorAdjustment = false;
    bool    m_fCursorMoved      = true;
    size_w  m_nAddressOffset    = 0;
    size_w  m_nLastEditOffset   = 0;

    size_w  m_nSelectionStart   = 0;
    size_w  m_nSelectionEnd     = 0;
    SELMODE m_nSelectionMode    = SEL_NONE;

    int     m_nDataShift        = 0;

    // Font
    QFont   m_font;
    QRawFont m_rawFont;
    int     m_nFontHeight       = 0;
    int     m_nFontWidth        = 0;

    // View dimensions
    int     m_nWindowLines      = 0;
    int     m_nWindowColumns    = 0;
    int     m_nBytesPerLine     = 16;
    int     m_nBytesPerColumn   = 1;

    // Scrollbar state
    size_w  m_nVScrollPos       = 0;
    size_w  m_nVScrollMax       = 0;
    int     m_nHScrollPos       = 0;
    int     m_nHScrollMax       = 0;
    size_w  m_nVScrollPinned    = 0;

    // Column widths (in character units)
    int     m_nAddressWidth     = 0;
    int     m_nAddressDigits    = 8;
    int     m_nHexWidth         = 0;
    int     m_nHexPaddingLeft   = 1;
    int     m_nHexPaddingRight  = 1;
    int     m_nTotalWidth       = 0;
    int     m_nResizeBarPos     = 0;

    // Colours
    QColor  m_ColourList[HVC_MAX_COLOURS] = {};

    // Bookmarks
    QList<Bookmark> m_bookmarks;
    BookmarkButtonLayout m_bookmarkButtonLayout;
    int  m_pressedBookmarkIdx = -1;

    // Note strip inline editor
    QPlainTextEdit *m_noteEditor      = nullptr;
    int             m_noteEditorIdx   = -1;
    bool            m_noteEditorIsNew = false;  // true when opened for a never-saved bookmark


    // Index of the bookmark whose settings popup is currently open (-1 = none).
    // Set via setBookmarkPopupIdx() so the gear button stays in its pressed state.
    int             m_bookmarkPopupIdx = -1;

    // Pinned bookmark: overrides / seeds the cursor-based winner in computeBookmarkLayout().
    // Updated automatically when expansion-on-navigation enters a bookmark's
    // range; cleared when blank-space navigation leaves the pinned range.
    // Also set explicitly by pinBookmark() when the user clicks a bookmark strip.
    int             m_expandedBookmarkIdx = -1;

    // Surfaced bookmark: tracks the last collapsed-tab member that was brought to
    // the front by cursor navigation (separate from m_pinnedBookmarkIdx so that
    // surface-stickiness never causes unintended full-strip expansion).
    // Updated whenever the cursor enters a bookmark range; blank-space
    // navigation leaves this surfaced tab alone.
    int             m_surfacedBookmarkIdx = -1;

    // Note strip hover/press state (updated in mouseMoveEvent idle path and press/release)
    int             m_hoverBookmarkIdx = -1;
    bool            m_hoverOnClose     = false;
    bool            m_hoverOnEdit      = false;
    bool            m_pressedOnClose   = false;
    bool            m_pressedOnEdit    = false;
    bool            m_bookmarkContextMenuExternallyHandled = false;

    // Mouse / interaction
    bool    m_fResizeBar        = false;
    bool    m_fResizeAddr       = false;
    bool    m_fStartDrag        = false;
    QPoint  m_dragStartPos;
    HitTestRegion m_pressedHitTest = HVHT_NONE;

    // Scroll timer (for auto-scroll during mouse drag)
    QTimer  m_scrollTimer;
    int     m_nScrollCounter    = 0;
    int     m_nScrollMouseRemainder = 0;

    // Search
    uint8_t  m_pSearchPat[256]  = {};
    uint     m_nSearchLen       = 0;

    // Boyer-Moore search tables (populated by searchCompile)
    unsigned m_bmSkip[256]      = {};
    unsigned m_bmD[256]         = {};
    bool     m_findCancelled    = false;

    // Caret
    QTimer  m_caretTimer;
    bool    m_caretVisible      = false;

    bool    m_fRedrawChanges    = true;
};

#endif // HEXVIEW_H
