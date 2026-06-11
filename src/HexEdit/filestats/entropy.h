#ifndef FILESTATS_ENTROPY_H
#define FILESTATS_ENTROPY_H

#include <QImage>
#include <QVector>
#include <QWidget>

class QMouseEvent;
class QPaintEvent;

namespace filestats
{

enum class BigramScale { Log, Linear, Sqrt };

class EntropyView : public QWidget
{
    Q_OBJECT
public:
    explicit EntropyView(QWidget *parent = nullptr);

    void setData(const QVector<float> &data, qulonglong fileSize, int windowSize);
    void setBigramData(const QVector<quint64> &counts, qulonglong fileSize);
    void setBigramScale(BigramScale scale);
    void clear();
    void setRotated(bool rotated);
    void setSelection(qulonglong start, qulonglong end);
    void clearSelection();

    const QVector<float>  &data()       const { return m_data; }
    const QVector<quint64> &bigramCounts() const { return m_bigramCounts; }
    bool  isRotated() const { return m_rotated; }
    bool  isBigram()  const { return m_isBigram; }
    float minEntropy() const;
    float avgEntropy() const;
    float maxEntropy() const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void positionHovered(qulonglong byteOffset, float entropy);
    void positionClicked(qulonglong byteOffset);
    void hoverCleared();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    float         sampleAt(int pos, int axisLen) const;
    static QColor colorForEntropy(float e);
    void          buildBigramImage();

    QVector<float>   m_data;
    QVector<quint64> m_bigramCounts;
    QImage           m_bigramImage;
    qulonglong       m_fileSize     = 0;
    int              m_windowSize   = 256;
    int              m_hoverX       = -1;
    bool             m_rotated      = true;
    bool             m_dragging     = false;
    bool             m_hasSelection = false;
    bool             m_isBigram     = false;
    BigramScale      m_bigramScale  = BigramScale::Log;
    qulonglong       m_selStart     = 0;
    qulonglong       m_selEnd       = 0;
};

} // namespace filestats

#endif // FILESTATS_ENTROPY_H
