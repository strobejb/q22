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

// Owns the status bar panels and all update logic. Construct with a HexView and
// the window's QStatusBar; call update() whenever the HexView state changes.
class StatusBar : public QObject
{
    Q_OBJECT
public:
    StatusBar(HexView *hv, QStatusBar *bar, QObject *parent = nullptr);

public slots:
    void update();
    void onFindProgress(size_w pos, size_w len, double mbPerSec);
    void onFindDone();
    void showSearchHex(const QString &hex);
    void showMessage(const QString &msg);

private:
    QString computeValueText() const;

    HexView              *m_hv;
    bool                  m_hasSel      = false;
    RadioComboBox        *m_comboCursor = nullptr;
    RadioComboBox        *m_comboLength = nullptr;
    ValueOptionsComboBox *m_comboValue  = nullptr;
    RadioComboBox        *m_comboMode   = nullptr;

    QWidget      *m_searchWidget = nullptr;
    QLabel       *m_searchLabel  = nullptr;
    QProgressBar *m_searchBar    = nullptr;
    QWidget      *m_searchSep    = nullptr;
    QLabel       *m_patternLabel = nullptr;
    QWidget      *m_patternSep   = nullptr;
};

#endif // STATUSBAR_H
