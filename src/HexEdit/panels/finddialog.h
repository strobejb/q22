#ifndef FINDDIALOG_H
#define FINDDIALOG_H

#include <QByteArray>
#include <QPoint>
#include <QWidget>

class DataTypeComboBox;
class DockPanelRow;
class QAction;
namespace Ui { class FindDialog; }

enum SearchDataType {
    SearchHex,
    SearchUTF8,
    SearchUTF16,
    SearchUTF32,
    SearchByte,
    SearchWord,
    SearchDword,
};

class FindDialog : public QWidget
{
    Q_OBJECT
public:
    explicit FindDialog(QWidget *parent = nullptr);
    ~FindDialog();

    // Show the bar and give focus to the search field.
    // Pass initialText to pre-fill (e.g. from a selection).
    // pane: 0=hex (selects Hex type), 1=ascii (selects last-used text encoding),
    //       -1=leave type unchanged.
    void activate(const QString &initialText = {}, int pane = -1);

    bool    isRegex()     const;
    bool    isWholeWord() const;
    bool    isWrapAround() const;
    QString dataType()    const;

signals:
    void findPrevious();
    void findNext();
    void searchRequested(const QByteArray &pattern, uint flags);
    void searchHexChanged(const QString &hex);  // live hex-tuple preview of current pattern

protected:
    void keyPressEvent(QKeyEvent *e)            override;
    void hideEvent(QHideEvent *e)               override;
    void changeEvent(QEvent *e)                 override;
    bool eventFilter(QObject *o, QEvent *e)     override;

public:
    QByteArray buildPattern() const;

private:
    void       refreshStylesheet();
    void       triggerSearch(uint flags);
    void       updateSearchHexPreview();

    Ui::FindDialog   *ui;
    bool              m_inRefresh      = false;
    QAction          *m_actRegex       = nullptr;
    QAction          *m_actWholeWord   = nullptr;
    QAction          *m_actWrap        = nullptr;
    DockPanelRow     *m_row            = nullptr;
    DataTypeComboBox *m_comboDataType  = nullptr;
    QPoint            m_optMenuClosePos { -1, -1 };
    // QPoint         m_navMenuClosePos { -1, -1 };  // used by commented-out btnNavigate
    SearchDataType    m_lastTextType   = SearchUTF8; // remembered across pane-1 activations
};

#endif // FINDDIALOG_H
