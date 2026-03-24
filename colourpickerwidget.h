#ifndef COLOURPICKERWIDGET_H
#define COLOURPICKERWIDGET_H

#include <QWidget>
#include <QColor>
#include <QVector>

class ColourPickerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ColourPickerWidget(QWidget *parent = nullptr);

    void setColumns(int cols);
    void setForegroundColour(const QColor &fg);
    void setSelectedColour(const QColor &colour);

    QColor selectedColour() const;

    QSize sizeHint() const override;

signals:
    void colourSelected(const QColor &colour);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    int cellSize() const;
    int indexAt(const QPoint &pos) const;

    QVector<QColor> m_colours;
    int m_columns = 7;
    int m_selectedIndex = -1;
    QColor m_foreground;
};

#endif // COLOURPICKERWIDGET_H
