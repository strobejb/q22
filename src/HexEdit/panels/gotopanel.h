#ifndef GOTOPANEL_H
#define GOTOPANEL_H

#include <QPoint>
#include <QWidget>

class DataTypeComboBox;
class DockPanelRow;
class HexView;
class QAction;
namespace Ui { class GotoPanel; }

class GotoPanel : public QWidget
{
    Q_OBJECT
public:
    explicit GotoPanel(HexView *hv, QWidget *parent = nullptr);
    ~GotoPanel();

    // Show the bar and give focus to the search field.
    // Pass initialText to pre-fill (e.g. from a selection).
    void activate(const QString &initialText = {});
    void setContentMaximumWidth(int width);

    // Repopulate the bookmarks combo from the HexView's current bookmark list.
    void refreshBookmarks();

    // Show a small coloured circle swatch next to each bookmark in the dropdown.
    // On by default; call with false to disable without touching any user setting.
    void setBookmarkSwatchesEnabled(bool enabled) { m_bookmarkSwatches = enabled; refreshBookmarks(); }

signals:
    void bookmarkRequested();
    void bookmarkActivated(int idx);

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void hideEvent(QHideEvent *e)    override;
    void changeEvent(QEvent *e)      override;

private:
    enum class GotoOrigin {
        FromStart,
        FromCursor,
        FromEnd,
    };

    void       refreshStylesheet();
    QByteArray buildPattern() const;
    void       triggerSearch(uint flags);
    void       updateSearchHexPreview();

    Ui::GotoPanel   *ui;
    HexView          *m_hv;
    bool              m_inRefresh      = false;
    QAction          *m_actHexAddress  = nullptr;
    GotoOrigin        m_origin         = GotoOrigin::FromStart;
    DockPanelRow     *m_row            = nullptr;
    DataTypeComboBox *m_comboBookmarks  = nullptr;
    bool              m_bookmarkSwatches = true;
    QPoint            m_optMenuClosePos { -1, -1 };
    QPoint            m_navMenuClosePos { -1, -1 };
};

#endif // GOTOPANEL_H
