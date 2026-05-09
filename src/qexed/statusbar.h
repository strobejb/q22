#ifndef STATUSBAR_H
#define STATUSBAR_H

#include "theme.h"
#include "HexView/seqbase.h"
#include <QGuiApplication>
#include <QObject>
#include <QComboBox>
#include <QScreen>
#include <QLabel>
#include <QProgressBar>
#include <QEnterEvent>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QStylePainter>
#include <QStyleOption>
#include <QCursor>
#include <QStatusBar>

class HexView;

// ── ValueComboBox ─────────────────────────────────────────────────────────────
// QComboBox that paints a custom display string when collapsed, and provides a
// helper for right-aligning a popup menu to the widget edge.
class ValueComboBox : public QComboBox
{
    Q_OBJECT
public:
    explicit ValueComboBox(QWidget *parent = nullptr) : QComboBox(parent)
    {
        setFocusPolicy(Qt::NoFocus);
    }
    void setDisplayText(const QString &text)
    {
        if (m_displayText == text) return;
        m_displayText = text;
        updateGeometry(); // notify PanelStrip that our preferred width changed
        update();
    }
    const QString &displayText() const { return m_displayText; }

    QSize sizeHint() const override;

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;
    void enterEvent(QEnterEvent *e) override { m_hovered = true; update(); QComboBox::enterEvent(e); }
    void leaveEvent(QEvent       *e) override
    {
        // Don't drop the hover highlight while the popup is open — the mouse
        // leaving the widget to reach the menu should not clear the state.
        if (!m_popupOpen && !property("popupOpen").toBool()) { m_hovered = false; update(); }
        QComboBox::leaveEvent(e);
    }

    void popupRight(QMenu *menu)
    {
        // Explicit toggle: close if already visible (e.g. keyboard-triggered).
        if (menu->isVisible()) { menu->hide(); return; }

        const QPoint curPos = QCursor::pos();
        const bool   sameClick = (m_closePos == curPos);
        m_closePos = QPoint(-1, -1);
        if (sameClick) return;

        m_popupOpen = true;
        m_hovered   = true;
        update();

        connect(menu, &QMenu::aboutToHide, this, [this]() {
            // Let QComboBox clear its internal arrow-sunken state; without this the
            // combo stays in State_Sunken because mouseReleaseEvent is never
            // delivered (the QMenu grabs the mouse and consumes the release).
            QComboBox::hidePopup();
            m_popupOpen = false;
            m_closePos  = QCursor::pos();
            m_hovered   = underMouse();
            update();
        }, Qt::SingleShotConnection);

        const QPoint pos = smartMenuPos(this, menu, /*rightAlign=*/true);
        // On Wayland the compositor may reposition the popup window; move() in
        // the QEvent::Show handler overrides the compositor's placement.
        if (!m_menuFilterInstalled) {
            menu->installEventFilter(this);
            m_menuFilterInstalled = true;
        }
        m_pendingMenuPos = pos;
        menu->popup(pos);
    }
    // ── Same-click-reopen guard ───────────────────────────────────────────
    // Subclasses that manage their own popup menu call isSameClickReopen()
    // at the top of showPopup() and recordMenuClose() in their aboutToHide
    // handler to get the same toggle behaviour as popupRight().

    // Returns true (and consumes the guard) when showPopup() was triggered
    // by the same click that just dismissed the popup via Qt::Popup auto-close.
    bool isSameClickReopen() {
        const QPoint cur = QCursor::pos();
        const bool same = (m_closePos == cur);
        m_closePos = {-1, -1};
        return same;
    }
    // Record the cursor position at close time so isSameClickReopen() can
    // detect the next showPopup() call that belongs to the same click.
    void recordMenuClose() { m_closePos = QCursor::pos(); }

    void paintEvent(QPaintEvent *) override
    {
        QStylePainter painter(this);
        QStyleOptionComboBox opt;
        initStyleOption(&opt);
        opt.currentText = m_displayText;
        // Preserve hover appearance while the popup is open: Qt delivers a
        // Leave event when the popup window appears, which clears State_MouseOver
        // in initStyleOption even though the combo should still look active.
        if (m_hovered || m_popupOpen)
            opt.state |= QStyle::State_MouseOver;
        painter.drawComplexControl(QStyle::CC_ComboBox, opt);
        painter.drawControl(QStyle::CE_ComboBoxLabel, opt);
        // The global stylesheet suppresses the native drop-down arrow.
        // Draw it explicitly, but only while the mouse is over the widget.
        if (m_hovered) {
            QStyleOptionComboBox arrowOpt = opt;
            arrowOpt.rect = style()->subControlRect(
                QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxArrow, this);
            painter.drawPrimitive(QStyle::PE_IndicatorArrowDown, arrowOpt);
        }
    }
private:
    QString m_displayText;
    bool    m_hovered             = false;
    bool    m_popupOpen           = false;
    bool    m_menuFilterInstalled = false;
    QPoint  m_closePos     { -1, -1 };
    QPoint  m_pendingMenuPos { -1, -1 };
};

