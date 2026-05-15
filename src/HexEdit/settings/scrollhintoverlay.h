#ifndef SCROLLHINTOVERLAY_H
#define SCROLLHINTOVERLAY_H

#include <QMetaObject>
#include <QObject>

class HintWidget;
class QScrollArea;

class ScrollHintOverlay : public QObject
{
    Q_OBJECT
public:
    static void install(QScrollArea *scrollArea);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    explicit ScrollHintOverlay(QScrollArea *scrollArea);
    void reposition();
    void updateVisibility();
    void dismiss();
    void reset();

    QScrollArea *m_scroll;
    HintWidget  *m_hint;
    bool         m_dismissed = false;
};

#endif // SCROLLHINTOVERLAY_H
