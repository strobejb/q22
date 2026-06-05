#ifndef STATUSBAR_H
#define STATUSBAR_H

#include "combos/statuscomboboxes.h"
#include "HexView/seqbase.h"

#include <QLabel>
#include <QList>
#include <QObject>
#include <QProgressBar>
#include <QStatusBar>
#include <QWidget>

class HexView;
class QEvent;
class QHBoxLayout;
class QToolButton;

// Right-anchored container for the status bar panels. Panels are positioned
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
    void setRightAligned(bool on);

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
    bool             m_rightAligned = true;
};

// Owns the status bar panels and all update logic. Construct with a HexView and
// the window's QStatusBar; call update() whenever the HexView state changes.
class StatusBar : public QObject
{
    Q_OBJECT
public:
    StatusBar(HexView *hv, QStatusBar *bar, bool showPanelToggles,
              bool toolsRight, bool infoRight, QObject *parent = nullptr);

    void setFileInfoPanelOpen(bool open);
    void setTypesPanelOpen(bool open);
    void setAlignment(bool toolsRight, bool infoRight);

signals:
    void fileInfoToggled();
    void typesToggled(bool checked);

public slots:
    void update();
    void onFindProgress(size_w pos, size_w len, double mbPerSec);
    void onFindDone();
    void showSearchHex(const QString &hex);
    void showMessage(const QString &msg);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    QString computeValueText() const;
    void rebuildLayout();
    void refreshToggleIcons();

    HexView              *m_hv;
    QStatusBar           *m_bar         = nullptr;
    bool                  m_showPanelToggles = false;
    bool                  m_toolsRight  = true;
    bool                  m_infoRight   = true;
    bool                  m_hasSel      = false;
    RadioComboBox        *m_comboCursor = nullptr;
    RadioComboBox        *m_comboLength = nullptr;
    ValueOptionsComboBox *m_comboValue  = nullptr;
    RadioComboBox        *m_comboMode   = nullptr;
    QToolButton          *m_fileInfoBtn = nullptr;
    QToolButton          *m_typesBtn    = nullptr;
    QWidget              *m_toggleStrip = nullptr;
    QHBoxLayout          *m_toggleLayout = nullptr;
    PanelStrip           *m_comboStrip  = nullptr;

    QWidget      *m_searchWidget = nullptr;
    QLabel       *m_searchLabel  = nullptr;
    QProgressBar *m_searchBar    = nullptr;
    QWidget      *m_searchSep    = nullptr;
    QLabel       *m_patternLabel = nullptr;
    QWidget      *m_patternSep   = nullptr;
};

#endif // STATUSBAR_H