// ── RadioComboBox ─────────────────────────────────────────────────────────────
// ValueComboBox whose dropdown is a QMenu with mutually-exclusive checkable
// items (radio behaviour via QActionGroup).
class RadioComboBox : public ValueComboBox
{
    Q_OBJECT
public:
    explicit RadioComboBox(const QStringList &items, QWidget *parent = nullptr);
    int  selection() const { return m_selection; }
    void setSelection(int index);           // programmatic — no signal emitted
signals:
    void selectionChanged(int index);
protected:
    void showPopup() override;
private:
    QMenu          *m_menu      = nullptr;
    QList<QAction*> m_actions;
    int             m_selection = 0;
    bool            m_updating  = false;
};

// ── ValueOptionsComboBox ──────────────────────────────────────────────────────
// ValueComboBox whose dropdown is a QMenu with checkable format toggles and an
// exclusive data-size selector.
class ValueOptionsComboBox : public ValueComboBox
{
    Q_OBJECT
public:
    explicit ValueOptionsComboBox(QWidget *parent = nullptr);

    bool isSigned()    const { return m_signed; }
    bool isBigEndian() const { return m_bigEndian; }
    bool isHex()       const { return m_hex; }
    int  dataSize()    const { return m_dataSize; }  // 0=byte 1=word 2=dword 3=qword 4=float 5=double

signals:
    void optionsChanged();

protected:
    void showPopup() override;

private:
    QMenu   *m_menu         = nullptr;
    QAction *m_actSigned    = nullptr;
    QAction *m_actBigEndian = nullptr;
    QAction *m_actHex       = nullptr;
    QAction *m_sizeActions[6] = {};

    bool m_signed    = false;
    bool m_bigEndian = false;
    bool m_hex       = false;
    int  m_dataSize  = 0;
};

// ── PanelStrip ────────────────────────────────────────────────────────────────
// Right-anchored container for the status bar panels.  Panels are positioned
// right-to-left at their preferred widths; when the strip is narrower than the
// total panel width the leftmost panels are clipped at the strip's left edge.
// minimumSizeHint() advertises width=0 so the window can shrink freely.
class PanelStrip : public QWidget
{
    Q_OBJECT
public:
    explicit PanelStrip(QWidget *parent = nullptr);
    void addPanel(QWidget *w);
    void setOverlapGuard(QWidget *w);

    QSize sizeHint()        const override;
    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent *) override;
    bool event(QEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;

private:
    void layoutPanels();
    void checkOverlap();
    QList<QWidget *> m_panels;
    QWidget         *m_overlapGuard = nullptr;
};

// ── StatusBar ─────────────────────────────────────────────────────────────────
// Owns the four status bar panels and all update logic.
// Construct with a HexView and the window's QStatusBar; call update() whenever
// the HexView state changes.
class StatusBar : public QObject
{
    Q_OBJECT
public:
    StatusBar(HexView *hv, QStatusBar *bar, QObject *parent = nullptr);

public slots:
    void update();
    void onFindProgress(size_w pos, size_w len, double mbPerSec);
    void onFindDone();
    void showSearchHex(const QString &hex);  // "" hides the preview
    void showMessage(const QString &msg);    // show arbitrary text in pattern label; "" hides

private:
    QString computeValueText() const;

    HexView              *m_hv;
    RadioComboBox        *m_comboCursor  = nullptr;
    RadioComboBox        *m_comboLength  = nullptr;
    ValueOptionsComboBox *m_comboValue   = nullptr;
    RadioComboBox        *m_comboMode    = nullptr;

    QWidget      *m_searchWidget  = nullptr;
    QLabel       *m_searchLabel   = nullptr;
    QProgressBar *m_searchBar     = nullptr;
    QWidget      *m_searchSep     = nullptr;
    QLabel       *m_patternLabel  = nullptr;
    QWidget      *m_patternSep    = nullptr;
};

#endif // STATUSBAR_H
