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

    // Repopulate the bookmarks combo from the HexView's current bookmark list.
    void refreshBookmarks();

signals:
    void bookmarkRequested();

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void hideEvent(QHideEvent *e)    override;
    void changeEvent(QEvent *e)      override;

private:
    void       refreshStylesheet();
    QByteArray buildPattern() const;
    void       triggerSearch(uint flags);
    void       updateSearchHexPreview();

    Ui::GotoPanel   *ui;
    HexView          *m_hv;
    bool              m_inRefresh      = false;
    DockPanelRow     *m_row            = nullptr;
    DataTypeComboBox *m_comboBookmarks  = nullptr;
    QPoint            m_optMenuClosePos { -1, -1 };
    QPoint            m_navMenuClosePos { -1, -1 };
};

#endif // GOTOPANEL_H

