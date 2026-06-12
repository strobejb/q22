#include "filestats/entropy.h"
#include "filestats/sidepanel.h"
#include "filestats/widgets.h"
#include "HexView/hexview.h"
#include "theme.h"

#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QPointer>
#include <QProgressBar>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <numeric>

using namespace filestats;

// -----------------------------------------------------------------------
// EntropyView
// -----------------------------------------------------------------------

EntropyView::EntropyView(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
}

void EntropyView::setData(const QVector<float> &data, qulonglong fileSize, int windowSize)
{
    m_data        = data;
    m_fileSize    = fileSize;
    m_windowSize  = windowSize;
    m_isBigram    = false;
    m_isByteClass = false;
    m_isHilbert   = false;
    m_isGilbert   = false;
    m_hoverX      = -1;
    update();
}

void EntropyView::clear()
{
    m_data.clear();
    m_bigramCounts.clear();
    m_byteClassData.clear();
    m_hilbertRawData.clear();
    m_gilbertInverse.clear();
    m_bigramImage        = QImage();
    m_hilbertCachedImage = QImage();
    m_isBigram           = false;
    m_isByteClass        = false;
    m_isHilbert          = false;
    m_isGilbert          = false;
    m_hilbertSampleCount = 0;
    m_hilbertScopeStart  = 0;
    m_fileSize           = 0;
    m_windowSize         = 256;
    m_hoverX             = -1;
    m_hoverY             = -1;
    update();
}

void EntropyView::setBigramData(const QVector<quint64> &counts, qulonglong fileSize)
{
    m_bigramCounts = counts;
    m_fileSize     = fileSize;
    m_isBigram     = true;
    m_isByteClass  = false;
    m_isHilbert    = false;
    m_isGilbert    = false;
    m_hoverX       = -1;
    buildBigramImage();
    update();
}

void EntropyView::buildBigramImage()
{
    if (m_bigramCounts.size() != 256 * 256)
    {
        m_bigramImage = QImage();
        return;
    }

    quint64 maxCount = 1;
    for (quint64 c : m_bigramCounts)
        if (c > maxCount) maxCount = c;

    const double dmax = double(maxCount);
    const QRgb   bg   = palette().base().color().rgb();

    auto normalise = [&](quint64 count) -> float {
        if (count == 0) return 0.0f;
        switch (m_bigramScale) {
        case BigramScale::Log:    return float(std::log(1.0 + double(count)) / std::log(1.0 + dmax));
        case BigramScale::Sqrt:   return float(std::sqrt(double(count))       / std::sqrt(dmax));
        case BigramScale::Linear: return float(double(count) / dmax);
        }
        return 0.0f;
    };

    m_bigramImage = QImage(256, 256, QImage::Format_RGB32);
    for (int y = 0; y < 256; ++y)
    {
        QRgb *line = reinterpret_cast<QRgb *>(m_bigramImage.scanLine(y));
        for (int x = 0; x < 256; ++x)
        {
            const float norm = normalise(m_bigramCounts[y * 256 + x]);
            line[x] = (norm == 0.0f) ? bg : colorForEntropy(norm).rgb();
        }
    }
}

void EntropyView::setBigramScale(BigramScale scale)
{
    if (m_bigramScale == scale)
        return;
    m_bigramScale = scale;
    if (!m_bigramCounts.isEmpty())
    {
        buildBigramImage();
        update();
    }
}

void EntropyView::setByteClassData(const QVector<float> &data, qulonglong fileSize, int windowSize)
{
    m_byteClassData = data;
    m_fileSize      = fileSize;
    m_windowSize    = windowSize;
    m_isByteClass   = true;
    m_isBigram      = false;
    m_isHilbert     = false;
    m_isGilbert     = false;
    m_hoverX        = -1;
    update();
}

void EntropyView::setHilbertData(const QVector<quint8> &bytes, qulonglong scopeSize, int sampleCount, int gridSide, qulonglong scopeStart)
{
    m_hilbertRawData     = bytes;
    m_hilbertSampleCount = sampleCount;
    m_hilbertGridSide    = qMax(1, gridSide);
    m_hilbertScopeStart  = scopeStart;
    m_fileSize           = scopeSize;
    m_isHilbert          = true;
    m_isGilbert          = false;
    m_isBigram           = false;
    m_isByteClass        = false;
    m_hilbertCachedImage = QImage();
    m_gilbertInverse.clear();
    m_hoverX             = -1;
    m_hoverY             = -1;
    update();
}

void EntropyView::setGilbertData(const QVector<quint8> &bytes, qulonglong scopeSize, int sampleCount, qulonglong scopeStart)
{
    m_hilbertRawData     = bytes;
    m_hilbertSampleCount = sampleCount;
    m_hilbertScopeStart  = scopeStart;
    m_fileSize           = scopeSize;
    m_isGilbert          = true;
    m_isHilbert          = false;
    m_isBigram           = false;
    m_isByteClass        = false;
    m_hilbertCachedImage = QImage();
    m_gilbertInverse.clear();
    m_hoverX             = -1;
    m_hoverY             = -1;
    update();
}

void EntropyView::setHilbertColorMode(HilbertColorMode mode)
{
    if (m_hilbertColorMode == mode)
        return;
    m_hilbertColorMode   = mode;
    m_hilbertCachedImage = QImage(); // invalidate — rebuilt on next paint
    update();
}

// Gilbert curve: generalized space-filling curve for arbitrary W×H rectangles.
// Fills `out` with (x,y) positions in traversal order so out[i] is the pixel
// that belongs to file-order position i.
static void gilbertGenerate(int x, int y, int ax, int ay, int bx, int by,
                             QVector<QPoint> &out)
{
    const int w   = std::abs(ax + ay);
    const int h   = std::abs(bx + by);
    const int dax = (ax > 0) - (ax < 0);
    const int day = (ay > 0) - (ay < 0);
    const int dbx = (bx > 0) - (bx < 0);
    const int dby = (by > 0) - (by < 0);

    if (h == 1)
    {
        for (int i = 0; i < w; ++i) { out.append({x, y}); x += dax; y += day; }
        return;
    }
    if (w == 1)
    {
        for (int i = 0; i < h; ++i) { out.append({x, y}); x += dbx; y += dby; }
        return;
    }

    int ax2 = ax / 2, ay2 = ay / 2;
    int bx2 = bx / 2, by2 = by / 2;
    const int w2 = std::abs(ax2 + ay2);
    const int h2 = std::abs(bx2 + by2);

    if (2 * w > 3 * h)
    {
        if ((w2 % 2) && (w > 2)) { ax2 += dax; ay2 += day; }
        gilbertGenerate(x,       y,       ax2,      ay2,      bx,       by,       out);
        gilbertGenerate(x + ax2, y + ay2, ax - ax2, ay - ay2, bx,       by,       out);
    }
    else
    {
        if ((h2 % 2) && (h > 2)) { bx2 += dbx; by2 += dby; }
        gilbertGenerate(x,                          y,                          bx2,      by2,      ax2,       ay2,       out);
        gilbertGenerate(x + bx2,                    y + by2,                    ax,       ay,       bx - bx2,  by - by2,  out);
        gilbertGenerate(x + (ax - dax) + (bx2 - dbx), y + (ay - day) + (by2 - dby),
                        -bx2, -by2, -(ax - ax2), -(ay - ay2), out);
    }
}

