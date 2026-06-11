#include "filestats/entropy.h"
#include "filestats/sidepanel.h"
#include "filestats/widgets.h"
#include "HexView/hexview.h"

#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
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
    m_data       = data;
    m_fileSize   = fileSize;
    m_windowSize = windowSize;
    m_hoverX     = -1;
    update();
}

void EntropyView::clear()
{
    m_data.clear();
    m_bigramCounts.clear();
    m_bigramImage  = QImage();
    m_isBigram     = false;
    m_fileSize     = 0;
    m_windowSize   = 256;
    m_hoverX       = -1;
    update();
}

void EntropyView::setBigramData(const QVector<quint64> &counts, qulonglong fileSize)
{
    m_bigramCounts = counts;
    m_fileSize     = fileSize;
    m_isBigram     = true;
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
        {0.30f,   0, 100, 200},
        {0.55f,  50, 180,  70},
        {0.75f, 220, 150,   0},
        {1.00f, 210,  30,  30},
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

void EntropyView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_dragging = true;
        if (!m_data.isEmpty() && m_fileSize > 0)
        {
            const QPoint pos     = event->position().toPoint();
            const int    coord   = m_rotated ? pos.y() : pos.x();
            const int    axisLen = m_rotated ? height() : width();
            if (axisLen > 1)
            {
                const float      t      = qBound(0.0f, float(coord) / (axisLen - 1), 1.0f);
                const qulonglong offset = static_cast<qulonglong>(t * m_fileSize);
                emit positionClicked(offset);
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
    m_hoverX = m_rotated ? pos.y() : pos.x();
    update();

    if (!m_data.isEmpty() && m_fileSize > 0)
    {
        const int axisLen = m_rotated ? height() : width();
        if (axisLen > 1)
        {
            const float      t      = qBound(0.0f, float(m_hoverX) / (axisLen - 1), 1.0f);
            const qulonglong offset = static_cast<qulonglong>(t * m_fileSize);
            emit positionHovered(offset, sampleAt(m_hoverX, axisLen));
            if (m_dragging)
                emit positionClicked(offset);
        }
    }
    QWidget::mouseMoveEvent(event);
}

void EntropyView::leaveEvent(QEvent *event)
{
    m_hoverX = -1;
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

// -----------------------------------------------------------------------
// FilePropertiesPanel — entropy methods
// -----------------------------------------------------------------------

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

    const QString     path       = m_hexView->filePath();
    const int         windowSize = m_entropyWindowSize;
    const EntropyMode mode       = m_entropyMode;
    const int         stride     = m_bigramStride;

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
                                    scopeStart, scopeLength, stride, cancelFlag, pause]()
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
    if (!m_entropyStatsLabel || !m_entropyView || m_entropyView->isBigram() || m_entropyView->data().isEmpty())
    {
        if (m_entropyStatsLabel) m_entropyStatsLabel->clear();
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
