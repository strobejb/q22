#include "filestats/entropyscan.h"

#include <QIODevice>

#include <algorithm>
#include <cmath>
#include <limits>

namespace filestats
{

namespace
{

bool shouldContinue(const EntropyScanCallbacks &callbacks)
{
    return !callbacks.shouldContinue || callbacks.shouldContinue();
}

bool seekTo(QIODevice &input, qulonglong offset)
{
    return offset <= static_cast<qulonglong>(std::numeric_limits<qint64>::max()) &&
           input.seek(static_cast<qint64>(offset));
}

qulonglong boundedScope(QIODevice &input, qulonglong &startOffset, qulonglong byteCount)
{
    const qint64 deviceSize = input.size();
    if (deviceSize <= 0)
        return 0;

    const qulonglong fileTotal = static_cast<qulonglong>(deviceSize);
    startOffset = qMin(startOffset, fileTotal);
    if (byteCount == 0 || byteCount > fileTotal - startOffset)
        byteCount = fileTotal - startOffset;
    return byteCount;
}

} // namespace

QVector<float> calculateEntropy(QIODevice &input,
                                int windowSize,
                                qulonglong startOffset,
                                qulonglong byteCount,
                                qulonglong &outScopeSize,
                                const EntropyScanCallbacks &callbacks)
{
    if (!input.isOpen() || !input.isReadable())
    {
        outScopeSize = 0;
        return {};
    }

    byteCount = boundedScope(input, startOffset, byteCount);
    outScopeSize = byteCount;
    if (byteCount == 0 || windowSize <= 0)
        return {};
    if (!seekTo(input, startOffset))
    {
        outScopeSize = 0;
        return {};
    }

    const qint64   scanTotal = static_cast<qint64>(byteCount);
    qint64         scanned   = 0;
    int            lastProg  = -1;
    QVector<float> results;
    results.reserve(static_cast<int>(qMin((scanTotal + windowSize - 1) / windowSize, qint64(1 << 20))));

    while (scanned < scanTotal)
    {
        if (!shouldContinue(callbacks))
            return {};

        const qint64     toRead = qMin(qint64(windowSize), scanTotal - scanned);
        const QByteArray chunk  = input.read(toRead);
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
        if (callbacks.progress && prog != lastProg)
        {
            lastProg = prog;
            callbacks.progress(prog);
        }
    }

    return shouldContinue(callbacks) ? results : QVector<float>();
}

QVector<quint64> calculateBigram(QIODevice &input,
                                 qulonglong startOffset,
                                 qulonglong byteCount,
                                 int stride,
                                 qulonglong &outFileSize,
                                 const EntropyScanCallbacks &callbacks)
{
    if (!input.isOpen() || !input.isReadable())
    {
        outFileSize = 0;
        return {};
    }

    const qint64 deviceSize = input.size();
    if (deviceSize < 0)
    {
        outFileSize = 0;
        return {};
    }
    outFileSize = static_cast<qulonglong>(deviceSize);

    byteCount = boundedScope(input, startOffset, byteCount);
    if (!seekTo(input, startOffset))
    {
        outFileSize = 0;
        return {};
    }

    QVector<quint64> counts(256 * 256, 0);
    if (byteCount == 0 || stride <= 0)
        return counts;

    constexpr int    bufSize   = 65536;
    qint64           scanned   = 0;
    int              lastProg  = -1;
    const qint64     scanTotal = static_cast<qint64>(byteCount);
    QVector<unsigned char> tail;
    tail.reserve(stride);

    while (scanned < scanTotal)
    {
        if (!shouldContinue(callbacks))
            return {};

        const qint64     toRead = qMin(qint64(bufSize), scanTotal - scanned);
        const QByteArray chunk  = input.read(toRead);
        if (chunk.isEmpty())
            break;

        const auto *data = reinterpret_cast<const unsigned char *>(chunk.constData());
        const int   n    = chunk.size();

        for (int i = 0; i < int(tail.size()); ++i)
        {
            const int j = stride - int(tail.size()) + i;
            if (j < n)
                ++counts[tail[i] * 256 + data[j]];
        }

        for (int i = 0; i + stride < n; ++i)
            ++counts[data[i] * 256 + data[i + stride]];

        const int tailLen = qMin(stride, n);
        tail.resize(tailLen);
        for (int i = 0; i < tailLen; ++i)
            tail[i] = data[n - tailLen + i];

        scanned += n;
        const int prog = (scanTotal > 0) ? int(1000LL * scanned / scanTotal) : 1000;
        if (callbacks.progress && prog != lastProg)
        {
            lastProg = prog;
            callbacks.progress(prog);
        }
    }

    return shouldContinue(callbacks) ? counts : QVector<quint64>();
}

QVector<float> calculateByteClass(QIODevice &input,
                                  int windowSize,
                                  qulonglong startOffset,
                                  qulonglong byteCount,
                                  qulonglong &outScopeSize,
                                  const EntropyScanCallbacks &callbacks)
{
    if (!input.isOpen() || !input.isReadable())
    {
        outScopeSize = 0;
        return {};
    }

    byteCount = boundedScope(input, startOffset, byteCount);
    outScopeSize = byteCount;
    if (byteCount == 0 || windowSize <= 0)
        return {};
    if (!seekTo(input, startOffset))
    {
        outScopeSize = 0;
        return {};
    }

    static const int kNumSchemes = 4;
    uint8_t lut[kNumSchemes][256];
    for (int b = 0; b < 256; ++b)
    {
        const auto u = static_cast<unsigned char>(b);
        lut[0][b] = (u == 0x00) ? 0
                  : (u == 0x09 || u == 0x0A || u == 0x0B || u == 0x0C || u == 0x0D || u == 0x20) ? 1
                  : (u >= 0x21 && u <= 0x7E) ? 2 : 3;
        lut[1][b] = (u < 0x20) ? 0 : (u <= 0x7E) ? 1 : (u == 0x7F) ? 2 : 3;
        int bits = 0;
        for (int x = u; x; x &= x - 1)
            ++bits;
        lut[2][b] = static_cast<uint8_t>(bits <= 2 ? 0 : bits <= 4 ? 1 : bits <= 6 ? 2 : 3);
        lut[3][b] = (u < 0x40) ? 0 : (u < 0x80) ? 1 : (u < 0xC0) ? 2 : 3;
    }

    const qint64 scanTotal = static_cast<qint64>(byteCount);
    qint64 scanned         = 0;
    int    lastProg        = -1;
    QVector<float> results;
    results.reserve(16 * static_cast<int>(qMin((scanTotal + windowSize - 1) / windowSize, qint64(1 << 20))));

    while (scanned < scanTotal)
    {
        if (!shouldContinue(callbacks))
            return {};

        const qint64     toRead = qMin(qint64(windowSize), scanTotal - scanned);
        const QByteArray chunk  = input.read(toRead);
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
        if (callbacks.progress && prog != lastProg)
        {
            lastProg = prog;
            callbacks.progress(prog);
        }
    }

    if (!shouldContinue(callbacks))
        return {};

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

QVector<quint8> calculateHilbert(QIODevice &input,
                                 qulonglong startOffset,
                                 qulonglong byteCount,
                                 qulonglong &outScopeSize,
                                 int &outSampleCount,
                                 int maxSamples,
                                 const EntropyScanCallbacks &callbacks)
{
    if (!input.isOpen() || !input.isReadable())
    {
        outScopeSize = 0;
        outSampleCount = 0;
        return {};
    }

    byteCount = boundedScope(input, startOffset, byteCount);
    outScopeSize = byteCount;
    if (byteCount == 0 || maxSamples <= 0)
    {
        outSampleCount = 0;
        return {};
    }
    if (!seekTo(input, startOffset))
    {
        outScopeSize = 0;
        outSampleCount = 0;
        return {};
    }

    const qint64 total        = static_cast<qint64>(byteCount);
    const int    sampleCount  = static_cast<int>(qMin(qint64(maxSamples), total));
    outSampleCount            = sampleCount;

    QVector<quint8> result(sampleCount, 0);

    constexpr int bufSize  = 65536;
    qint64        scanned  = 0;
    int           lastProg = -1;

    while (scanned < total)
    {
        if (!shouldContinue(callbacks))
            return {};

        const qint64     toRead = qMin(qint64(bufSize), total - scanned);
        const QByteArray chunk  = input.read(toRead);
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
        if (callbacks.progress && prog != lastProg)
        {
            lastProg = prog;
            callbacks.progress(prog);
        }
    }

    return shouldContinue(callbacks) ? result : QVector<quint8>();
}

} // namespace filestats