// Interpolate between evenly-spaced color stops (t in [0,1]).
static QRgb gradientColor(float t, const QRgb *stops, int n)
{
    t = qBound(0.0f, t, 1.0f);
    const float fi  = t * (n - 1);
    const int   lo  = qMin(static_cast<int>(fi), n - 2);
    const float f   = fi - lo;
    return qRgb(qRound(qRed(stops[lo])   + f * (qRed(stops[lo+1])   - qRed(stops[lo]))),
                qRound(qGreen(stops[lo]) + f * (qGreen(stops[lo+1]) - qGreen(stops[lo]))),
                qRound(qBlue(stops[lo])  + f * (qBlue(stops[lo+1])  - qBlue(stops[lo]))));
}

void EntropyView::rebuildHilbertImage()
{
    if (m_hilbertRawData.isEmpty())
    {
        m_hilbertCachedImage = QImage();
        m_gilbertInverse.clear();
        return;
    }

    // Hilbert: square image (gridSide × gridSide), letterboxed in widget.
    // Gilbert: image at widget size, filling the widget.
    int iw, ih;
    if (m_isHilbert)
        iw = ih = m_hilbertGridSide;
    else
        { iw = width(); ih = height(); }

    if (iw <= 0 || ih <= 0)
    {
        m_hilbertCachedImage = QImage();
        m_gilbertInverse.clear();
        return;
    }

    const int sampleCount = m_hilbertSampleCount;
    const quint8 *rawData = m_hilbertRawData.constData();

    // ------------------------------------------------------------------
    // Build a per-sample colour array based on the current colour mode.
    // ------------------------------------------------------------------
    QVector<QRgb> sampleColors(sampleCount, 0xFF000000u);

    switch (m_hilbertColorMode)
    {
    case HilbertColorMode::ByteClass:
    {
        static const QRgb kCls[4] = {
            qRgb(0x00, 0x00, 0x00),   // null       — black
            qRgb(0x88, 0xFF, 0xFF),   // whitespace — cyan
            qRgb(0x00, 0x50, 0xEE),   // printable  — blue
            qRgb(0xCC, 0x00, 0x30),   // other      — red/purple
        };
        for (int i = 0; i < sampleCount; ++i)
        {
            const quint8 b = rawData[i];
            int cls;
            if      (b == 0x00) cls = 0;
            else if (b == 0x09 || b == 0x0A || b == 0x0B || b == 0x0C || b == 0x0D || b == 0x20) cls = 1;
            else if (b >= 0x21 && b <= 0x7E) cls = 2;
            else    cls = 3;
            sampleColors[i] = kCls[cls];
        }
        break;
    }
    case HilbertColorMode::Magnitude:
    {
        // black → purple → cyan
        static const QRgb kStops[3] = {
            qRgb(  0,   0,   0),
            qRgb(110,   0, 160),
            qRgb(  0, 210, 220),
        };
        QRgb lut[256];
        for (int v = 0; v < 256; ++v)
            lut[v] = gradientColor(v / 255.0f, kStops, 3);
        for (int i = 0; i < sampleCount; ++i)
            sampleColors[i] = lut[rawData[i]];
        break;
    }
    case HilbertColorMode::Entropy:
    {
        // Local variability (sliding-window std dev) → dark navy → hot pink.
        // Sliding window: maintain running sum and sum-of-squares for O(n) total.
        static const QRgb kStops[3] = {
            qRgb(  0,   0,  70),   // dark navy
            qRgb( 90,   0, 170),   // deep purple
            qRgb(255,  20, 147),   // hot pink
        };
        // Max std dev of a uniform [0,255] distribution ≈ 73.6; use 80 as scale.
        constexpr double kMaxStdDev = 80.0;
        constexpr int    kHalf      = 16; // half-window radius

        double wSum = 0.0, wSumSq = 0.0;
        int    wN   = 0;

        // Seed forward half of initial window.
        for (int j = 0; j < qMin(kHalf, sampleCount); ++j)
        {
            double v = rawData[j];
            wSum += v; wSumSq += v * v; ++wN;
        }

        for (int i = 0; i < sampleCount; ++i)
        {
            // Expand right edge.
            const int rEdge = i + kHalf;
            if (rEdge < sampleCount)
            {
                double v = rawData[rEdge];
                wSum += v; wSumSq += v * v; ++wN;
            }
            // Compute std dev.
            const double mean  = wSum / wN;
            const double var   = wSumSq / wN - mean * mean;
            const float  norm  = float(std::sqrt(qMax(0.0, var)) / kMaxStdDev);
            sampleColors[i]    = gradientColor(norm, kStops, 3);
            // Contract left edge.
            const int lEdge = i - kHalf;
            if (lEdge >= 0)
            {
                double v = rawData[lEdge];
                wSum -= v; wSumSq -= v * v; --wN;
            }
        }
        break;
    }
    case HilbertColorMode::Detail:
    {
        // Absolute byte-to-byte delta → black → yellow.
        static const QRgb kStops[3] = {
            qRgb(  0,   0,   0),   // no change   — black
            qRgb(200,  80,   0),   // mid change  — amber
            qRgb(255, 240,  20),   // max change  — yellow
        };
        QRgb lut[256];
        for (int v = 0; v < 256; ++v)
            lut[v] = gradientColor(v / 255.0f, kStops, 3);
        sampleColors[0] = lut[0];
        for (int i = 1; i < sampleCount; ++i)
            sampleColors[i] = lut[std::abs(int(rawData[i]) - int(rawData[i - 1]))];
        break;
    }
    }

    // ------------------------------------------------------------------
    // Generate the Gilbert/Hilbert curve and paint pixels.
    // ------------------------------------------------------------------
    QVector<QPoint> curve;
    curve.reserve(iw * ih);
    if (iw >= ih)
        gilbertGenerate(0, 0, iw, 0, 0, ih, curve);
    else
        gilbertGenerate(0, 0, 0, ih, iw, 0, curve);

    const int  totalCells = iw * ih;
    const QRgb bgColor    = palette().base().color().rgb();

    m_hilbertCachedImage = QImage(iw, ih, QImage::Format_RGB32);
    m_hilbertCachedImage.fill(bgColor);
    QRgb *raw = reinterpret_cast<QRgb *>(m_hilbertCachedImage.bits());

    m_gilbertInverse.resize(totalCells);

    for (int i = 0; i < curve.size(); ++i)
    {
        const QPoint &pt     = curve[i];
        const int     pixIdx = pt.y() * iw + pt.x();
        m_gilbertInverse[pixIdx] = i;
        const int sampleIdx = (totalCells <= sampleCount)
            ? i
            : int(qint64(i) * sampleCount / totalCells);
        if (sampleIdx < sampleCount)
            raw[pixIdx] = sampleColors[sampleIdx];
    }
}

