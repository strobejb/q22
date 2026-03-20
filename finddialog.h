#ifndef FINDDIALOG_H
#define FINDDIALOG_H

#include <QWidget>

class DataTypeComboBox;
class QAction;
namespace Ui { class FindDialog; }

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

protected:
    void keyPressEvent(QKeyEvent *e) override;

private:
    Ui::FindDialog   *ui;
    QAction          *m_actRegex       = nullptr;
    QAction          *m_actWholeWord   = nullptr;
    QAction          *m_actWrap        = nullptr;
    DataTypeComboBox *m_comboDataType  = nullptr;
};

#endif // FINDDIALOG_H
