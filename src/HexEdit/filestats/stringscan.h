#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QElapsedTimer>
#include <QString>
#include <QTextStream>
#include <QVariantMap>
#include <QVector>

namespace stringscan
{

static constexpr int    kInitialStringResultBatchLimit = 1000;
static constexpr int    kMaxStringResultBatchLimit     = 5000;
static constexpr qint64 kMinimumStringScanMs           = 10000;
static constexpr int    kMaxBufferedStringLength       = 4096;

enum class StringScanMode
{
    PrintableAscii = 0,
    Alphanumeric,
    AsciiText,
    CIdentifiers,
};

StringScanMode stringScanModeFromIndex(int index);

struct StringScanState
{
    QVector<QVariantMap> results;
    QByteArray           run;
    qulonglong           runStart         = 0;
    qulonglong           runLength        = 0;
    qulonglong           offset           = 0;
    int                  resultCount      = 0;
    int                  resultLimit      = kInitialStringResultBatchLimit;
    int                  visibleBaseCount = 0;
    qulonglong           totalResultCount = 0;
    bool                 scanAll          = false;
    bool                 capped           = false;
    qulonglong           nextOffset       = 0;
    QElapsedTimer        elapsed;
};

QString displayString(QByteArrayView bytes);
QString hexOffsetString(qulonglong offset);
QString exportStringLine(const QString &text, qulonglong offset, bool prefixHexOffset);

bool isAsciiStringByte(unsigned char ch);
bool isAlphanumericByte(unsigned char ch);
bool isAsciiWhitespaceByte(unsigned char ch);
bool isAlphaOrUnderscoreByte(unsigned char ch);
bool isIdentifierByte(unsigned char ch);
bool isAsciiTextByte(unsigned char ch, bool includeWhitespace);
bool acceptsByte(StringScanMode mode, unsigned char ch, bool includeWhitespace);
bool acceptsFirstByte(StringScanMode mode, unsigned char ch, bool includeWhitespace);

void flushAsciiRun(StringScanState &state, int minLength, qulonglong resumeOffset, bool terminated,
                   QTextStream *exportStream, bool prefixHexOffset);

void scanAsciiChunk(StringScanState &state, const QByteArray &chunk, int minLength, StringScanMode mode,
                    bool includeWhitespace, QTextStream *exportStream, bool prefixHexOffset);

} // namespace stringscan