// Forward declaration — defined after gilbertOffsetForPixel below.
static qulonglong gilbertOffsetForPixel(int px, int py, int w, int h,
                                        const QVector<int> &inverse,
                                        int sampleCount, qulonglong fileSize);

qulonglong EntropyView::offsetForWidgetPos(int wx, int wy) const
{
    qulonglong withinScope;
    if (m_isHilbert)
    {
        const int gs   = m_hilbertGridSide;
        const int side = qMin(width(), height());
        if (side <= 0) return m_hilbertScopeStart;
        const int ox = (width()  - side) / 2;
        const int oy = (height() - side) / 2;
        const int ix = qBound(0, int(float(wx - ox) / side * gs), gs - 1);
        const int iy = qBound(0, int(float(wy - oy) / side * gs), gs - 1);
        withinScope = gilbertOffsetForPixel(ix, iy, gs, gs, m_gilbertInverse, m_hilbertSampleCount, m_fileSize);
    }
    else
    {
        withinScope = gilbertOffsetForPixel(wx, wy, width(), height(), m_gilbertInverse, m_hilbertSampleCount, m_fileSize);
    }
    return m_hilbertScopeStart + withinScope;
}

float EntropyView::byteClassAvg(int classIdx) const
{
    const int n = m_byteClassData.size() / 4;
    if (n == 0 || classIdx < 0 || classIdx > 3) return 0.0f;
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += double(m_byteClassData[i * 4 + classIdx]);
    return float(sum / n);
}

void EntropyView::setSelection(qulonglong start, qulonglong end)
{
    m_hasSelection = (end > start);
    m_selStart     = start;
    m_selEnd       = end;
    repaint();
}

void EntropyView::clearSelection()
{
    m_hasSelection = false;
    repaint();
}

float EntropyView::minEntropy() const
{
    if (m_data.isEmpty()) return 0.0f;
    return *std::min_element(m_data.cbegin(), m_data.cend());
}

float EntropyView::avgEntropy() const
{
    if (m_data.isEmpty()) return 0.0f;
    const double sum = std::accumulate(m_data.cbegin(), m_data.cend(), 0.0);
    return static_cast<float>(sum / m_data.size());
}

float EntropyView::maxEntropy() const
{
    if (m_data.isEmpty()) return 0.0f;
    return *std::max_element(m_data.cbegin(), m_data.cend());
}

QSize EntropyView::sizeHint() const        { return {200, 120}; }
QSize EntropyView::minimumSizeHint() const { return {40,  80};  }

void EntropyView::setRotated(bool rotated)
{
    if (m_rotated == rotated)
        return;
    m_rotated = rotated;
    m_hoverX  = -1;
    update();
}

float EntropyView::sampleAt(int pos, int axisLen) const
{
    if (m_data.isEmpty() || axisLen <= 1)
        return 0.0f;
    const float t    = static_cast<float>(pos) / (axisLen - 1);
    const float idx  = t * (m_data.size() - 1);
    const int   lo   = qBound(0, int(idx), m_data.size() - 1);
    const int   hi   = qMin(lo + 1, m_data.size() - 1);
    const float frac = idx - float(lo);
    return m_data[lo] * (1.0f - frac) + m_data[hi] * frac;
}

QColor EntropyView::colorForEntropy(float e)
{
    struct Stop { float pos; int r, g, b; };
    static constexpr Stop stops[] = {
        {0.00f,   0,  10,  80},
        {0.30f,   0, 100, 255},
        {0.65f, 130,   0, 200},
        {0.85f, 220,  20,  20},
        {1.00f, 255,  80, 120},
    };
    static constexpr int N = int(sizeof(stops) / sizeof(stops[0]));
    e = qBound(0.0f, e, 1.0f);
    int i = 0;
    while (i < N - 2 && stops[i + 1].pos <= e)
        ++i;
    const float span = stops[i + 1].pos - stops[i].pos;
    const float frac = (span > 0.0f) ? (e - stops[i].pos) / span : 0.0f;
    return QColor(
        qRound(stops[i].r + frac * (stops[i + 1].r - stops[i].r)),
        qRound(stops[i].g + frac * (stops[i + 1].g - stops[i].g)),
        qRound(stops[i].b + frac * (stops[i + 1].b - stops[i].b)));
}

