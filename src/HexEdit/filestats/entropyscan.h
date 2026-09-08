#pragma once

#include <QVector>

#include <cstdint>
#include <functional>

class QIODevice;

namespace filestats
{

struct EntropyScanCallbacks
{
    std::function<bool()> shouldContinue;
    std::function<void(int)> progress;
};

QVector<float> calculateEntropy(QIODevice &input,
                                int windowSize,
                                qulonglong startOffset,
                                qulonglong byteCount,
                                qulonglong &outScopeSize,
                                const EntropyScanCallbacks &callbacks = {});

QVector<quint64> calculateBigram(QIODevice &input,
                                 qulonglong startOffset,
                                 qulonglong byteCount,
                                 int stride,
                                 qulonglong &outFileSize,
                                 const EntropyScanCallbacks &callbacks = {});

QVector<float> calculateByteClass(QIODevice &input,
                                  int windowSize,
                                  qulonglong startOffset,
                                  qulonglong byteCount,
                                  qulonglong &outScopeSize,
                                  const EntropyScanCallbacks &callbacks = {});

QVector<quint8> calculateHilbert(QIODevice &input,
                                 qulonglong startOffset,
                                 qulonglong byteCount,
                                 qulonglong &outScopeSize,
                                 int &outSampleCount,
                                 int maxSamples,
                                 const EntropyScanCallbacks &callbacks = {});

} // namespace filestats
