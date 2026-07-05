#ifndef FILESTATS_WIDGETS_H
#define FILESTATS_WIDGETS_H

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QHeaderView>
#include <QList>
#include <QPalette>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTreeWidgetItem>
#include <QWidget>

#include <functional>

class QCheckBox;
class QEnterEvent;
class QEvent;
class QGraphicsOpacityEffect;
class QHideEvent;
class QLabel;
class QMouseEvent;
class QPainter;
class QPaintEvent;
class QProgressBar;
class QPropertyAnimation;
class QResizeEvent;
class QToolButton;
class QTreeWidget;
class QVBoxLayout;

namespace filestats
{

class ActionBanner;

static constexpr int kContentMargin            = 10;
static constexpr int kSectionHeaderOuterMargin = 0;
static constexpr int kHeaderControlGap         = 2;
static constexpr int kGroupTopGap              = 20;
static constexpr int kSectionHeaderHeight      = 32;
static constexpr int kCardLeftInset            = 5;
static constexpr int kCardScrollbarInset       = 6;
static constexpr int kSettingsCardShadowInset  = 4;
static constexpr int kStringsListMinHeight     = 160;
static constexpr int kEntropyViewMinHeight     = 80;
static constexpr int kEntropyViewDefaultHeight = 120;

inline QString cssColor(const QColor &color)
{
    return color.name(QColor::HexArgb);
}

inline QColor subduedTextColor(const QPalette &palette)
{
    const QColor    text       = palette.color(QPalette::WindowText);
    const QColor    mid        = palette.color(QPalette::Mid);
    constexpr qreal midWeight  = 0.55;
    constexpr qreal textWeight = 1.0 - midWeight;
    return QColor(qRound(text.red() * textWeight + mid.red() * midWeight),
                  qRound(text.green() * textWeight + mid.green() * midWeight),
                  qRound(text.blue() * textWeight + mid.blue() * midWeight),
                  qRound(text.alpha() * textWeight + mid.alpha() * midWeight));
}

inline QColor stringsHeaderTextColor(const QPalette &palette)
{
    const QColor    mid        = palette.color(QPalette::Mid);
    const QColor    dark       = palette.color(QPalette::Dark);
    constexpr qreal midWeight  = 0.45;
    constexpr qreal darkWeight = 1.0 - midWeight;
    return QColor(qRound(mid.red() * midWeight + dark.red() * darkWeight),
                  qRound(mid.green() * midWeight + dark.green() * darkWeight),
                  qRound(mid.blue() * midWeight + dark.blue() * darkWeight),
                  qRound(mid.alpha() * midWeight + dark.alpha() * darkWeight));
}

inline int stringsHeaderPadding(const QFontMetrics &fm)
{
    return qMax(4, qRound(fm.height() * 0.6));
}

inline int stringsHeaderGap(const QFontMetrics &fm)
{
    return qMax(4, fm.height() / 4);
}

static constexpr int kStringFooterRole = Qt::UserRole + 2;

QFont styledListHeaderFont(const QFont &baseFont);
int   styledListHeaderHeight(const QFont &baseFont);
int   styledListHeaderItemLeftPadding(const QFont &baseFont, int itemCellInset = 3);

class StyledListHeader : public QHeaderView
{
  public:
    explicit StyledListHeader(Qt::Orientation orientation, QWidget *parent = nullptr);

  protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

    bool  isSectionHovered(int logicalIndex) const;
    bool  nearSectionResizeHandle(const QPoint &pos) const;
    QRect sectionTextRect(const QRect &sectionRect, const QFontMetrics &metrics) const;
    void  paintSectionBase(QPainter *painter, const QRect &rect, int logicalIndex,
                           const QFont &headerFont, const QString &text, bool drawNativeHeader = false) const;

  private:
    int m_hoveredSection = -1;
};

class StyledSortHeader : public StyledListHeader
{
  public:
    explicit StyledSortHeader(Qt::Orientation orientation, QWidget *parent = nullptr);

  protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override;
};

class SectionHeader : public QWidget
{
  public:
    explicit SectionHeader(const QString &title, QWidget *parent = nullptr);

    QString title() const { return m_title; }
    bool    isCollapsed() const { return m_collapsed; }