void EntropyView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const int w = width();
    const int h = height();

    p.fillRect(rect(), palette().base());

    if (m_isBigram)
    {
        if (m_bigramImage.isNull())
        {
            p.setPen(palette().mid().color());
            p.drawText(rect(), Qt::AlignCenter, tr("No data"));
        }
        else
        {
            // Letterbox into a square to preserve the 256×256 aspect ratio
            const int   side = qMin(width(), height());
            const QRect dest((width()  - side) / 2,
                             (height() - side) / 2,
                             side, side);
            p.drawImage(dest, m_bigramImage);
        }
        return;
    }

    if (m_isHilbert || m_isGilbert)
    {
        if (m_hilbertRawData.isEmpty())
        {
            p.setPen(palette().mid().color());
            p.drawText(rect(), Qt::AlignCenter, tr("No data"));
        }
        else
        {
            if (m_isHilbert)
            {
                const QSize needed(m_hilbertGridSide, m_hilbertGridSide);
                if (m_hilbertCachedImage.size() != needed)
                    rebuildHilbertImage();
                if (!m_hilbertCachedImage.isNull())
                {
                    const int side = qMin(w, h);
                    const QRect dest((w - side) / 2, (h - side) / 2, side, side);
                    p.drawImage(dest, m_hilbertCachedImage);
                }
            }
            else // Gilbert
            {
                if (m_hilbertCachedImage.size() != size())
                    rebuildHilbertImage();
                if (!m_hilbertCachedImage.isNull())
                    p.drawImage(0, 0, m_hilbertCachedImage);
            }

            if (m_hoverX >= 0 && m_hoverY >= 0)
            {
                p.setCompositionMode(QPainter::RasterOp_SourceXorDestination);
                p.fillRect(m_hoverX - 1, m_hoverY - 1, 3, 3, Qt::white);
                p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            }
        }
        return;
    }

    if (m_isByteClass)
    {
        if (m_byteClassData.isEmpty())
        {
            p.setPen(palette().mid().color());
            p.drawText(rect(), Qt::AlignCenter, tr("No data"));
            return;
        }

        // Display order: printable → whitespace → null → other/binary
        // Data layout per window: [0]=null [1]=whitespace [2]=printable [3]=other
        static const int kOrder[4] = {2, 1, 0, 3};
        static const QColor kColors[4] = {
            QColor(0x00, 0x50, 0xEE),   // printable  — blue-green
            QColor(0x88, 0xFF, 0xFF),   // whitespace — white
            QColor(0x00, 0x00, 0x00),   // null       — black
            QColor(0xCC, 0x0,  0x30),   // other      — purple
        };

        const int numW = m_byteClassData.size() / 4;

        auto sampleClass = [&](int pos, int axisLen, int cls) -> float {
            if (numW <= 0 || axisLen <= 1) return 0.0f;
            const float fidx = float(pos) / (axisLen - 1) * (numW - 1);
            const int   lo   = qBound(0, int(fidx), numW - 1);
            const int   hi   = qMin(lo + 1, numW - 1);
            const float frac = fidx - float(lo);
            return m_byteClassData[lo * 4 + cls] * (1.0f - frac)
                 + m_byteClassData[hi * 4 + cls] * frac;
        };

        if (!m_rotated)
        {
            for (int x = 0; x < w; ++x)
            {
                int y = h;
                for (int i = 0; i < 4; ++i)
                {
                    const int segH = (i < 3)
                        ? qRound(sampleClass(x, w, kOrder[i]) * h)
                        : y;
                    if (segH > 0)
                    {
                        y -= segH;
                        p.fillRect(x, y, 1, segH, kColors[i]);
                    }
                }
            }
        }
        else
        {
            for (int y = 0; y < h; ++y)
            {
                int x = 0;
                for (int i = 0; i < 4; ++i)
                {
                    const int segW = (i < 3)
                        ? qRound(sampleClass(y, h, kOrder[i]) * w)
                        : (w - x);
                    if (segW > 0)
                        p.fillRect(x, y, segW, 1, kColors[i]);
                    x += segW;
                }
            }
        }

        if (m_hasSelection && m_fileSize > 0)
        {
            const double fs = double(m_fileSize);
            if (!m_rotated)
            {
                const int x1 = qBound(0, qRound(double(m_selStart) / fs * (w - 1)), w - 1);
                const int x2 = qMin(w, qMax(x1 + 3, qRound(double(m_selEnd) / fs * (w - 1)) + 1));
                p.fillRect(x1, 0, x2 - x1, h, QColor(255, 255, 255, 80));
                p.setCompositionMode(QPainter::RasterOp_SourceXorDestination);
                p.setPen(QPen(Qt::white, 1));
                p.drawLine(x1, 0, x1, h - 1);
                if (x2 - 1 > x1) p.drawLine(x2 - 1, 0, x2 - 1, h - 1);
                p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            }
            else
            {
                const int y1 = qBound(0, qRound(double(m_selStart) / fs * (h - 1)), h - 1);
                const int y2 = qMin(h, qMax(y1 + 3, qRound(double(m_selEnd) / fs * (h - 1)) + 1));
                p.fillRect(0, y1, w, y2 - y1, QColor(255, 255, 255, 80));
                p.setCompositionMode(QPainter::RasterOp_SourceXorDestination);
                p.setPen(QPen(Qt::white, 1));
                p.drawLine(0, y1, w - 1, y1);
                if (y2 - 1 > y1) p.drawLine(0, y2 - 1, w - 1, y2 - 1);
                p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            }
        }

        const int hoverMax = m_rotated ? h : w;
        if (m_hoverX >= 0 && m_hoverX < hoverMax)
        {
            p.setCompositionMode(QPainter::RasterOp_SourceXorDestination);
            p.setPen(QPen(Qt::white, 3));
            if (m_rotated) p.drawLine(0, m_hoverX, w, m_hoverX);
            else           p.drawLine(m_hoverX, 0, m_hoverX, h);
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        }
        return;
    }

    if (m_data.isEmpty())
    {
        p.setPen(palette().mid().color());
        p.drawText(rect(), Qt::AlignCenter, tr("No data"));
        return;
    }

    if (!m_rotated)
    {
        for (int x = 0; x < w; ++x)
        {
            const float e    = sampleAt(x, w);
            const int   barH = qRound(e * h);
            if (barH > 0)
                p.fillRect(x, h - barH, 1, barH, colorForEntropy(e));
        }
        if (m_hasSelection && m_fileSize > 0)
        {
            const double fs = double(m_fileSize);
            const int x1 = qBound(0, qRound(double(m_selStart) / fs * (w - 1)), w - 1);
            const int x2 = qMin(w, qMax(x1 + 3, qRound(double(m_selEnd) / fs * (w - 1)) + 1));
            p.fillRect(x1, 0, x2 - x1, h, QColor(255, 255, 255, 80));
            p.setCompositionMode(QPainter::RasterOp_SourceXorDestination);
            p.setPen(QPen(Qt::white, 1));
            p.drawLine(x1, 0, x1, h - 1);
            if (x2 - 1 > x1) p.drawLine(x2 - 1, 0, x2 - 1, h - 1);
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        }
        // Threshold at ~7.6 bits/byte — typical lower bound for compressed/encrypted data
        const int threshY = h - qRound(0.95f * h);
        p.setPen(QPen(QColor(200, 60, 60, 120), 1, Qt::DashLine));
        p.drawLine(0, threshY, w, threshY);
        if (m_hoverX >= 0 && m_hoverX < w)
        {
            p.setCompositionMode(QPainter::RasterOp_SourceXorDestination);
            p.setPen(QPen(Qt::white, 3));
            p.drawLine(m_hoverX, 0, m_hoverX, h);
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        }
    }
    else
    {
        for (int y = 0; y < h; ++y)
        {
            const float e    = sampleAt(y, h);
            const int   barW = qRound(e * w);
            if (barW > 0)
                p.fillRect(0, y, barW, 1, colorForEntropy(e));
        }
        if (m_hasSelection && m_fileSize > 0)
        {
            const double fs = double(m_fileSize);
            const int y1 = qBound(0, qRound(double(m_selStart) / fs * (h - 1)), h - 1);
            const int y2 = qMin(h, qMax(y1 + 3, qRound(double(m_selEnd) / fs * (h - 1)) + 1));
            p.fillRect(0, y1, w, y2 - y1, QColor(255, 255, 255, 80));
            p.setCompositionMode(QPainter::RasterOp_SourceXorDestination);
            p.setPen(QPen(Qt::white, 1));
            p.drawLine(0, y1, w - 1, y1);
            if (y2 - 1 > y1) p.drawLine(0, y2 - 1, w - 1, y2 - 1);
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        }
        const int threshX = qRound(0.95f * w);
        p.setPen(QPen(QColor(200, 60, 60, 120), 1, Qt::DashLine));
        p.drawLine(threshX, 0, threshX, h);
        if (m_hoverX >= 0 && m_hoverX < h)
        {
            p.setCompositionMode(QPainter::RasterOp_SourceXorDestination);
            p.setPen(QPen(Qt::white, 3));
            p.drawLine(0, m_hoverX, w, m_hoverX);
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        }
    }
}

static qulonglong gilbertOffsetForPixel(int px, int py, int w, int h,
                                        const QVector<int> &inverse,
                                        int sampleCount, qulonglong fileSize)
{
    if (inverse.isEmpty() || inverse.size() != w * h || sampleCount <= 0)
        return 0;
    const int curveIdx  = inverse[qBound(0, py, h-1) * w + qBound(0, px, w-1)];
    const int totalCells = w * h;
    const int sampleIdx = (totalCells <= sampleCount)
        ? curveIdx
        : int(qint64(curveIdx) * sampleCount / totalCells);
    if (fileSize <= qulonglong(sampleCount))
        return qMin(qulonglong(sampleIdx), fileSize - 1);
    return qMin(qulonglong(qint64(sampleIdx) * qint64(fileSize) / sampleCount), fileSize - 1);
}

