#ifndef ROUNDEDLISTWIDGET_H
#define ROUNDEDLISTWIDGET_H

#include <QListWidget>

class RoundedListWidget : public QListWidget
{
    Q_OBJECT
public:
    explicit RoundedListWidget(QWidget *parent = nullptr);

protected:
    void configureViewport();
};

#endif // ROUNDEDLISTWIDGET_H