    void setTitle(const QString &title) { m_title = title; update(); }
    void setCollapsed(bool collapsed);
    void setExpandable(bool expandable) { m_expandable = expandable; updateChevronIcon(); update(); }
    void setCloseMode(bool on);
    void setSectionExpanded(bool expanded) { m_sectionExpanded = expanded; updateChevronIcon(); update(); }
    void setExpandCallback(std::function<void()> callback) { m_expandCallback = std::move(callback); }
    void setClickedCallback(std::function<void()> callback) { m_clicked = std::move(callback); }
    void setDoubleClickCallback(std::function<void()> callback) { m_doubleClicked = std::move(callback); }
    void setDragCallbacks(std::function<void(QPoint)> started, std::function<void(QPoint)> moved,
                          std::function<void(QPoint)> ended)
    {
        m_dragStarted = std::move(started);
        m_dragMoved   = std::move(moved);
        m_dragEnded   = std::move(ended);
    }
    void setDragTarget(bool on) { m_isDragTarget = on; update(); }
    void syncHoverFromCursor();

  protected:
    void resizeEvent(QResizeEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

  private:
    QRect chevronHitRect() const;
    void  updateChevronIcon();



    QString                     m_title;
    QLabel                     *m_icon = nullptr;
    std::function<void()>       m_clicked;
    std::function<void()>       m_expandCallback;
    std::function<void()>       m_doubleClicked;
    std::function<void(QPoint)> m_dragStarted;
    std::function<void(QPoint)> m_dragMoved;
    std::function<void(QPoint)> m_dragEnded;
    bool                        m_collapsed           = false;
    bool                        m_closeMode           = false;
    bool                        m_expandable          = false;
    bool                        m_sectionExpanded     = false;
    bool                        m_chevronHovered      = false;
    bool                        m_chevronPressed      = false;
    bool                        m_hover               = false;
    bool                        m_pressing            = false;
    bool                        m_dragging            = false;
    bool                        m_dragMovedAfterStart = false;
    bool                        m_isDragTarget        = false;
    QPoint                      m_pressGlobal;
};

class SectionOperationStrip
{
  public:
    SectionOperationStrip(QWidget *parent, const std::function<void()> &onStart, const std::function<void()> &onStop,
                          const std::function<void()> &onResume, const std::function<void()> &onRetry);

    QWidget      *widget() const { return m_strip; }
    QProgressBar *progressBar() const { return m_progress; }
    QToolButton  *stopButton() const { return m_stopButton; }

    bool hasOperation() const;
    void setCollapsed(bool collapsed) { m_collapsed = collapsed; updateVisibility(); }
    void showProgress();
    void setProgressActionStop() { setProgressAction(ProgressAction::Stop); }
    void setProgressActionResume() { setProgressAction(ProgressAction::Resume); }
    void showRetry(const QString &message) { showAction(message, QObject::tr("Restart")); }
    void showRescan(const QString &message) { showAction(message, QObject::tr("Rescan")); }
    void showAction(const QString &message, const QString &buttonText);
    void showStart(const QString &message);
    void clear();

  private:
    enum class ProgressAction
    {
        Stop,
        Resume
    };

    void setProgressAction(ProgressAction action);
    void updateVisibility();

    QWidget              *m_strip       = nullptr;
    QWidget              *m_startRow    = nullptr;
    QLabel               *m_startLabel  = nullptr;
    QToolButton          *m_startButton = nullptr;
    QWidget              *m_progressRow = nullptr;
    QProgressBar         *m_progress    = nullptr;
    QToolButton          *m_stopButton  = nullptr;
    ActionBanner         *m_retryStrip  = nullptr;
    std::function<void()> m_onStop;
    std::function<void()> m_onResume;
    ProgressAction        m_progressAction = ProgressAction::Stop;
    bool                  m_collapsed      = false;
};

class VerticalResizeHandle : public QWidget
{
  public:
    static constexpr int kHeight = 14;

    explicit VerticalResizeHandle(std::function<void(int)> onDrag, QWidget *parent = nullptr);

  protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

  private:
    std::function<void(int)> m_onDrag;
    bool                     m_hovered = false;
    int                      m_dragY   = 0;
};

class StringListFrame : public QFrame
{
  public:
    explicit StringListFrame(QWidget *parent = nullptr);

    void setListWidget(QTreeWidget *list);
    void setFooterText(const QString &text);
    void clearFooter() { setFooterText({}); }
    void clearList();
    void refreshFooterPlacement();

  protected:
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;