void EntropyView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_dragging = true;
        if (m_fileSize > 0 && (!m_data.isEmpty() || !m_byteClassData.isEmpty() || m_isHilbert || m_isGilbert))
        {
            const QPoint pos = event->position().toPoint();

            if (m_isHilbert || m_isGilbert)
            {
                if (width() > 0 && height() > 0)
                    emit positionClicked(offsetForWidgetPos(pos.x(), pos.y()));
            }
            else
            {
                const int    coord   = m_rotated ? pos.y() : pos.x();
                const int    axisLen = m_rotated ? height() : width();
                if (axisLen > 1)
                {
                    const float      t      = qBound(0.0f, float(coord) / (axisLen - 1), 1.0f);
                    const qulonglong offset = static_cast<qulonglong>(t * m_fileSize);
                    if (event->modifiers() & Qt::ShiftModifier)
                        emit rangeSelected(m_dragAnchor, offset);
                    else
                    {
                        m_dragAnchor = offset;
                        emit positionClicked(offset);
                    }
                }
            }
        }
    }
    QWidget::mousePressEvent(event);
}

void EntropyView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        m_dragging = false;
    QWidget::mouseReleaseEvent(event);
}

void EntropyView::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();

    if (m_isHilbert || m_isGilbert)
    {
        m_hoverX = qBound(0, pos.x(), width()  - 1);
        m_hoverY = qBound(0, pos.y(), height() - 1);
        update();
        if (m_fileSize > 0 && width() > 0 && height() > 0)
            emit positionHovered(offsetForWidgetPos(m_hoverX, m_hoverY), 0.0f);
    }
    else
    {
        m_hoverX = m_rotated ? pos.y() : pos.x();
        update();

        if (m_fileSize > 0 && (!m_data.isEmpty() || !m_byteClassData.isEmpty()))
        {
            const int axisLen = m_rotated ? height() : width();
            if (axisLen > 1)
            {
                const float      t      = qBound(0.0f, float(m_hoverX) / (axisLen - 1), 1.0f);
                const qulonglong offset = static_cast<qulonglong>(t * m_fileSize);
                emit positionHovered(offset, sampleAt(m_hoverX, axisLen));
                if (m_dragging)
                    emit rangeSelected(m_dragAnchor, offset);
            }
        }
    }
    QWidget::mouseMoveEvent(event);
}

void EntropyView::leaveEvent(QEvent *event)
{
    m_hoverX = -1;
    m_hoverY = -1;
    update();
    emit hoverCleared();
    QWidget::leaveEvent(event);
}

// -----------------------------------------------------------------------
// Background calculation
// -----------------------------------------------------------------------

static QVector<float> calculateEntropy(
    const QString &path,
    int windowSize,
    qulonglong &outFileSize,
    const std::shared_ptr<std::atomic_bool> &cancelFlag,
    const std::shared_ptr<filestats::OperationPause> &pause,
    const std::function<void(int)> &progressCallback)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        outFileSize = 0;
        return {};
    }

    const qint64   total    = file.size();
    outFileSize             = static_cast<qulonglong>(total);
    qint64         scanned  = 0;
    int            lastProg = -1;
    QVector<float> results;
    if (total > 0 && windowSize > 0)
        results.reserve(static_cast<int>(qMin((total + windowSize - 1) / windowSize, qint64(1 << 20))));

    while (!cancelFlag->load() && !file.atEnd())
    {
        if (pause && !pause->waitIfPaused(cancelFlag))
            return {};

        const QByteArray chunk = file.read(windowSize);
        if (chunk.isEmpty())
            break;

        int freq[256] = {};
        for (unsigned char b : chunk)
            ++freq[b];

        const int n = chunk.size();
        double entropy = 0.0;
        for (int f : freq)
        {
            if (f > 0)
            {
                const double p = double(f) / n;
                entropy -= p * std::log2(p);
            }
        }
        results.append(static_cast<float>(entropy / 8.0));

        scanned += chunk.size();
        const int prog = (total > 0) ? int(1000LL * scanned / total) : 1000;
        if (prog != lastProg)
        {
            lastProg = prog;
            progressCallback(prog);
        }
    }

    return cancelFlag->load() ? QVector<float>() : results;
}

static QVector<quint64> calculateBigram(
    const QString &path,
    qulonglong startOffset,
    qulonglong byteCount,
    int stride,
    qulonglong &outFileSize,
    const std::shared_ptr<std::atomic_bool> &cancelFlag,
    const std::shared_ptr<filestats::OperationPause> &pause,
    const std::function<void(int)> &progressCallback)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        outFileSize = 0;
        return {};
    }

    const qulonglong fileTotal = static_cast<qulonglong>(file.size());
    outFileSize = fileTotal;

    startOffset = qMin(startOffset, fileTotal);
    if (byteCount == 0 || startOffset + byteCount > fileTotal)
        byteCount = fileTotal - startOffset;
    if (startOffset > 0)
        file.seek(static_cast<qint64>(startOffset));

    QVector<quint64> counts(256 * 256, 0);

    constexpr int    bufSize   = 65536;
    qint64           scanned   = 0;
    int              lastProg  = -1;
    const qint64     scanTotal = static_cast<qint64>(byteCount);
    QVector<unsigned char> tail;
    tail.reserve(stride);

    while (!cancelFlag->load() && scanned < scanTotal)
    {
        if (pause && !pause->waitIfPaused(cancelFlag))
            return {};

        const qint64     toRead = qMin(qint64(bufSize), scanTotal - scanned);
        const QByteArray chunk  = file.read(toRead);
        if (chunk.isEmpty())
            break;

        const auto *data = reinterpret_cast<const unsigned char *>(chunk.constData());
        const int   n    = chunk.size();

        // Pair tail bytes from previous chunk with the first bytes of this one
        for (int i = 0; i < int(tail.size()); ++i)
        {
            const int j = stride - int(tail.size()) + i;
            if (j < n)
                ++counts[tail[i] * 256 + data[j]];
        }

        // Pairs entirely within this chunk
        for (int i = 0; i + stride < n; ++i)
            ++counts[data[i] * 256 + data[i + stride]];

        // Save the last `stride` bytes as the tail for the next chunk
        const int tailLen = qMin(stride, n);
        tail.resize(tailLen);
        for (int i = 0; i < tailLen; ++i)
            tail[i] = data[n - tailLen + i];

        scanned += n;
        const int prog = (scanTotal > 0) ? int(1000LL * scanned / scanTotal) : 1000;
        if (prog != lastProg)
        {
            lastProg = prog;
            progressCallback(prog);
        }
    }

    return cancelFlag->load() ? QVector<quint64>() : counts;
}

