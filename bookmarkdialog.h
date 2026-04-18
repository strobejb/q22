#ifndef BOOKMARKDIALOG_H
#define BOOKMARKDIALOG_H

#include "theme.h"
#include <QDialog>
#include <QColor>
#include <QVector>

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
    void setSwatchColours(const QVector<QColor> &colours);

    quint64 offset() const { return m_offset; }
    quint64 length() const { return m_length; }
    QString bookmarkName() const;
    QColor  foregroundColour()    const { return m_foreground; }
    QColor  selectedColour()      const;
    int     selectedColourIndex() const;

protected:
    void showEvent(QShowEvent *event) override;

private:
    void updateRangeLabel();

    Ui::BookmarkDialog *ui;
    quint64 m_offset = 0;
    quint64 m_length = 0;
    QColor  m_foreground;
};

#endif // BOOKMARKDIALOG_H
