#ifndef BOOKMARKCOLOURWIDGET_H
#define BOOKMARKCOLOURWIDGET_H

#include <QWidget>
#include <QColor>
#include <QVector>

class BookmarkColourWidget : public QWidget
{
    Q_OBJECT
public:
    explicit BookmarkColourWidget(QWidget *parent = nullptr);

    void setColumns(int cols);
    void setColours(const QVector<QColor> &colours);
    void setForegroundColour(const QColor &fg);
    void setSelectedColour(const QColor &colour);

    QColor selectedColour() const;
    int    selectedIndex()  const { return m_selectedIndex; }

    QSize sizeHint() const override;

signals:
    void colourSelected(const QColor &colour);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    int cellSize() const;
    int indexAt(const QPoint &pos) const;

    QVector<QColor> m_colours;
    int m_columns = 7;
    int m_selectedIndex = -1;
    int m_hoveredIndex  = -1;
    QColor m_foreground;
};

#endif // BOOKMARKCOLOURWIDGET_H