static QVector<float> calculateByteClass(
    const QString &path,
    int windowSize,
    qulonglong &outFileSize,
    const std::shared_ptr<std::atomic_bool> &cancelFlag,
    const std::shared_ptr<filestats::OperationPause> &pause,
    const std::function<void(int)> &progressCallback)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        outFileSize = 0;
        return {};
    }

    const qint64 total   = file.size();
    outFileSize          = static_cast<qulonglong>(total);
    qint64 scanned       = 0;
    int    lastProg      = -1;
    QVector<float> results;
    if (total > 0 && windowSize > 0)
        results.reserve(4 * static_cast<int>(qMin((total + windowSize - 1) / windowSize, qint64(1 << 20))));

    while (!cancelFlag->load() && !file.atEnd())
    {
        if (pause && !pause->waitIfPaused(cancelFlag))
            return {};

        const QByteArray chunk = file.read(windowSize);
        if (chunk.isEmpty())
            break;

        // Buckets: [0]=null(0x00), [1]=whitespace(tab/LF/CR/space), [2]=printable(0x21-0x7E), [3]=other(control+high)
        int counts[4] = {};
        for (unsigned char b : chunk)
        {
            if (b == 0x00)
                ++counts[0];
            else if (b == 0x09 || b == 0x0A || b == 0x0B || b == 0x0C || b == 0x0D || b == 0x20)
                ++counts[1];
            else if (b >= 0x21 && b <= 0x7E)
                ++counts[2];
            else
                ++counts[3];
        }
        const float n = float(chunk.size());
        results.append(counts[0] / n);
        results.append(counts[1] / n);
        results.append(counts[2] / n);
        results.append(counts[3] / n);

        scanned += chunk.size();
        const int prog = (total > 0) ? int(1000LL * scanned / total) : 1000;
        if (prog != lastProg)
        {
            lastProg = prog;
            progressCallback(prog);
        }
    }

    if (cancelFlag->load())
        return {};

    // 3-tap Gaussian smooth [0.25, 0.5, 0.25] per class along the window axis
    const int nW = results.size() / 4;
    if (nW >= 3)
    {
        QVector<float> smoothed(results.size());
        for (int i = 0; i < nW; ++i)
        {
            const int prev = qMax(0, i - 1);
            const int next = qMin(nW - 1, i + 1);
            for (int c = 0; c < 4; ++c)
                smoothed[i * 4 + c] = 0.25f * results[prev * 4 + c]
                                    + 0.50f * results[i    * 4 + c]
                                    + 0.25f * results[next * 4 + c];
        }
        results = std::move(smoothed);
    }

    return results;
}

static QVector<quint8> calculateHilbert(
    const QString &path,
    qulonglong startOffset,
    qulonglong byteCount,       // 0 = whole file from startOffset
    qulonglong &outScopeSize,   // bytes actually scanned
    int &outSampleCount,
    int maxSamples,
    const std::shared_ptr<std::atomic_bool> &cancelFlag,
    const std::shared_ptr<filestats::OperationPause> &pause,
    const std::function<void(int)> &progressCallback)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        outScopeSize   = 0;
        outSampleCount = 0;
        return {};
    }

    const qulonglong fileTotal = static_cast<qulonglong>(file.size());
    startOffset = qMin(startOffset, fileTotal);
    if (byteCount == 0 || startOffset + byteCount > fileTotal)
        byteCount = fileTotal - startOffset;

    outScopeSize = byteCount;

    if (byteCount == 0)
    {
        outSampleCount = 0;
        return {};
    }

    if (startOffset > 0)
        file.seek(static_cast<qint64>(startOffset));

    const qint64 total        = static_cast<qint64>(byteCount);
    const int    sampleCount  = static_cast<int>(qMin(qint64(maxSamples), total));
    outSampleCount            = sampleCount;

    QVector<quint8> result(sampleCount, 0);

    constexpr int bufSize  = 65536;
    qint64        scanned  = 0;
    int           lastProg = -1;

    while (!cancelFlag->load() && scanned < total)
    {
        if (pause && !pause->waitIfPaused(cancelFlag))
            return {};

        const qint64     toRead = qMin(qint64(bufSize), total - scanned);
        const QByteArray chunk  = file.read(toRead);
        if (chunk.isEmpty())
            break;

        const auto *data = reinterpret_cast<const unsigned char *>(chunk.constData());
        const int   n    = chunk.size();

        for (int i = 0; i < n; ++i)
        {
            const qint64 pos = scanned + i;
            const int sampleIdx = (total <= qint64(maxSamples))
                ? int(pos)
                : int(double(pos) / double(total) * sampleCount);
            if (sampleIdx >= sampleCount)
                continue;
            result[sampleIdx] = data[i];
        }

        scanned += n;
        const int prog = int(1000LL * scanned / total);
        if (prog != lastProg)
        {
            lastProg = prog;
            progressCallback(prog);
        }
    }

    return cancelFlag->load() ? QVector<quint8>() : result;
}

// -----------------------------------------------------------------------
// FilePropertiesPanel — entropy methods
// -----------------------------------------------------------------------

void FilePropertiesPanel::updateHilbertZoomButton()
{
    if (!m_hilbertZoomButton)
        return;
    const bool hasSel  = m_hexView && (m_hexView->selectionEnd() > m_hexView->selectionStart());
    const bool isZoomed = m_hilbertScopeLength > 0;
    if (hasSel)
    {
        m_hilbertZoomButton->setIcon(recoloredIcon(QStringLiteral("actions/zoom-in"),
                                                   palette().buttonText().color(), 16));
        m_hilbertZoomButton->setToolTip(tr("Re-scan selected range at full grid resolution"));
        m_hilbertZoomButton->setEnabled(true);
    }
    else if (isZoomed)
    {
        m_hilbertZoomButton->setIcon(recoloredIcon(QStringLiteral("actions/zoom-restore"),
                                                   palette().buttonText().color(), 16));
        m_hilbertZoomButton->setToolTip(tr("Re-scan entire file"));
        m_hilbertZoomButton->setEnabled(true);
    }
    else
    {
        m_hilbertZoomButton->setIcon(recoloredIcon(QStringLiteral("actions/zoom-in"),
                                                   palette().buttonText().color(), 16));
        m_hilbertZoomButton->setToolTip(tr("Re-scan selected range at full grid resolution"));
        m_hilbertZoomButton->setEnabled(false);
    }
}

void FilePropertiesPanel::maybeStartEntropyAnalysis()
{
    if (!m_panelFullyOpened)
        return;
    if (isSectionCollapsed(SectionId::Entropy))
        return;
    if (m_entropyState.started)
        return;
    if (m_entropyState.rescanRequired)
    {
        if (m_entropyOperation)
            m_entropyOperation->showRescan(m_entropyState.rescanMessage.isEmpty()
                                               ? tr("File contents changed")
                                               : m_entropyState.rescanMessage);
        requestSectionLayoutRefresh(SectionId::Entropy);
        return;
    }
    if (m_entropyState.autoStartConsumed)
        return;
    if (!shouldAutoStartOperations())
    {
        if (m_entropyOperation && !m_entropyOperation->hasOperation())
            m_entropyOperation->showStart(tr("Calculate entropy"));
        return;
    }
    m_entropyState.started = true;
    startEntropyAnalysis();
}

