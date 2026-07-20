#ifndef SOURCEBREADCRUMBBAR_H
#define SOURCEBREADCRUMBBAR_H

#include <QColor>
#include <QPoint>
#include <QRect>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <functional>

class QPainter;
class QToolButton;

class SourceBreadcrumbBar : public QWidget
{
public:
    explicit SourceBreadcrumbBar(QWidget *parent = nullptr);

    void setNavigateCallback(std::function<void(int)> callback);
    void setExitCallback(std::function<void()> callback);
    void setBreadcrumbEnabled(bool enabled);
    void setStack(const QStringList &labels, const QStringList &details, int activeIndex);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool event(QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    static constexpr int kBarHeight = 36;
    static constexpr int kRadius = 8;
    static constexpr int kLeftPad = 6;
    static constexpr int kTabHeight = 33;
    static constexpr int kTabHorzPad = 14;
    static constexpr int kCloseSide = 28;
    static constexpr int kCloseGap = 6;

    void updateStyle();
    void updateTabRects();
    int tabAt(const QPoint &pos) const;
    void paintTab(QPainter *painter, const QRect &rect, const QString &label,
                  bool active, const QColor &fill);
    void updateVisibleState();

    QToolButton *m_closeButton = nullptr;
    QStringList m_labels;
    QStringList m_details;
    QVector<QRect> m_tabRects;
    int m_activeIndex = -1;
    int m_hoverIndex = -1;
    bool m_breadcrumbEnabled = false;
    bool m_updatingStyle = false;
    std::function<void(int)> m_navigateCallback;
    std::function<void()> m_exitCallback;
};

#endif // SOURCEBREADCRUMBBAR_H
