#ifndef FILESTATS_ENTROPY_H
#define FILESTATS_ENTROPY_H

#include <QImage>
#include <QVector>
#include <QWidget>

class QMouseEvent;
class QPaintEvent;

namespace filestats
{

enum class BigramScale     { Log, Linear, Sqrt };
enum class HilbertColorMode { ByteClass, Magnitude, Entropy, Detail };

class EntropyView : public QWidget
{
    Q_OBJECT
public:
    explicit EntropyView(QWidget *parent = nullptr);

    void setData(const QVector<float> &data, qulonglong fileSize, int windowSize);
    void setBigramData(const QVector<quint64> &counts, qulonglong fileSize);
    void setBigramScale(BigramScale scale);
    void setByteClassData(const QVector<float> &data, qulonglong fileSize, int windowSize);
    void setHilbertData(const QVector<quint8> &bytes, qulonglong scopeSize, int sampleCount, int gridSide, qulonglong scopeStart = 0);
    void setGilbertData(const QVector<quint8> &bytes, qulonglong scopeSize, int sampleCount, qulonglong scopeStart = 0);
    void setHilbertColorMode(HilbertColorMode mode);
    void clear();
    void setRotated(bool rotated);
    void setSelection(qulonglong start, qulonglong end);
    void clearSelection();

    const QVector<float>   &data()          const { return m_data; }
    const QVector<quint64> &bigramCounts()  const { return m_bigramCounts; }
    const QVector<float>   &byteClassData() const { return m_byteClassData; }
    bool  isRotated()   const { return m_rotated; }
    bool  isBigram()    const { return m_isBigram; }
    bool  isByteClass() const { return m_isByteClass; }
    bool             isHilbert()       const { return m_isHilbert; }
    bool             isGilbert()       const { return m_isGilbert; }
    HilbertColorMode hilbertColorMode() const { return m_hilbertColorMode; }
    float minEntropy()    const;
    float avgEntropy()    const;
    float maxEntropy()    const;
    float byteClassAvg(int classIdx) const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void positionHovered(qulonglong byteOffset, float entropy);
    void positionClicked(qulonglong byteOffset);
    void rangeSelected(qulonglong anchor, qulonglong cursor);
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
    void          rebuildHilbertImage();
    qulonglong    offsetForWidgetPos(int wx, int wy) const;

    QVector<float>   m_data;
    QVector<quint64> m_bigramCounts;
    QVector<float>   m_byteClassData;
    QVector<quint8>  m_hilbertRawData;
    QVector<int>     m_gilbertInverse;
    QImage           m_bigramImage;
    QImage           m_hilbertCachedImage;
    qulonglong       m_fileSize           = 0;
    int              m_windowSize         = 256;
    int              m_hilbertSampleCount = 0;
    int              m_hilbertGridSide    = 256;
    qulonglong       m_hilbertScopeStart = 0;
    qulonglong       m_dragAnchor        = 0;
    int              m_hoverX             = -1;
    int              m_hoverY             = -1;
    bool             m_rotated            = true;
    bool             m_dragging           = false;
    bool             m_hasSelection       = false;
    bool             m_isBigram           = false;
    bool             m_isByteClass        = false;
    bool             m_isHilbert          = false;
    bool             m_isGilbert          = false;
    HilbertColorMode m_hilbertColorMode   = HilbertColorMode::ByteClass;
    BigramScale      m_bigramScale        = BigramScale::Log;
    qulonglong       m_selStart           = 0;
    qulonglong       m_selEnd             = 0;
};

} // namespace filestats

#endif // FILESTATS_ENTROPY_H
