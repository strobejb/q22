#ifndef GOTODIALOG_H
#define GOTODIALOG_H

#include <QPoint>
#include <QWidget>

class DataTypeComboBox;
class HexView;
class QAction;
namespace Ui { class GotoDialog; }

class GotoDialog : public QWidget
{
    Q_OBJECT
public:
    explicit GotoDialog(HexView *hv, QWidget *parent = nullptr);
    ~GotoDialog();

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

private:
    QByteArray buildPattern() const;
    void       triggerSearch(uint flags);
    void       updateSearchHexPreview();

    Ui::GotoDialog   *ui;
    HexView          *m_hv;
    DataTypeComboBox *m_comboBookmarks  = nullptr;
    QPoint            m_optMenuClosePos { -1, -1 };
    QPoint            m_navMenuClosePos { -1, -1 };
};

#endif // GOTODIALOG_H
