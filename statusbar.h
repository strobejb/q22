#ifndef STATUSBAR_H
#define STATUSBAR_H

#include "theme.h"
#include <QObject>
#include <QComboBox>
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

    QSize sizeHint() const override;

protected:
    void enterEvent(QEnterEvent *e) override { m_hovered = true; update(); QComboBox::enterEvent(e); }
    void leaveEvent(QEvent       *e) override
    {
        // Don't drop the hover highlight while the popup is open — the mouse
        // leaving the widget to reach the menu should not clear the state.
        if (!m_popupOpen) { m_hovered = false; update(); }
        QComboBox::leaveEvent(e);
    }

    void popupRight(QMenu *menu)
    {
        // Explicit toggle: close if already visible (e.g. keyboard-triggered).
        if (menu->isVisible()) { menu->hide(); return; }

        // Detect the "same click" pattern: when a Qt::Popup menu is open and
        // the user left-clicks the combobox, Qt closes the popup first (firing
        // aboutToHide which records the cursor position), then delivers the
        // click to the combobox which calls showPopup() → popupRight().
        // Because the mouse hasn't moved between those two steps, the current
        // cursor position equals the recorded close position — so we know this
        // is the same click and suppress the re-open.  We consume m_closePos
        // immediately so the very next click opens normally.
        const QPoint curPos = QCursor::pos();
        const bool   sameClick = (m_closePos == curPos);
        m_closePos = QPoint(-1, -1);
        if (sameClick) return;

        m_popupOpen = true;
        m_hovered   = true;
        update();

        // When the menu closes, record the cursor position and restore hover.
        // SingleShotConnection auto-removes after one fire so repeated
        // open/close cycles don't accumulate connections.
        connect(menu, &QMenu::aboutToHide, this, [this]() {
            m_popupOpen = false;
            m_closePos  = QCursor::pos();
            m_hovered   = underMouse();
            update();
        }, Qt::SingleShotConnection);

        QPoint pos = smartMenuPos(this, menu, /*rightAlign=*/true);
        menu->popup(pos);
    }
    void paintEvent(QPaintEvent *) override
    {
        QStylePainter painter(this);
        QStyleOptionComboBox opt;
        initStyleOption(&opt);
        opt.currentText = m_displayText;
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
    bool    m_hovered   = false;
    bool    m_popupOpen = false;
    QPoint  m_closePos  { -1, -1 };  // cursor pos when menu last closed; used to detect same-click re-open
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
    bool m_hex       = true;
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

    QSize sizeHint()        const override;
    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent *) override;
    bool event(QEvent *) override;

private:
    void layoutPanels();
    QList<QWidget *> m_panels;
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

private:
    QString computeValueText() const;

    HexView              *m_hv;
    RadioComboBox        *m_comboCursor = nullptr;
    RadioComboBox        *m_comboLength = nullptr;
    ValueOptionsComboBox *m_comboValue  = nullptr;
    RadioComboBox        *m_comboMode   = nullptr;
};

#endif // STATUSBAR_H