void FilePropertiesPanel::startEntropyAnalysis()
{
    if (!m_hexView)
    {
        m_entropyState.started          = false;
        m_entropyState.pausedByCollapse = false;
        return;
    }

    m_entropyState.autoStartConsumed = true;
    m_entropyState.rescanRequired    = false;
    m_entropyState.rescanMessage.clear();
    m_entropyState.pausedByCollapse = isSectionCollapsed(SectionId::Entropy);
    const int generation = ++m_entropyState.generation;

    if (m_entropyState.cancel) m_entropyState.cancel->store(true);
    if (m_entropyState.pause)  m_entropyState.pause->wake();

    auto cancelFlag = std::make_shared<std::atomic_bool>(false);
    m_entropyState.cancel = cancelFlag;
    auto pause = std::make_shared<filestats::OperationPause>();
    pause->setPaused(isSectionCollapsed(SectionId::Entropy));
    m_entropyState.pause = pause;

    if (m_entropyView)       m_entropyView->clear();
    if (m_entropyStatsLabel) m_entropyStatsLabel->clear();
    setEntropyProgressTitle(0);
    if (m_entropyOperation)  m_entropyOperation->showProgress();
    requestSectionLayoutRefresh(SectionId::Entropy);

    const QString     path              = m_hexView->filePath();
    const int         windowSize        = m_entropyWindowSize;
    const EntropyMode mode              = m_entropyMode;
    const int         stride            = m_bigramStride;
    const int         gridSide          = m_hilbertGridSide;
    const qulonglong  hilbertScopeStart = m_hilbertScopeStart;
    const qulonglong  hilbertScopeLen   = m_hilbertScopeLength;

    qulonglong scopeStart  = 0;
    qulonglong scopeLength = 0; // 0 = whole file
    if (mode == EntropyMode::Bigram && m_hexView)
    {
        const auto ss = static_cast<qulonglong>(m_hexView->selectionStart());
        const auto se = static_cast<qulonglong>(m_hexView->selectionEnd());
        if (se > ss) { scopeStart = ss; scopeLength = se - ss; }
    }

    QPointer<FilePropertiesPanel> guard(this);

    auto *thread = QThread::create([guard, generation, path, windowSize, mode,
                                    scopeStart, scopeLength, stride, gridSide,
                                    hilbertScopeStart, hilbertScopeLen,
                                    cancelFlag, pause]()
    {
        auto progressCb = [guard, generation](int prog) {
            QMetaObject::invokeMethod(qApp, [guard, generation, prog]() {
                if (guard) guard->updateEntropyProgress(generation, prog);
            }, Qt::QueuedConnection);
        };

        qulonglong fileSize = 0;
        if (mode == EntropyMode::Bigram)
        {
            const QVector<quint64> counts = calculateBigram(
                path, scopeStart, scopeLength, stride, fileSize, cancelFlag, pause, progressCb);
            if (cancelFlag->load())
                return;
            QMetaObject::invokeMethod(qApp, [guard, generation, counts, fileSize]() {
                if (guard) guard->applyBigramResults(generation, counts, fileSize);
            }, Qt::QueuedConnection);
        }
        else if (mode == EntropyMode::ByteClass)
        {
            const QVector<float> results = calculateByteClass(path, windowSize, fileSize, cancelFlag, pause, progressCb);
            if (cancelFlag->load())
                return;
            QMetaObject::invokeMethod(qApp, [guard, generation, results, fileSize, windowSize]() {
                if (guard) guard->applyByteClassResults(generation, results, fileSize, windowSize);
            }, Qt::QueuedConnection);
        }
        else if (mode == EntropyMode::Hilbert)
        {
            int        sampleCount = 0;
            qulonglong scopeSize   = 0;
            const QVector<quint8> bytes = calculateHilbert(path,
                hilbertScopeStart, hilbertScopeLen, scopeSize, sampleCount,
                gridSide * gridSide, cancelFlag, pause, progressCb);
            if (cancelFlag->load()) return;
            QMetaObject::invokeMethod(qApp,
                [guard, generation, bytes, scopeSize, sampleCount, gridSide, hilbertScopeStart]() {
                    if (guard) guard->applyHilbertResults(generation, bytes, scopeSize, sampleCount,
                                                          gridSide, hilbertScopeStart);
                }, Qt::QueuedConnection);
        }
        else if (mode == EntropyMode::Gilbert)
        {
            int        sampleCount = 0;
            qulonglong scopeSize   = 0;
            const QVector<quint8> bytes = calculateHilbert(path,
                hilbertScopeStart, hilbertScopeLen, scopeSize, sampleCount,
                gridSide * gridSide, cancelFlag, pause, progressCb);
            if (cancelFlag->load()) return;
            QMetaObject::invokeMethod(qApp,
                [guard, generation, bytes, scopeSize, sampleCount, hilbertScopeStart]() {
                    if (guard) guard->applyGilbertResults(generation, bytes, scopeSize, sampleCount,
                                                          hilbertScopeStart);
                }, Qt::QueuedConnection);
        }
        else
        {
            const QVector<float> results = calculateEntropy(path, windowSize, fileSize, cancelFlag, pause, progressCb);
            if (cancelFlag->load())
                return;
            QMetaObject::invokeMethod(qApp, [guard, generation, results, fileSize, windowSize]() {
                if (guard) guard->applyEntropyResults(generation, results, fileSize, windowSize);
            }, Qt::QueuedConnection);
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void FilePropertiesPanel::cancelEntropyAnalysis()
{
    ++m_entropyState.generation;
    m_entropyState.started          = false;
    m_entropyState.pausedByCollapse = false;
    if (m_entropyState.cancel) m_entropyState.cancel->store(true);
    if (m_entropyState.pause)  m_entropyState.pause->wake();
    if (m_entropyOperation)
        m_entropyOperation->showRetry(tr("Operation cancelled"));
    resetEntropyTitle();
    requestSectionLayoutRefresh(SectionId::Entropy);
}

void FilePropertiesPanel::resumeEntropyAnalysis()
{
    if (!m_entropyState.pause)
        return;
    m_entropyState.pause->setPaused(false);
    m_entropyState.pausedByCollapse = false;
    if (m_entropyOperation)
        m_entropyOperation->setProgressActionStop();
    setEntropyProgressTitle(m_entropyState.progress);
}

void FilePropertiesPanel::updateEntropyProgress(int generation, int value)
{
    if (generation != m_entropyState.generation)
        return;
    m_entropyState.progress = value;
    setEntropyProgressTitle(value);
    if (m_entropyOperation && m_entropyOperation->progressBar())
        m_entropyOperation->progressBar()->setValue(value);
}

void FilePropertiesPanel::applyEntropyResults(int generation, QVector<float> data,
                                              qulonglong fileSize, int windowSize)
{
    if (generation != m_entropyState.generation)
        return;
    if (m_entropyView)
        m_entropyView->setData(data, fileSize, windowSize);
    updateEntropyStatsLabel();
    if (m_entropyOperation) m_entropyOperation->clear();
    resetEntropyTitle();
    requestSectionLayoutRefresh(SectionId::Entropy);
    QTimer::singleShot(0, this, [this]() { repairExpandedSectionGeometry(SectionId::Entropy); });
}

void FilePropertiesPanel::applyBigramResults(int generation, QVector<quint64> counts, qulonglong fileSize)
{
    if (generation != m_entropyState.generation)
        return;
    if (m_entropyView)
        m_entropyView->setBigramData(counts, fileSize);
    updateEntropyStatsLabel();
    if (m_entropyOperation) m_entropyOperation->clear();
    resetEntropyTitle();
    requestSectionLayoutRefresh(SectionId::Entropy);
    QTimer::singleShot(0, this, [this]() { repairExpandedSectionGeometry(SectionId::Entropy); });
}

void FilePropertiesPanel::applyByteClassResults(int generation, QVector<float> data,
                                                qulonglong fileSize, int windowSize)
{
    if (generation != m_entropyState.generation)
        return;
    if (m_entropyView)
        m_entropyView->setByteClassData(data, fileSize, windowSize);
    updateEntropyStatsLabel();
    if (m_entropyOperation) m_entropyOperation->clear();
    resetEntropyTitle();
    requestSectionLayoutRefresh(SectionId::Entropy);
    QTimer::singleShot(0, this, [this]() { repairExpandedSectionGeometry(SectionId::Entropy); });
}

void FilePropertiesPanel::applyHilbertResults(int generation, QVector<quint8> bytes,
                                              qulonglong scopeSize, int sampleCount, int gridSide,
                                              qulonglong scopeStart)
{
    if (generation != m_entropyState.generation)
        return;
    if (m_entropyView)
        m_entropyView->setHilbertData(bytes, scopeSize, sampleCount, gridSide, scopeStart);
    updateEntropyStatsLabel();
    if (m_entropyOperation) m_entropyOperation->clear();
    resetEntropyTitle();
    requestSectionLayoutRefresh(SectionId::Entropy);
    QTimer::singleShot(0, this, [this]() { repairExpandedSectionGeometry(SectionId::Entropy); });
}

void FilePropertiesPanel::applyGilbertResults(int generation, QVector<quint8> bytes,
                                              qulonglong scopeSize, int sampleCount,
                                              qulonglong scopeStart)
{
    if (generation != m_entropyState.generation)
        return;
    if (m_entropyView)
        m_entropyView->setGilbertData(bytes, scopeSize, sampleCount, scopeStart);
    updateEntropyStatsLabel();
    if (m_entropyOperation) m_entropyOperation->clear();
    resetEntropyTitle();
    requestSectionLayoutRefresh(SectionId::Entropy);
    QTimer::singleShot(0, this, [this]() { repairExpandedSectionGeometry(SectionId::Entropy); });
}

void FilePropertiesPanel::markEntropyContentsChanged()
{
    m_entropyState.started          = false;
    m_entropyState.pausedByCollapse = false;
    ++m_entropyState.generation;
    if (m_entropyState.cancel) m_entropyState.cancel->store(true);
    if (m_entropyState.pause)  m_entropyState.pause->wake();
    if (m_entropyView)         m_entropyView->clear();
    if (m_entropyStatsLabel)   m_entropyStatsLabel->clear();
    m_entropyState.rescanRequired = true;
    m_entropyState.rescanMessage  = tr("File contents changed");
    if (m_entropyOperation)
        m_entropyOperation->showRescan(m_entropyState.rescanMessage);
    resetEntropyTitle();
    requestSectionLayoutRefresh(SectionId::Entropy);
}

void FilePropertiesPanel::resetEntropyForCurrentDocument()
{
    m_entropyState.started           = false;
    m_entropyState.pausedByCollapse  = false;
    m_entropyState.autoStartConsumed = false;
    ++m_entropyState.generation;
    if (m_entropyState.cancel) m_entropyState.cancel->store(true);
    if (m_entropyState.pause)  m_entropyState.pause->wake();
    if (m_entropyView)         m_entropyView->clear();
    if (m_entropyStatsLabel)   m_entropyStatsLabel->clear();
    m_entropyState.rescanRequired = false;
    m_entropyState.rescanMessage.clear();
    if (m_entropyOperation)    m_entropyOperation->clear();
    m_hilbertScopeStart  = 0;
    m_hilbertScopeLength = 0;
    updateHilbertZoomButton();
    resetEntropyTitle();
    requestSectionLayoutRefresh(SectionId::Entropy);
}

void FilePropertiesPanel::setEntropyProgressTitle(int value)
{
    if (PanelSection *s = sectionFor(SectionId::Entropy))
        if (s->header)
            s->header->setTitle(tr("Entropy (%1%)").arg(value / 10));
}

void FilePropertiesPanel::resetEntropyTitle()
{
    if (PanelSection *s = sectionFor(SectionId::Entropy))
        if (s->header)
            s->header->setTitle(tr("Entropy"));
}

void FilePropertiesPanel::updateEntropyStatsLabel()
{
    if (!m_entropyStatsLabel || !m_entropyView)
    {
        if (m_entropyStatsLabel) m_entropyStatsLabel->clear();
        return;
    }
    if (m_entropyView->isBigram())
    {
        m_entropyStatsLabel->clear();
        return;
    }
    if (m_entropyView->isHilbert() || m_entropyView->isGilbert())
    {
        if (m_hilbertScopeLength > 0)
            m_entropyStatsLabel->setText(
                tr("Zoomed: 0x%1 – 0x%2")
                    .arg(m_hilbertScopeStart, 0, 16)
                    .arg(m_hilbertScopeStart + m_hilbertScopeLength - 1, 0, 16));
        else
            m_entropyStatsLabel->clear();
        return;
    }
    if (m_entropyView->isByteClass())
    {
        if (m_entropyView->byteClassData().isEmpty())
            m_entropyStatsLabel->clear();
        else
            m_entropyStatsLabel->setText(
                tr("Printable %1%   Space %2%   Null %3%   Binary %4%")
                    .arg(qRound(m_entropyView->byteClassAvg(2) * 100))
                    .arg(qRound(m_entropyView->byteClassAvg(1) * 100))
                    .arg(qRound(m_entropyView->byteClassAvg(0) * 100))
                    .arg(qRound(m_entropyView->byteClassAvg(3) * 100)));
        return;
    }
    if (m_entropyView->data().isEmpty())
    {
        m_entropyStatsLabel->clear();
        return;
    }
    const auto fmt = [](float e) {
        return QStringLiteral("%1").arg(double(e * 8.0), 0, 'f', 2);
    };
    m_entropyStatsLabel->setText(
        tr("Min %1   Avg %2   Max %3  bits/byte")
            .arg(fmt(m_entropyView->minEntropy()))
            .arg(fmt(m_entropyView->avgEntropy()))
            .arg(fmt(m_entropyView->maxEntropy())));
}
