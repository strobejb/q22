#ifndef FINDDIALOG_H
#define FINDDIALOG_H

#include <QByteArray>
#include <QPoint>
#include <QWidget>

class DataTypeComboBox;
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
    void activate(const QString &initialText = {});

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
    void keyPressEvent(QKeyEvent *e) override;
    void hideEvent(QHideEvent *e)    override;
    void changeEvent(QEvent *e)      override;

private:
    QByteArray buildPattern() const;
    void       triggerSearch(uint flags);
    void       updateSearchHexPreview();

    Ui::FindDialog   *ui;
    QAction          *m_actRegex       = nullptr;
    QAction          *m_actWholeWord   = nullptr;
    QAction          *m_actWrap        = nullptr;
    DataTypeComboBox *m_comboDataType  = nullptr;
    QPoint            m_optMenuClosePos { -1, -1 };
    QPoint            m_navMenuClosePos { -1, -1 };
};

#endif // FINDDIALOG_H
