#ifndef FINDPANEL_H
#define FINDPANEL_H

#include "panels/findpattern.h"

#include <QByteArray>
#include <QPoint>
#include <QWidget>

class DataTypeComboBox;
class DockPanelRow;
class QAction;
namespace Ui { class FindPanel; }

class FindPanel : public QWidget
{
    Q_OBJECT
public:
    explicit FindPanel(QWidget *parent = nullptr);
    ~FindPanel();

    // Show the bar and give focus to the search field.
    // Pass initialText to pre-fill (e.g. from a selection).
    // pane: 0=hex (selects Hex type), 1=ascii (selects last-used text encoding),
    //       -1=leave type unchanged.
    void activate(const QString &initialText = {}, int pane = -1);

    bool    isRegex()     const;
    bool    isWholeWord() const;
    bool    isWrapAround() const;
    bool    highlightAllOccurrences() const;
    QString dataType()    const;
    bool    searchTextBigEndian() const;
    bool    searchIntegerBigEndian() const;
    bool    searchSigned() const;
    void    setSearchTextBigEndian(bool checked);
    void    setSearchIntegerBigEndian(bool checked);
    void    setSearchSigned(bool checked);

signals:
    void findPrevious();
    void findNext();
    void searchRequested(const QByteArray &pattern, uint flags);
    void searchHexChanged(const QString &hex);  // live hex-tuple preview of current pattern
    void highlightAllOccurrencesChanged(bool on);

protected:
    void keyPressEvent(QKeyEvent *e)            override;
    void hideEvent(QHideEvent *e)               override;
    void changeEvent(QEvent *e)                 override;
    bool eventFilter(QObject *o, QEvent *e)     override;

public:
    QByteArray buildPattern() const;

private:
    void       refreshStylesheet();
    void       refreshSearchIcon();
    void       refreshDataTypeDisplayText();
    void       triggerSearch(uint flags);
    void       updateSearchHexPreview();

    Ui::FindPanel   *ui;
    bool              m_inRefresh      = false;
    QAction          *m_actRegex       = nullptr;
    QAction          *m_actWholeWord   = nullptr;
    QAction          *m_actWrap        = nullptr;
    QAction          *m_actHighlightAll = nullptr;
    DockPanelRow     *m_row            = nullptr;
    DataTypeComboBox *m_comboDataType  = nullptr;
    QPoint            m_optMenuClosePos { -1, -1 };
    // QPoint         m_navMenuClosePos { -1, -1 };  // used by commented-out btnNavigate
    SearchDataType    m_lastTextType   = SearchUTF8; // remembered across pane-1 activations
};

#endif // FINDPANEL_H
