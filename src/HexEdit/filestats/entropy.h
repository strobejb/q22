#ifndef FILESTATS_ENTROPY_H
#define FILESTATS_ENTROPY_H

#include <QVector>
#include <QWidget>

class QMouseEvent;
class QPaintEvent;

namespace filestats
{

class EntropyView : public QWidget
{
    Q_OBJECT
public:
    explicit EntropyView(QWidget *parent = nullptr);

    void setData(const QVector<float> &data, qulonglong fileSize, int windowSize);
    void clear();
    void setRotated(bool rotated);

    const QVector<float> &data() const { return m_data; }
    bool  isRotated() const { return m_rotated; }
    float minEntropy() const;
    float avgEntropy() const;
    float maxEntropy() const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void positionHovered(qulonglong byteOffset, float entropy);
    void hoverCleared();

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    float        sampleAt(int pos, int axisLen) const;
    static QColor colorForEntropy(float e);

    QVector<float> m_data;
    qulonglong     m_fileSize   = 0;
    int            m_windowSize = 256;
    int            m_hoverX     = -1;
    bool           m_rotated    = false;
};

} // namespace filestats

#endif // FILESTATS_ENTROPY_H
