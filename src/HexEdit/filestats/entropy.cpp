#include "filestats/entropy.h"
#include "filestats/sidepanel.h"
#include "filestats/widgets.h"
#include "HexView/hexview.h"
#include "combos/menucombobox.h"
#include "settings/settingscard.h"
#include "theme.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QMenu>
#include <QSizePolicy>
#include <QVBoxLayout>
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

void EntropyView::setData(const QVector<float> &data, qulonglong scopeSize, int windowSize, qulonglong scopeStart)
{
    m_data        = data;
    m_fileSize    = scopeSize;
    m_windowSize  = windowSize;
    m_scopeStart  = scopeStart;
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
    m_scopeStart         = 0;
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

void EntropyView::setByteClassData(const QVector<float> &data, qulonglong scopeSize, int windowSize, qulonglong scopeStart)
{
    m_byteClassData = data;
    m_fileSize      = scopeSize;
    m_windowSize    = windowSize;
    m_scopeStart    = scopeStart;
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
    m_scopeStart         = scopeStart;
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
    m_scopeStart         = scopeStart;
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
        if (side <= 0) return m_scopeStart;
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
    return m_scopeStart + withinScope;
}

void EntropyView::setByteClassScheme(ByteClassScheme scheme)
{
    m_byteClassScheme = scheme;
    update();
}

float EntropyView::byteClassAvg(int classIdx) const
{
    const int n          = m_byteClassData.size() / 16;
    const int schemeBase = int(m_byteClassScheme) * 4;
    if (n == 0 || classIdx < 0 || classIdx > 3) return 0.0f;
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += double(m_byteClassData[i * 16 + schemeBase + classIdx]);
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
    if (!m_hasSelection)
        return;
    m_hasSelection = false;
    repaint();
    emit selectionCleared();
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

        // Per-scheme display: order[i] = class drawn at position i (bottom-first in rotated mode)
        // colors[i] pairs with order[i]. Data layout: 16 floats/window [s0c0..c3, s1c0..c3, s2c0..c3, s3c0..c3]
        // Shared palette — same as Hilbert/Gilbert ByteClass mode for visual consistency
        static const QColor kNull (0x00, 0x00, 0x00);   // black      — null / zero / sparse / low
        static const QColor kSpace(0x88, 0xFF, 0xFF);   // cyan       — whitespace / moderate-high
        static const QColor kPrint(0x00, 0x50, 0xEE);   // blue       — printable / structured
        static const QColor kOther(0xCC, 0x00, 0x30);   // red/purple — binary / high / dense

        struct SchemeDisplay { int order[4]; QColor colors[4]; };
        static const SchemeDisplay kSchemes[] = {
            // Semantic     — class layout: [0]=null [1]=whitespace [2]=printable [3]=other
            //   draw bottom→top: printable | whitespace | null | other
            {{2,1,0,3}, {kPrint, kSpace, kNull, kOther}},
            // ASCII Range  — class layout: [0]=control(00-1F) [1]=printable(20-7E) [2]=DEL(7F) [3]=high(80-FF)
            //   draw bottom→top: printable | control | high | DEL
            {{1,0,3,2}, {kPrint, kNull, kOther, kSpace}},
            // Bit Density  — class layout: [0]=0-2 bits [1]=3-4 bits [2]=5-6 bits [3]=7-8 bits
            //   draw bottom→top: sparse | med-lo | med-hi | dense
            {{0,1,2,3}, {kNull, kPrint, kSpace, kOther}},
            // Nibble Range — class layout: [0]=00-3F [1]=40-7F [2]=80-BF [3]=C0-FF
            //   draw bottom→top: low | mid-lo | mid-hi | high
            {{0,1,2,3}, {kNull, kPrint, kSpace, kOther}},
        };
        const SchemeDisplay &sd = kSchemes[int(m_byteClassScheme)];

        const int numW       = m_byteClassData.size() / 16;
        const int schemeBase = int(m_byteClassScheme) * 4;

        auto sampleClass = [&](int pos, int axisLen, int cls) -> float {
            if (numW <= 0 || axisLen <= 1) return 0.0f;
            const float fidx = float(pos) / (axisLen - 1) * (numW - 1);
            const int   lo   = qBound(0, int(fidx), numW - 1);
            const int   hi   = qMin(lo + 1, numW - 1);
            const float frac = fidx - float(lo);
            return m_byteClassData[lo * 16 + schemeBase + cls] * (1.0f - frac)
                 + m_byteClassData[hi * 16 + schemeBase + cls] * frac;
        };

        if (!m_rotated)
        {
            for (int x = 0; x < w; ++x)
            {
                int y = h;
                for (int i = 0; i < 4; ++i)
                {
                    const int segH = (i < 3)
                        ? qRound(sampleClass(x, w, sd.order[i]) * h)
                        : y;
                    if (segH > 0)
                    {
                        y -= segH;
                        p.fillRect(x, y, 1, segH, sd.colors[i]);
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
                        ? qRound(sampleClass(y, h, sd.order[i]) * w)
                        : (w - x);
                    if (segW > 0)
                        p.fillRect(x, y, segW, 1, sd.colors[i]);
                    x += segW;
                }
            }
        }

        if (m_hasSelection && m_fileSize > 0)
        {
            const double     fs       = double(m_fileSize);
            const qulonglong relStart = (m_selStart > m_scopeStart) ? m_selStart - m_scopeStart : 0;
            const qulonglong relEnd   = (m_selEnd   > m_scopeStart) ? m_selEnd   - m_scopeStart : 0;
            const double     rs       = double(qMin(relStart, m_fileSize));
            const double     re       = double(qMin(relEnd,   m_fileSize));
            if (!m_rotated)
            {
                const int x1 = qBound(0, qRound(rs / fs * (w - 1)), w - 1);
                const int x2 = qMin(w, qMax(x1 + 3, qRound(re / fs * (w - 1)) + 1));
                p.fillRect(x1, 0, x2 - x1, h, QColor(255, 255, 255, 80));
                p.setCompositionMode(QPainter::RasterOp_SourceXorDestination);
                p.setPen(QPen(Qt::white, 1));
                p.drawLine(x1, 0, x1, h - 1);
                if (x2 - 1 > x1) p.drawLine(x2 - 1, 0, x2 - 1, h - 1);
                p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            }
            else
            {
                const int y1 = qBound(0, qRound(rs / fs * (h - 1)), h - 1);
                const int y2 = qMin(h, qMax(y1 + 3, qRound(re / fs * (h - 1)) + 1));
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
            const double     fs       = double(m_fileSize);
            const qulonglong relStart = (m_selStart > m_scopeStart) ? m_selStart - m_scopeStart : 0;
            const qulonglong relEnd   = (m_selEnd   > m_scopeStart) ? m_selEnd   - m_scopeStart : 0;
            const int x1 = qBound(0, qRound(double(qMin(relStart, m_fileSize)) / fs * (w - 1)), w - 1);
            const int x2 = qMin(w, qMax(x1 + 3, qRound(double(qMin(relEnd, m_fileSize)) / fs * (w - 1)) + 1));
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
            const double     fs       = double(m_fileSize);
            const qulonglong relStart = (m_selStart > m_scopeStart) ? m_selStart - m_scopeStart : 0;
            const qulonglong relEnd   = (m_selEnd   > m_scopeStart) ? m_selEnd   - m_scopeStart : 0;
            const int y1 = qBound(0, qRound(double(qMin(relStart, m_fileSize)) / fs * (h - 1)), h - 1);
            const int y2 = qMin(h, qMax(y1 + 3, qRound(double(qMin(relEnd, m_fileSize)) / fs * (h - 1)) + 1));
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
                {
                    if (!(event->modifiers() & Qt::ShiftModifier))
                        clearSelection();
                    emit positionClicked(offsetForWidgetPos(pos.x(), pos.y()));
                }
            }
            else
            {
                const int    coord   = m_rotated ? pos.y() : pos.x();
                const int    axisLen = m_rotated ? height() : width();
                if (axisLen > 1)
                {
                    const float      t      = qBound(0.0f, float(coord) / (axisLen - 1), 1.0f);
                    const qulonglong offset = m_scopeStart + static_cast<qulonglong>(t * m_fileSize);
                    if (event->modifiers() & Qt::ShiftModifier)
                        emit rangeSelected(m_dragAnchor, offset);
                    else
                    {
                        clearSelection();
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
                const qulonglong offset = m_scopeStart + static_cast<qulonglong>(t * m_fileSize);
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
    qulonglong startOffset,
    qulonglong byteCount,
    qulonglong &outScopeSize,
    const std::shared_ptr<std::atomic_bool> &cancelFlag,
    const std::shared_ptr<filestats::OperationPause> &pause,
    const std::function<void(int)> &progressCallback)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        outScopeSize = 0;
        return {};
    }

    const qulonglong fileTotal = static_cast<qulonglong>(file.size());
    startOffset = qMin(startOffset, fileTotal);
    if (byteCount == 0 || startOffset + byteCount > fileTotal)
        byteCount = fileTotal - startOffset;
    outScopeSize = byteCount;
    if (startOffset > 0)
        file.seek(static_cast<qint64>(startOffset));

    const qint64   scanTotal = static_cast<qint64>(byteCount);
    qint64         scanned   = 0;
    int            lastProg  = -1;
    QVector<float> results;
    if (scanTotal > 0 && windowSize > 0)
        results.reserve(static_cast<int>(qMin((scanTotal + windowSize - 1) / windowSize, qint64(1 << 20))));

    while (!cancelFlag->load() && scanned < scanTotal)
    {
        if (pause && !pause->waitIfPaused(cancelFlag))
            return {};

        const qint64     toRead = qMin(qint64(windowSize), scanTotal - scanned);
        const QByteArray chunk  = file.read(toRead);
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
        const int prog = (scanTotal > 0) ? int(1000LL * scanned / scanTotal) : 1000;
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
    qulonglong startOffset,
    qulonglong byteCount,
    qulonglong &outScopeSize,
    const std::shared_ptr<std::atomic_bool> &cancelFlag,
    const std::shared_ptr<filestats::OperationPause> &pause,
    const std::function<void(int)> &progressCallback)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        outScopeSize = 0;
        return {};
    }

    const qulonglong fileTotal = static_cast<qulonglong>(file.size());
    startOffset = qMin(startOffset, fileTotal);
    if (byteCount == 0 || startOffset + byteCount > fileTotal)
        byteCount = fileTotal - startOffset;
    outScopeSize = byteCount;
    if (startOffset > 0)
        file.seek(static_cast<qint64>(startOffset));

    // Build per-byte scheme lookup tables (256 entries each, computed once).
    // Layout per window in results: 16 floats — [scheme0_c0..c3, scheme1_c0..c3, scheme2_c0..c3, scheme3_c0..c3]
    // Scheme 0 — Semantic:    null | whitespace | printable ASCII | control+high
    // Scheme 1 — ASCII Range: 0x00-0x1F control | 0x20-0x7E printable | 0x7F DEL | 0x80-0xFF high
    // Scheme 2 — Bit Density: 0-2 bits set | 3-4 bits | 5-6 bits | 7-8 bits
    // Scheme 3 — Nibble Range: 0x00-0x3F | 0x40-0x7F | 0x80-0xBF | 0xC0-0xFF
    static const int kNumSchemes = 4;
    uint8_t lut[kNumSchemes][256];
    for (int b = 0; b < 256; ++b)
    {
        const auto u = static_cast<unsigned char>(b);
        lut[0][b] = (u == 0x00) ? 0
                  : (u == 0x09 || u == 0x0A || u == 0x0B || u == 0x0C || u == 0x0D || u == 0x20) ? 1
                  : (u >= 0x21 && u <= 0x7E) ? 2 : 3;
        lut[1][b] = (u < 0x20) ? 0 : (u <= 0x7E) ? 1 : (u == 0x7F) ? 2 : 3;
        int bits = 0; for (int x = u; x; x &= x - 1) ++bits;
        lut[2][b] = static_cast<uint8_t>(bits <= 2 ? 0 : bits <= 4 ? 1 : bits <= 6 ? 2 : 3);
        lut[3][b] = (u < 0x40) ? 0 : (u < 0x80) ? 1 : (u < 0xC0) ? 2 : 3;
    }

    const qint64 scanTotal = static_cast<qint64>(byteCount);
    qint64 scanned         = 0;
    int    lastProg        = -1;
    QVector<float> results;
    if (scanTotal > 0 && windowSize > 0)
        results.reserve(16 * static_cast<int>(qMin((scanTotal + windowSize - 1) / windowSize, qint64(1 << 20))));

    while (!cancelFlag->load() && scanned < scanTotal)
    {
        if (pause && !pause->waitIfPaused(cancelFlag))
            return {};

        const qint64     toRead = qMin(qint64(windowSize), scanTotal - scanned);
        const QByteArray chunk  = file.read(toRead);
        if (chunk.isEmpty())
            break;

        int counts[kNumSchemes][4] = {};
        for (unsigned char b : chunk)
            for (int s = 0; s < kNumSchemes; ++s)
                ++counts[s][lut[s][b]];

        const float n = float(chunk.size());
        for (int s = 0; s < kNumSchemes; ++s)
            for (int c = 0; c < 4; ++c)
                results.append(counts[s][c] / n);

        scanned += chunk.size();
        const int prog = (scanTotal > 0) ? int(1000LL * scanned / scanTotal) : 1000;
        if (prog != lastProg)
        {
            lastProg = prog;
            progressCallback(prog);
        }
    }

    if (cancelFlag->load())
        return {};

    // 3-tap Gaussian smooth [0.25, 0.5, 0.25] per value along the window axis
    const int nW = results.size() / 16;
    if (nW >= 3)
    {
        QVector<float> smoothed(results.size());
        for (int i = 0; i < nW; ++i)
        {
            const int prev = qMax(0, i - 1);
            const int next = qMin(nW - 1, i + 1);
            for (int v = 0; v < 16; ++v)
                smoothed[i * 16 + v] = 0.25f * results[prev * 16 + v]
                                     + 0.50f * results[i    * 16 + v]
                                     + 0.25f * results[next * 16 + v];
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

void FilePropertiesPanel::updateZoomButton()
{
    if (!m_hilbertZoomButton)
        return;
    const bool hasSel   = m_entropyView && m_entropyView->hasSelection();
    const bool isZoomed = m_entropyScopeLength > 0;
    // True when the active selection is exactly what we're already zoomed into —
    // re-zooming would be a no-op, so treat it as "restore" instead.
    const bool selMatchesScope = isZoomed && m_hexView
        && m_hexView->selectionEnd() > m_hexView->selectionStart()
        && static_cast<qulonglong>(m_hexView->selectionStart()) == m_entropyScopeStart
        && (static_cast<qulonglong>(m_hexView->selectionEnd())
            - static_cast<qulonglong>(m_hexView->selectionStart())) == m_entropyScopeLength;
    if (hasSel && !selMatchesScope)
    {
        m_hilbertZoomButton->setIcon(recoloredIcon(QStringLiteral("actions/zoom-in"),
                                                   palette().buttonText().color(), 16));
        m_hilbertZoomButton->setToolTip(tr("Re-scan selected range at full resolution"));
        m_hilbertZoomButton->setEnabled(true);
        m_hilbertZoomButton->show();
    }
    else if (isZoomed)
    {
        m_hilbertZoomButton->setIcon(recoloredIcon(QStringLiteral("actions/zoom-restore"),
                                                   palette().buttonText().color(), 16));
        m_hilbertZoomButton->setToolTip(tr("Re-scan entire file"));
        m_hilbertZoomButton->setEnabled(true);
        m_hilbertZoomButton->show();
    }
    else
    {
        m_hilbertZoomButton->setIcon(recoloredIcon(QStringLiteral("actions/zoom-in"),
                                                   palette().buttonText().color(), 16));
        m_hilbertZoomButton->setToolTip(tr("Select a range in the map or hex view to zoom in"));
        m_hilbertZoomButton->setEnabled(false);
        m_hilbertZoomButton->show();
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

    m_entropyScanStartMs             = QDateTime::currentMSecsSinceEpoch();
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
    const qulonglong  entropyScopeStart = m_entropyScopeStart;
    const qulonglong  entropyScopeLen   = m_entropyScopeLength;

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
                                    entropyScopeStart, entropyScopeLen,
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
            qulonglong scopeSize = 0;
            const QVector<float> results = calculateByteClass(path, windowSize,
                entropyScopeStart, entropyScopeLen, scopeSize, cancelFlag, pause, progressCb);
            if (cancelFlag->load())
                return;
            QMetaObject::invokeMethod(qApp, [guard, generation, results, scopeSize, windowSize, entropyScopeStart]() {
                if (guard) guard->applyByteClassResults(generation, results, scopeSize, windowSize, entropyScopeStart);
            }, Qt::QueuedConnection);
        }
        else if (mode == EntropyMode::Hilbert)
        {
            int        sampleCount = 0;
            qulonglong scopeSize   = 0;
            const QVector<quint8> bytes = calculateHilbert(path,
                entropyScopeStart, entropyScopeLen, scopeSize, sampleCount,
                gridSide * gridSide, cancelFlag, pause, progressCb);
            if (cancelFlag->load()) return;
            QMetaObject::invokeMethod(qApp,
                [guard, generation, bytes, scopeSize, sampleCount, gridSide, entropyScopeStart]() {
                    if (guard) guard->applyHilbertResults(generation, bytes, scopeSize, sampleCount,
                                                          gridSide, entropyScopeStart);
                }, Qt::QueuedConnection);
        }
        else if (mode == EntropyMode::Gilbert)
        {
            int        sampleCount = 0;
            qulonglong scopeSize   = 0;
            const QVector<quint8> bytes = calculateHilbert(path,
                entropyScopeStart, entropyScopeLen, scopeSize, sampleCount,
                gridSide * gridSide, cancelFlag, pause, progressCb);
            if (cancelFlag->load()) return;
            QMetaObject::invokeMethod(qApp,
                [guard, generation, bytes, scopeSize, sampleCount, entropyScopeStart]() {
                    if (guard) guard->applyGilbertResults(generation, bytes, scopeSize, sampleCount,
                                                          entropyScopeStart);
                }, Qt::QueuedConnection);
        }
        else
        {
            qulonglong scopeSize = 0;
            const QVector<float> results = calculateEntropy(path, windowSize,
                entropyScopeStart, entropyScopeLen, scopeSize, cancelFlag, pause, progressCb);
            if (cancelFlag->load())
                return;
            QMetaObject::invokeMethod(qApp, [guard, generation, results, scopeSize, windowSize, entropyScopeStart]() {
                if (guard) guard->applyEntropyResults(generation, results, scopeSize, windowSize, entropyScopeStart);
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
                                              qulonglong scopeSize, int windowSize, qulonglong scopeStart)
{
    if (generation != m_entropyState.generation)
        return;
    m_entropyLastScanMs = QDateTime::currentMSecsSinceEpoch() - m_entropyScanStartMs;
    if (m_entropyView)
        m_entropyView->setData(data, scopeSize, windowSize, scopeStart);
    updateEntropyStatsLabel();
    updateZoomButton();
    if (m_entropyOperation) m_entropyOperation->clear();
    resetEntropyTitle();
    requestSectionLayoutRefresh(SectionId::Entropy);
    QTimer::singleShot(0, this, [this]() { repairExpandedSectionGeometry(SectionId::Entropy); });
}

void FilePropertiesPanel::applyBigramResults(int generation, QVector<quint64> counts, qulonglong fileSize)
{
    if (generation != m_entropyState.generation)
        return;
    m_entropyLastScanMs = QDateTime::currentMSecsSinceEpoch() - m_entropyScanStartMs;
    if (m_entropyView)
        m_entropyView->setBigramData(counts, fileSize);
    updateEntropyStatsLabel();
    if (m_entropyOperation) m_entropyOperation->clear();
    resetEntropyTitle();
    requestSectionLayoutRefresh(SectionId::Entropy);
    QTimer::singleShot(0, this, [this]() { repairExpandedSectionGeometry(SectionId::Entropy); });
}

void FilePropertiesPanel::applyByteClassResults(int generation, QVector<float> data,
                                                qulonglong scopeSize, int windowSize, qulonglong scopeStart)
{
    if (generation != m_entropyState.generation)
        return;
    m_entropyLastScanMs = QDateTime::currentMSecsSinceEpoch() - m_entropyScanStartMs;
    if (m_entropyView)
        m_entropyView->setByteClassData(data, scopeSize, windowSize, scopeStart);
    updateEntropyStatsLabel();
    updateZoomButton();
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
    m_entropyLastScanMs = QDateTime::currentMSecsSinceEpoch() - m_entropyScanStartMs;
    if (m_entropyView)
        m_entropyView->setHilbertData(bytes, scopeSize, sampleCount, gridSide, scopeStart);
    updateEntropyStatsLabel();
    updateZoomButton();
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
    m_entropyLastScanMs = QDateTime::currentMSecsSinceEpoch() - m_entropyScanStartMs;
    if (m_entropyView)
        m_entropyView->setGilbertData(bytes, scopeSize, sampleCount, scopeStart);
    updateEntropyStatsLabel();
    updateZoomButton();
    if (m_entropyOperation) m_entropyOperation->clear();
    resetEntropyTitle();
    requestSectionLayoutRefresh(SectionId::Entropy);
    QTimer::singleShot(0, this, [this]() { repairExpandedSectionGeometry(SectionId::Entropy); });
}

void FilePropertiesPanel::markEntropyContentsChanged()
{
    m_entropyLastScanMs             = -1;
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
    m_entropyLastScanMs              = -1;
    m_entropyState.started           = false;
    m_entropyState.pausedByCollapse  = false;
    m_entropyState.autoStartConsumed = false;
    ++m_entropyState.generation;
    if (m_entropyState.cancel) m_entropyState.cancel->store(true);
    if (m_entropyState.pause)  m_entropyState.pause->wake();
    if (m_entropyView)         m_entropyView->clear();
    if (m_entropyStatsLabel)   m_entropyStatsLabel->clear();
    m_entropyScopeStart  = 0;
    m_entropyScopeLength = 0;
    updateZoomButton();
    resetEntropyTitle();
    m_entropyState.rescanRequired = true;
    m_entropyState.rescanMessage  = tr("File changed");
    if (m_entropyOperation)    m_entropyOperation->showRescan(m_entropyState.rescanMessage);
    requestSectionLayoutRefresh(SectionId::Entropy);
}

void FilePropertiesPanel::triggerParamRescan(const QString &message)
{
    static constexpr qint64 kAutoRescanThresholdMs = 5000;

    if (m_entropyLastScanMs >= 0 && m_entropyLastScanMs < kAutoRescanThresholdMs)
    {
        // Last scan was fast enough — restart immediately without prompting
        m_entropyState.started          = true;
        m_entropyState.pausedByCollapse = false;
        if (m_entropyView)       m_entropyView->clear();
        if (m_entropyStatsLabel) m_entropyStatsLabel->clear();
        startEntropyAnalysis();
    }
    else
    {
        m_entropyState.started          = false;
        m_entropyState.pausedByCollapse = false;
        ++m_entropyState.generation;
        if (m_entropyState.cancel) m_entropyState.cancel->store(true);
        if (m_entropyState.pause)  m_entropyState.pause->wake();
        if (m_entropyView)         m_entropyView->clear();
        if (m_entropyStatsLabel)   m_entropyStatsLabel->clear();
        m_entropyState.rescanRequired = true;
        m_entropyState.rescanMessage  = message;
        if (m_entropyOperation)    m_entropyOperation->showRescan(message);
        requestSectionLayoutRefresh(SectionId::Entropy);
    }
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
        if (m_entropyScopeLength > 0)
            m_entropyStatsLabel->setText(
                tr("Zoomed: 0x%1 – 0x%2")
                    .arg(QString::number(m_entropyScopeStart, 16).toUpper())
                    .arg(QString::number(m_entropyScopeStart + m_entropyScopeLength - 1, 16).toUpper()));
        else
            m_entropyStatsLabel->clear();
        return;
    }
    if (m_entropyView->isByteClass())
    {
        if (m_entropyView->byteClassData().isEmpty())
        {
            m_entropyStatsLabel->clear();
            return;
        }
        // Class names vary by scheme; order matches byteClassAvg(0..3)
        using BC = filestats::ByteClassScheme;
        struct ClassNames { const char *c[4]; };
        static const ClassNames kNames[] = {
            {{"Null",    "Space",    "Printable", "Binary" }},  // Semantic
            {{"Control", "Printable","DEL",       "High"   }},  // AsciiRange
            {{"0-2 bits","3-4 bits", "5-6 bits",  "7-8 bits"}}, // BitDensity
            {{"00-3F",   "40-7F",    "80-BF",     "C0-FF"  }},  // NibbleRange
        };
        const auto &cn = kNames[int(m_entropyView->byteClassScheme())];
        const auto pct = [&](int i){ return qRound(m_entropyView->byteClassAvg(i) * 100); };
        const QString stats = QStringLiteral("%1 %2%   %3 %4%   %5 %6%   %7 %8%")
            .arg(QLatin1String(cn.c[0])).arg(pct(0))
            .arg(QLatin1String(cn.c[1])).arg(pct(1))
            .arg(QLatin1String(cn.c[2])).arg(pct(2))
            .arg(QLatin1String(cn.c[3])).arg(pct(3));
        if (m_entropyScopeLength > 0)
            m_entropyStatsLabel->setText(
                tr("Zoomed: 0x%1 – 0x%2\n")
                    .arg(QString::number(m_entropyScopeStart, 16).toUpper())
                    .arg(QString::number(m_entropyScopeStart + m_entropyScopeLength - 1, 16).toUpper())
                + "\n" + stats);
        else
            m_entropyStatsLabel->setText(stats);
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
    if (m_entropyScopeLength > 0)
        m_entropyStatsLabel->setText(
            tr("Zoomed: 0x%1 – 0x%2\nMin %3   Avg %4   Max %5  bits/byte")
                .arg(QString::number(m_entropyScopeStart, 16).toUpper())
                .arg(QString::number(m_entropyScopeStart + m_entropyScopeLength - 1, 16).toUpper())
                .arg(fmt(m_entropyView->minEntropy()))
                .arg(fmt(m_entropyView->avgEntropy()))
                .arg(fmt(m_entropyView->maxEntropy())));
    else
        m_entropyStatsLabel->setText(
            tr("Min %1   Avg %2   Max %3  bits/byte")
                .arg(fmt(m_entropyView->minEntropy()))
                .arg(fmt(m_entropyView->avgEntropy()))
                .arg(fmt(m_entropyView->maxEntropy())));
}

void FilePropertiesPanel::buildEntropySection(QWidget *parent, QVBoxLayout *contentLayout)
{
    m_entropyHeader = new SectionHeader(tr("Entropy"), parent);
    m_entropyHeader->setClickedCallback(
        [this]() { setSectionCollapsed(SectionId::Entropy, !isSectionCollapsed(SectionId::Entropy)); });
    contentLayout->addWidget(m_entropyHeader);
    m_entropyHeader->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_entropyHeaderGap = new QSpacerItem(0, kHeaderControlGap, QSizePolicy::Minimum, QSizePolicy::Fixed);
    contentLayout->addSpacerItem(m_entropyHeaderGap);

    m_entropySectionBody = new QWidget(parent);
    m_entropySectionBody->setMinimumWidth(0);
    auto *entropyBodyLayout = new QVBoxLayout(m_entropySectionBody);
    entropyBodyLayout->setContentsMargins(kSectionHeaderOuterMargin + kCardLeftInset, 0,
                                          kSectionHeaderOuterMargin + kCardScrollbarInset, 0);
    entropyBodyLayout->setSpacing(0);

    auto startEntropy = [this]()
    {
        if (m_entropyState.started)
            return;
        m_entropyState.started = true;
        startEntropyAnalysis();
    };
    m_entropyOperation = new SectionOperationStrip(
        parent, startEntropy,
        [this]() { cancelEntropyAnalysis(); },
        [this]() { resumeEntropyAnalysis(); },
        startEntropy);

    auto *entropyControlsStack       = new QWidget(m_entropySectionBody);
    auto *entropyControlsStackLayout = new QVBoxLayout(entropyControlsStack);
    entropyControlsStackLayout->setContentsMargins(kSettingsCardShadowInset, 0, kSettingsCardShadowInset, 0);
    entropyControlsStackLayout->setSpacing(0);

    auto *entropyControls       = new QWidget(entropyControlsStack);
    auto *entropyControlsLayout = new QHBoxLayout(entropyControls);
    entropyControlsLayout->setContentsMargins(0, 4, 0, 4);
    entropyControlsLayout->setSpacing(6);

    m_entropyModeCombo = new MenuComboBox(entropyControls);
    m_entropyModeCombo->addItem(tr("Shannon"),    QVariant::fromValue(int(EntropyMode::Shannon)));
    m_entropyModeCombo->addItem(tr("Bigram"),     QVariant::fromValue(int(EntropyMode::Bigram)));
    m_entropyModeCombo->addItem(tr("Byte Class"), QVariant::fromValue(int(EntropyMode::ByteClass)));
    m_entropyModeCombo->addItem(tr("Hilbert"),    QVariant::fromValue(int(EntropyMode::Hilbert)));
    m_entropyModeCombo->addItem(tr("Gilbert"),    QVariant::fromValue(int(EntropyMode::Gilbert)));
    m_entropyModeCombo->setCurrentIndex(0);
    m_entropyModeCombo->setFocusPolicy(Qt::StrongFocus);
    m_entropyModeCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_entropyModeCombo->setFixedHeight(qMax(24, m_entropyModeCombo->sizeHint().height() - 4));
    m_entropyModeCombo->setToolTip(
        tr("Visualisation mode: byte entropy per window, byte-pair frequency heatmap, byte class stacked bars, "
           "square Hilbert curve map, or adaptive Gilbert curve map"));
    entropyControlsLayout->addWidget(m_entropyModeCombo);

    // Hilbert/Gilbert/ByteClass color picker button — shown after mode combo
    m_hilbertColorMenu = new QMenu(this);
    themeMenu(m_hilbertColorMenu);
    auto *colorGroup = new QActionGroup(m_hilbertColorMenu);
    colorGroup->setExclusive(true);
    m_colorByteClassAction = m_hilbertColorMenu->addAction(tr("Byte Class"));
    m_colorByteClassAction->setCheckable(true);
    m_colorByteClassAction->setChecked(true);
    m_colorByteClassAction->setActionGroup(colorGroup);
    m_colorMagnitudeAction = m_hilbertColorMenu->addAction(tr("Magnitude"));
    m_colorMagnitudeAction->setCheckable(true);
    m_colorMagnitudeAction->setActionGroup(colorGroup);
    m_colorEntropyAction = m_hilbertColorMenu->addAction(tr("Entropy"));
    m_colorEntropyAction->setCheckable(true);
    m_colorEntropyAction->setActionGroup(colorGroup);
    m_colorDetailAction = m_hilbertColorMenu->addAction(tr("Detail"));
    m_colorDetailAction->setCheckable(true);
    m_colorDetailAction->setActionGroup(colorGroup);

    m_hilbertColorButton = new QToolButton(entropyControls);
    m_hilbertColorButton->setFixedSize(28, 28);
    m_hilbertColorButton->setFocusPolicy(Qt::TabFocus);
    m_hilbertColorButton->setToolTip(tr("Colorisation mode"));
    m_hilbertColorButton->setAutoRaise(true);
    m_hilbertColorButton->setPopupMode(QToolButton::InstantPopup);
    m_hilbertColorButton->setProperty("iconThemeName", QStringLiteral("actions/palette"));
    m_hilbertColorButton->setProperty("iconSize", 16);
    m_hilbertColorButton->setIconSize(QSize(16, 16));
    m_hilbertColorButton->setIcon(
        recoloredIcon(QStringLiteral("actions/palette"), palette().buttonText().color(), 16));
    {
        const bool    dark    = QApplication::palette().window().color().lightness() < 128;
        const QString hover   = dark ? QStringLiteral("rgba(255,255,255,0.15)") : QStringLiteral("rgba(0,0,0,0.10)");
        const QString pressed = dark ? QStringLiteral("rgba(255,255,255,0.25)") : QStringLiteral("rgba(0,0,0,0.18)");
        m_hilbertColorButton->setStyleSheet(QStringLiteral(R"(
            QToolButton {
                border: none;
                border-radius: 6px;
                background: transparent;
            }
            QToolButton:hover   { background: %1; }
            QToolButton:pressed { background: %2; }
        )").arg(hover, pressed));
    }
    m_hilbertColorButton->setMenu(m_hilbertColorMenu);
    m_hilbertColorButton->hide();
    entropyControlsLayout->addWidget(m_hilbertColorButton);

    // ByteClass-specific scheme menu — swapped onto color button in ByteClass mode
    m_byteClassSchemeMenu = new QMenu(this);
    themeMenu(m_byteClassSchemeMenu);
    auto *schemeGroup = new QActionGroup(m_byteClassSchemeMenu);
    schemeGroup->setExclusive(true);
    m_bcSchemeSemanticAction = m_byteClassSchemeMenu->addAction(tr("Semantic"));
    m_bcSchemeSemanticAction->setCheckable(true);
    m_bcSchemeSemanticAction->setChecked(true);
    m_bcSchemeSemanticAction->setActionGroup(schemeGroup);
    m_bcSchemeAsciiAction = m_byteClassSchemeMenu->addAction(tr("ASCII Range"));
    m_bcSchemeAsciiAction->setCheckable(true);
    m_bcSchemeAsciiAction->setActionGroup(schemeGroup);
    m_bcSchemeBitDensAction = m_byteClassSchemeMenu->addAction(tr("Bit Density"));
    m_bcSchemeBitDensAction->setCheckable(true);
    m_bcSchemeBitDensAction->setActionGroup(schemeGroup);
    m_bcSchemeNibbleAction = m_byteClassSchemeMenu->addAction(tr("Nibble Range"));
    m_bcSchemeNibbleAction->setCheckable(true);
    m_bcSchemeNibbleAction->setActionGroup(schemeGroup);
    connect(m_bcSchemeSemanticAction, &QAction::triggered, this,
            [this]() { if (m_entropyView) m_entropyView->setByteClassScheme(ByteClassScheme::Semantic); });
    connect(m_bcSchemeAsciiAction, &QAction::triggered, this,
            [this]() { if (m_entropyView) m_entropyView->setByteClassScheme(ByteClassScheme::AsciiRange); });
    connect(m_bcSchemeBitDensAction, &QAction::triggered, this,
            [this]() { if (m_entropyView) m_entropyView->setByteClassScheme(ByteClassScheme::BitDensity); });
    connect(m_bcSchemeNibbleAction, &QAction::triggered, this,
            [this]() { if (m_entropyView) m_entropyView->setByteClassScheme(ByteClassScheme::NibbleRange); });

    // Zoom button (all modes except Bigram)
    m_hilbertZoomButton = new QToolButton(entropyControls);
    m_hilbertZoomButton->setFixedSize(28, 28);
    m_hilbertZoomButton->setFocusPolicy(Qt::TabFocus);
    m_hilbertZoomButton->setAutoRaise(true);
    m_hilbertZoomButton->setIconSize(QSize(16, 16));
    m_hilbertZoomButton->setIcon(
        recoloredIcon(QStringLiteral("actions/zoom-in"), palette().buttonText().color(), 16));
    m_hilbertZoomButton->setToolTip(tr("Re-scan selected range at full resolution"));
    {
        const bool    dark    = QApplication::palette().window().color().lightness() < 128;
        const QString hover   = dark ? QStringLiteral("rgba(255,255,255,0.15)") : QStringLiteral("rgba(0,0,0,0.10)");
        const QString pressed = dark ? QStringLiteral("rgba(255,255,255,0.25)") : QStringLiteral("rgba(0,0,0,0.18)");
        m_hilbertZoomButton->setStyleSheet(QStringLiteral(R"(
            QToolButton { border: none; border-radius: 6px; background: transparent; }
            QToolButton:hover   { background: %1; }
            QToolButton:pressed { background: %2; }
        )").arg(hover, pressed));
    }
    entropyControlsLayout->insertWidget(1, m_hilbertZoomButton);

    m_bigramScaleCombo = new MenuComboBox(entropyControls);
    m_bigramScaleCombo->addItem(tr("Log"),    QVariant::fromValue(int(BigramScale::Log)));
    m_bigramScaleCombo->addItem(tr("Sqrt"),   QVariant::fromValue(int(BigramScale::Sqrt)));
    m_bigramScaleCombo->addItem(tr("Linear"), QVariant::fromValue(int(BigramScale::Linear)));
    m_bigramScaleCombo->setCurrentIndex(0);
    m_bigramScaleCombo->setFocusPolicy(Qt::StrongFocus);
    m_bigramScaleCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_bigramScaleCombo->setFixedHeight(qMax(24, m_bigramScaleCombo->sizeHint().height() - 4));
    m_bigramScaleCombo->setToolTip(
        tr("Frequency scale: Log compresses bright peaks to reveal rare pairs; Sqrt is a gentler middle ground; "
           "Linear shows raw counts"));
    m_bigramScaleCombo->setVisible(false);
    entropyControlsLayout->addWidget(m_bigramScaleCombo);

    m_bigramStrideSpinner = new StepSpinBox(tr("Stride:"), 1, 8, 1, entropyControls);
    m_bigramStrideSpinner->setValues({1, 2, 4, 8});
    m_bigramStrideSpinner->setValue(1);
    m_bigramStrideSpinner->setLabelAlignment(Qt::AlignRight);
    m_bigramStrideSpinner->setLabelValueSpacing(4);
    m_bigramStrideSpinner->setValueWidth(12);
    m_bigramStrideSpinner->setValueBold(true);
    m_bigramStrideSpinner->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_bigramStrideSpinner->setToolTip(
        tr("Byte pair distance: 1 = consecutive, 2 = every other byte (16-bit), 4 = 32-bit words, 8 = 64-bit words"));
    m_bigramStrideSpinner->setVisible(false);

    entropyControlsLayout->addStretch();
    entropyControlsLayout->addWidget(m_bigramStrideSpinner);

    m_entropyWindowLabel = new QLabel(tr("Sample:"), entropyControls);
    m_entropyWindowLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    entropyControlsLayout->addWidget(m_entropyWindowLabel);

    m_entropyWindowCombo = new MenuComboBox(entropyControls);
    m_entropyWindowCombo->addItem(tr("128 bytes"),  QVariant::fromValue(128));
    m_entropyWindowCombo->addItem(tr("256 bytes"),  QVariant::fromValue(256));
    m_entropyWindowCombo->addItem(tr("512 bytes"),  QVariant::fromValue(512));
    m_entropyWindowCombo->addItem(tr("1024 bytes"), QVariant::fromValue(1024));
    m_entropyWindowCombo->setCurrentIndex(1);
    m_entropyWindowCombo->setFocusPolicy(Qt::StrongFocus);
    m_entropyWindowCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_entropyWindowCombo->setFixedHeight(qMax(24, m_entropyWindowCombo->sizeHint().height() - 4));
    m_entropyWindowCombo->setToolTip(tr("Entropy window size (bytes per sample)"));
    entropyControlsLayout->addWidget(m_entropyWindowCombo);

    m_hilbertGridLabel = new QLabel(tr("Grid:"), entropyControls);
    m_hilbertGridLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_hilbertGridLabel->hide();
    entropyControlsLayout->addWidget(m_hilbertGridLabel);

    m_hilbertGridCombo = new MenuComboBox(entropyControls);
    m_hilbertGridCombo->addItem(tr("64×64"),   QVariant::fromValue(64));
    m_hilbertGridCombo->addItem(tr("128×128"), QVariant::fromValue(128));
    m_hilbertGridCombo->addItem(tr("256×256"), QVariant::fromValue(256));
    m_hilbertGridCombo->addItem(tr("512×512"), QVariant::fromValue(512));
    m_hilbertGridCombo->setCurrentIndex(2);
    m_hilbertGridCombo->setFocusPolicy(Qt::StrongFocus);
    m_hilbertGridCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_hilbertGridCombo->setFixedHeight(qMax(24, m_hilbertGridCombo->sizeHint().height() - 4));
    m_hilbertGridCombo->setToolTip(tr("Curve grid resolution (samples)"));
    m_hilbertGridCombo->hide();
    entropyControlsLayout->addWidget(m_hilbertGridCombo);

    entropyControlsStackLayout->addWidget(entropyControls);
    entropyControlsStackLayout->addSpacing(kHeaderControlGap + 4);
    entropyControlsStackLayout->addWidget(m_entropyOperation->widget());
    entropyControlsStackLayout->addSpacing(kHeaderControlGap + 4);

    // Graph in bordered frame
    auto *entropyViewFrame = new QFrame(entropyControlsStack);
    entropyViewFrame->setObjectName(QStringLiteral("entropyViewFrame"));
    entropyViewFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    entropyViewFrame->setStyleSheet(QStringLiteral(R"(
        QFrame#entropyViewFrame {
            background: palette(base);
            border: 1px solid palette(mid);
            border-radius: 6px;
        }
    )"));
    auto *entropyViewFrameLayout = new QVBoxLayout(entropyViewFrame);
    entropyViewFrameLayout->setContentsMargins(1, 1, 1, 1);
    entropyViewFrameLayout->setSpacing(0);

    m_entropyView = new EntropyView(entropyViewFrame);
    m_entropyView->setRotated(true);
    m_entropyView->setMinimumSize(0, 0);
    m_entropyView->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    entropyViewFrameLayout->addWidget(m_entropyView);
    entropyControlsStackLayout->addWidget(entropyViewFrame);

    m_bigramRescanTimer = new QTimer(this);
    m_bigramRescanTimer->setSingleShot(true);
    m_bigramRescanTimer->setInterval(300);
    connect(m_bigramRescanTimer, &QTimer::timeout, this,
            [this]()
            {
                if (m_entropyMode == EntropyMode::Bigram && m_entropyState.started
                    && !m_entropyState.rescanRequired)
                    startEntropyAnalysis();
            });

    // Resize handle
    auto *entropyResizeWrap   = new QWidget(entropyControlsStack);
    auto *entropyResizeLayout = new QVBoxLayout(entropyResizeWrap);
    entropyResizeLayout->setContentsMargins(0, 0, 0, 0);
    entropyResizeLayout->setSpacing(0);
    auto *entropyResizeHandle = new VerticalResizeHandle(
        [this](int dy) { resizeSection(SectionId::Entropy, dy); }, entropyResizeWrap);
    entropyResizeLayout->addWidget(entropyResizeHandle);
    entropyResizeLayout->setAlignment(entropyResizeHandle, Qt::AlignTop);
    entropyControlsStackLayout->addWidget(entropyResizeWrap);

    // Stats / hover label
    m_entropyStatsLabel = new QLabel(entropyControlsStack);
    m_entropyStatsLabel->setAlignment(Qt::AlignCenter);
    m_entropyStatsLabel->setContentsMargins(0, 4, 0, 4);
    m_entropyStatsLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    entropyControlsStackLayout->addWidget(m_entropyStatsLabel);
    entropyControlsStackLayout->addSpacing(kContentMargin);

    entropyBodyLayout->addWidget(entropyControlsStack);
    contentLayout->addWidget(m_entropySectionBody);

    registerPanelSection({
        SectionId::Entropy,
        tr("Entropy"),
        m_entropyHeader,
        m_entropySectionBody,
        m_entropyHeaderGap,
        nullptr,
        entropyViewFrame,
        kEntropyViewMinHeight,
        [this]() { maybeStartEntropyAnalysis(); },
        [this](bool collapsed)
        {
            if (m_entropyState.pause && m_entropyState.started)
            {
                if (collapsed)
                {
                    m_entropyState.pausedByCollapse = true;
                    m_entropyState.pause->setPaused(true);
                }
                else if (m_entropyState.pausedByCollapse && m_entropyOperation)
                {
                    m_entropyOperation->setProgressActionResume();
                }
            }
            if (m_entropyState.started)
                setEntropyProgressTitle(m_entropyState.progress);
        },
        [this](bool contentsChanged) { if (contentsChanged) markEntropyContentsChanged(); },
        [this]() { resetEntropyForCurrentDocument(); },
    });

    // Signal connections for entropy controls
    connect(m_entropyModeCombo, &QComboBox::currentIndexChanged, this,
            [this](int index)
            {
                const auto mode = static_cast<EntropyMode>(m_entropyModeCombo->itemData(index).toInt());
                if (mode == m_entropyMode)
                    return;
                m_entropyMode = mode;
                if (m_entropyView)       m_entropyView->clear();
                if (m_entropyStatsLabel) m_entropyStatsLabel->clear();
                const bool isBigram    = (mode == EntropyMode::Bigram);
                const bool isImageMode = (mode == EntropyMode::Bigram || mode == EntropyMode::Hilbert
                                          || mode == EntropyMode::Gilbert);
                const bool isGridMode  = (mode == EntropyMode::Hilbert || mode == EntropyMode::Gilbert);
                const bool isZoomMode  = !isBigram;
                if (m_entropyWindowLabel)  m_entropyWindowLabel->setVisible(!isImageMode);
                if (m_entropyWindowCombo)  m_entropyWindowCombo->setVisible(!isImageMode);
                if (m_hilbertGridLabel)    m_hilbertGridLabel->setVisible(isGridMode);
                if (m_hilbertGridCombo)    m_hilbertGridCombo->setVisible(isGridMode);
                const bool isByteClass = (mode == EntropyMode::ByteClass);
                if (m_hilbertColorButton)
                {
                    m_hilbertColorButton->setVisible(isGridMode || isByteClass);
                    m_hilbertColorButton->setMenu(isByteClass ? m_byteClassSchemeMenu : m_hilbertColorMenu);
                }
                if (m_hilbertZoomButton)
                {
                    if (!isZoomMode)
                        m_hilbertZoomButton->hide();
                    else
                        updateZoomButton();
                }
                if (m_bigramScaleCombo)    m_bigramScaleCombo->setVisible(isBigram);
                if (m_bigramStrideSpinner) m_bigramStrideSpinner->setVisible(isBigram);
                triggerParamRescan(tr("View changed"));
            });
    connect(m_bigramScaleCombo, &QComboBox::currentIndexChanged, this,
            [this](int index)
            {
                const auto scale =
                    static_cast<BigramScale>(m_bigramScaleCombo->itemData(index).toInt());
                if (m_entropyView)
                    m_entropyView->setBigramScale(scale);
            });
    connect(m_bigramStrideSpinner, &StepSpinBox::valueChanged, this,
            [this](int stride)
            {
                if (stride <= 0 || stride == m_bigramStride)
                    return;
                m_bigramStride = stride;
                triggerParamRescan(tr("Stride changed"));
            });
    connect(m_entropyWindowCombo, &QComboBox::currentIndexChanged, this,
            [this](int index)
            {
                const int ws = m_entropyWindowCombo->itemData(index).toInt();
                if (ws <= 0 || ws == m_entropyWindowSize)
                    return;
                m_entropyWindowSize = ws;
                triggerParamRescan(tr("Window size changed"));
            });
    connect(m_hilbertGridCombo, &QComboBox::currentIndexChanged, this,
            [this](int index)
            {
                const int gs = m_hilbertGridCombo->itemData(index).toInt();
                if (gs <= 0 || gs == m_hilbertGridSide)
                    return;
                m_hilbertGridSide = gs;
                triggerParamRescan(tr("Grid size changed"));
            });

    auto connectColorAction = [this](QAction *action, HilbertColorMode mode)
    {
        connect(action, &QAction::triggered, this,
                [this, mode]() { if (m_entropyView) m_entropyView->setHilbertColorMode(mode); });
    };
    connectColorAction(m_colorByteClassAction, HilbertColorMode::ByteClass);
    connectColorAction(m_colorMagnitudeAction,  HilbertColorMode::Magnitude);
    connectColorAction(m_colorEntropyAction,    HilbertColorMode::Entropy);
    connectColorAction(m_colorDetailAction,     HilbertColorMode::Detail);

    connect(m_hilbertZoomButton, &QToolButton::clicked, this,
            [this]()
            {
                const bool isZoomed = m_entropyScopeLength > 0;
                const bool selMatchesScope = isZoomed && m_hexView
                    && m_hexView->selectionEnd() > m_hexView->selectionStart()
                    && static_cast<qulonglong>(m_hexView->selectionStart()) == m_entropyScopeStart
                    && (static_cast<qulonglong>(m_hexView->selectionEnd())
                        - static_cast<qulonglong>(m_hexView->selectionStart())) == m_entropyScopeLength;
                if (m_hexView && m_hexView->selectionEnd() > m_hexView->selectionStart()
                    && !selMatchesScope)
                {
                    m_entropyScopeStart  = static_cast<qulonglong>(m_hexView->selectionStart());
                    m_entropyScopeLength = static_cast<qulonglong>(m_hexView->selectionEnd())
                                         - m_entropyScopeStart;
                }
                else
                {
                    m_entropyScopeStart  = 0;
                    m_entropyScopeLength = 0;
                }
                updateZoomButton();
                m_entropyState.started           = false;
                m_entropyState.pausedByCollapse  = false;
                m_entropyState.autoStartConsumed = false;
                m_entropyState.rescanRequired    = false;
                startEntropyAnalysis();
            });
    connect(m_entropyView, &EntropyView::positionHovered, this,
            [this](qulonglong offset, float entropy)
            {
                if (!m_entropyStatsLabel)
                    return;
                if (m_entropyMode == EntropyMode::ByteClass || m_entropyMode == EntropyMode::Hilbert
                    || m_entropyMode == EntropyMode::Gilbert)
                    m_entropyStatsLabel->setText(
                        QStringLiteral("0x")
                        + QStringLiteral("%1").arg(offset, 8, 16, QLatin1Char('0')).toUpper());
                else
                    m_entropyStatsLabel->setText(
                        tr("0x%1  —  %2 bits/byte")
                            .arg(QStringLiteral("%1").arg(offset, 8, 16, QLatin1Char('0')).toUpper())
                            .arg(double(entropy * 8.0), 0, 'f', 2));
            });
    connect(m_entropyView, &EntropyView::hoverCleared,    this, [this]() { updateEntropyStatsLabel(); });
    connect(m_entropyView, &EntropyView::selectionCleared, this, [this]() { updateZoomButton(); });
    connect(m_entropyView, &EntropyView::positionClicked, this,
            [this](qulonglong offset)
            {
                if (m_hexView)
                {
                    const auto pos = static_cast<size_w>(offset);
                    m_hexView->setCurSel(pos, pos);
                    m_hexView->scrollCenter(pos);
                    m_hexView->setFocus();
                }
                updateZoomButton();
            });
    connect(m_entropyView, &EntropyView::rangeSelected, this,
            [this](qulonglong anchor, qulonglong cursor)
            {
                if (!m_hexView) return;
                const auto lo = static_cast<size_w>(qMin(anchor, cursor));
                const auto hi = static_cast<size_w>(qMax(anchor, cursor));
                if (m_entropyView) m_entropyView->setSelection(lo, hi);
                m_hexView->setCurSel(lo, hi);
                m_hexView->scrollCenterIfOffScreen(static_cast<size_w>(cursor));
                m_hexView->setFocus();
                updateZoomButton();
            });
    if (m_hexView)
        connect(m_hexView, &HexView::selectionChanged, this,
                [this](size_w start, size_w end)
                {
                    if (m_entropyView)
                    {
                        if (end > start)
                            m_entropyView->setSelection(static_cast<qulonglong>(start),
                                                        static_cast<qulonglong>(end));
                        else
                            m_entropyView->clearSelection();
                    }
                    if (m_entropyMode == EntropyMode::Bigram && m_entropyState.started
                        && !m_entropyState.rescanRequired && m_bigramRescanTimer)
                        m_bigramRescanTimer->start();
                    updateZoomButton();
                });
}
