#ifndef BOOKMARKDIALOG_H
#define BOOKMARKDIALOG_H

#include <QDialog>
#include <QColor>

namespace Ui { class BookmarkDialog; }

class BookmarkDialog : public QDialog
{
    Q_OBJECT
public:
    explicit BookmarkDialog(QWidget *parent = nullptr);
    ~BookmarkDialog();

    void setOffset(quint64 offset);
    void setLength(quint64 length);
    void setForegroundColour(const QColor &fg);

    quint64 offset() const { return m_offset; }
    quint64 length() const { return m_length; }
    QString bookmarkName() const;
    QColor  foregroundColour() const { return m_foreground; }
    QColor  selectedColour() const;

private:
    Ui::BookmarkDialog *ui;
    quint64 m_offset = 0;
    quint64 m_length = 0;
    QColor  m_foreground;
};

#endif // BOOKMARKDIALOG_H
