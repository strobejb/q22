#ifndef SHADOW_CHROME_H
#define SHADOW_CHROME_H

#include <QColor>
#include <QWidget>

class QPainter;
class QRect;
class QRectF;

struct ShadowChromeParams
{
    int shadowSize = 18;
    int cornerRadius = 10;
    int maxAlpha = 80;
    qreal alphaPower = 2.0;
    QColor shadowColor = QColor(0, 0, 0);
    QColor borderColor;
};

QRectF shadowContentRect(const QRectF &rect, const ShadowChromeParams &params);
void paintShadowRings(QPainter &p, const QRectF &content, const ShadowChromeParams &params);
void paintShadowedRoundedWindow(QPainter &p, QWidget *widget, const QRect &rect,
                                const ShadowChromeParams &params,
                                bool includeBackground);
void paintShadowCornerRepair(QPainter &p, const QRect &rect, const ShadowChromeParams &params);

class ShadowCornerClipper : public QWidget
{
public:
    ShadowCornerClipper(QWidget *parent, const ShadowChromeParams &params);

    bool eventFilter(QObject *obj, QEvent *event) override;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    ShadowChromeParams m_params;
};

#endif // SHADOW_CHROME_H