  private:
    struct FooterItem : public QTreeWidgetItem
    {
        using QTreeWidgetItem::QTreeWidgetItem;
        bool operator<(const QTreeWidgetItem &other) const override;
    };

    void ensureFooterItem();
    void refreshFooterAppearance();
    void removeFooterItem();
    void positionList();

    static constexpr int kInset         = 4;
    QTreeWidget         *m_list         = nullptr;
    QTreeWidgetItem     *m_footerItem   = nullptr;
    QWidget             *m_footerWidget = nullptr;
    QLabel              *m_footerLabel  = nullptr;
    QString              m_footerText;
};

// Rounded content area with a row of pill-shaped tabs connecting to its top
// edge -- same visual language as structview's StructureContentFrame
// (rounded body with a "notch" in the border where the active tab meets it),
// generalized here to an arbitrary list of plain text tabs so other panels
// can reuse the look without duplicating the paint/hit-test code.
class TabbedContentFrame : public QWidget
{
  public:
    explicit TabbedContentFrame(QWidget *parent = nullptr);

    void setTabs(const QStringList &labels);
    void setContentWidget(QWidget *widget);
    void setStatusLabel(QLabel *label);
    void setTabChangedCallback(std::function<void(int)> callback);
    void setCurrentIndex(int index);
    int  currentIndex() const { return m_currentIndex; }

    QSize sizeHint() const override;

  protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

  private:
    int     tabTop() const;
    QRectF  contentBodyRect() const;
    void    updateTabRects();
    QRect   tabGroupRect() const;
    void    updateFooterChildren();
    int     tabAt(const QPoint &pos) const;
    void    paintContentBody(QPainter *painter, const QColor &base, const QColor &border);
    QRect   activeTabRect() const;
    void    paintTabChrome(QPainter *painter, const QRect &rect, const QColor &fill, bool active);
    void    paintTab(QPainter *painter, const QRect &rect, const QString &text, int index);

    static constexpr int kRadius      = 6;
    static constexpr int kBorderWidth = 1;
    static constexpr int kTabHeight   = 24;
    static constexpr int kTabHorzPad  = 14;
    static constexpr int kFooterPad   = 6;

    QVBoxLayout              *m_contentLayout = nullptr;
    QLabel                   *m_statusLabel   = nullptr;
    QStringList               m_tabLabels;
    QList<QRect>               m_tabRects;
    int                        m_currentIndex = 0;
    int                        m_hoverTab     = -1;
    std::function<void(int)>   m_tabChangedCallback;
};

class PropertyRow : public QWidget
{
  public:
    enum class Action
    {
        CopyValue,
        OpenExternal
    };

    explicit PropertyRow(const QString &label, QLabel **valueOut, QWidget *parent = nullptr,
                         Action action = Action::CopyValue, std::function<void()> actionCallback = {},
                         QCheckBox **checkBoxOut = nullptr, bool checked = false);

    QSize sizeHint() const override
    {
        const QFontMetrics fm(font());
        return QSize(1, qMax(42, fm.height() * 2 + 16));
    }
    QSize minimumSizeHint() const override { return QSize(1, sizeHint().height()); }

  protected:
    void mousePressEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

  private:
    void                 updateHoverIcon();
    static PropertyRow *&hoveredRow();
    static void          setHoveredRow(PropertyRow *row);
    static void          clearHoveredRow(PropertyRow *row);
    void                 setActionIconPressed(bool pressed);
    void                 updateActionIconStyle();
    void                 triggerAction(const QPoint &clickPos);
    bool                 isActionHit(const QPoint &pos) const;
    void                 positionFeedback(const QPoint &clickPos);
    void                 showCopiedFeedback(const QPoint &clickPos);
    void                 installHoverFilter(QWidget *widget);

    QLabel                 *m_nameLabel       = nullptr;
    QLabel                 *m_valueLabel      = nullptr;
    QLabel                 *m_actionIcon      = nullptr;
    QLabel                 *m_feedback        = nullptr;
    QGraphicsOpacityEffect *m_feedbackOpacity = nullptr;
    QPropertyAnimation     *m_feedbackFadeIn  = nullptr;
    QPropertyAnimation     *m_feedbackFadeOut = nullptr;
    Action                  m_action          = Action::CopyValue;
    bool                    m_iconHovered     = false;
    bool                    m_iconPressed     = false;
    std::function<void()>   m_actionCallback;
};

} // namespace filestats

#endif // FILESTATS_WIDGETS_H
