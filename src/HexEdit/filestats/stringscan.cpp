#include "filestats/stringscan.h"

#include <algorithm>
#include <utility>

namespace stringscan
{

StringScanMode stringScanModeFromIndex(int index)
{
    if (index < 0 || index > static_cast<int>(StringScanMode::CIdentifiers))
        return StringScanMode::Alphanumeric;
    return static_cast<StringScanMode>(index);
}

bool isAsciiStringByte(unsigned char ch)
{
    return ch >= 0x20 && ch <= 0x7E;
}

bool isAlphanumericByte(unsigned char ch)
{
    return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

bool isAsciiWhitespaceByte(unsigned char ch)
{
    return ch == ' ' || ch == '\t';
}

bool isAlphaOrUnderscoreByte(unsigned char ch)
{
    return ch == '_' || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

bool isIdentifierByte(unsigned char ch)
{
    return isAlphaOrUnderscoreByte(ch) || (ch >= '0' && ch <= '9');
}

bool isAsciiTextByte(unsigned char ch, bool includeWhitespace)
{
    if (includeWhitespace)
        return isAsciiStringByte(ch) || isAsciiWhitespaceByte(ch);
    return ch >= 0x21 && ch <= 0x7E;
}

bool acceptsByte(StringScanMode mode, unsigned char ch, bool includeWhitespace)
{
    switch (mode)
    {
    case StringScanMode::Alphanumeric:
        return isAlphanumericByte(ch) || (includeWhitespace && isAsciiWhitespaceByte(ch));
    case StringScanMode::AsciiText:
        return isAsciiTextByte(ch, includeWhitespace);
    case StringScanMode::CIdentifiers:
        return isIdentifierByte(ch);
    case StringScanMode::PrintableAscii:
    default:
        return isAsciiStringByte(ch);
    }
}

bool acceptsFirstByte(StringScanMode mode, unsigned char ch, bool includeWhitespace)
{
    if (mode == StringScanMode::CIdentifiers)
        return isAlphaOrUnderscoreByte(ch);
    return acceptsByte(mode, ch, includeWhitespace);
}

QString displayString(QByteArrayView bytes)
{
    QString text = QString::fromLatin1(bytes.data(), bytes.size());
    text.replace(QLatin1Char('\t'), QLatin1Char(' '));

    qsizetype firstText = 0;
    while (firstText < text.size() && text.at(firstText).isSpace())
        ++firstText;
    if (firstText > 0 && firstText < text.size())
        text.remove(0, firstText);

    return text;
}

QString hexOffsetString(qulonglong offset)
{
    return QStringLiteral("%1").arg(offset, 8, 16, QLatin1Char('0')).toUpper();
}

QString exportStringLine(const QString &text, qulonglong offset, bool prefixHexOffset)
{
    if (!prefixHexOffset)
        return text;
    return QStringLiteral("%1 %2").arg(hexOffsetString(offset), text);
}

void appendScanResult(StringScanState &state, const QString &text, qulonglong offset, qulonglong byteLength,
                      qulonglong resumeOffset, QTextStream *exportStream, bool prefixHexOffset,
                      const QString &encoding)
{
    ++state.totalResultCount;
    if (exportStream)
        *exportStream << exportStringLine(text, offset, prefixHexOffset) << '\n';

    const bool mayAppendVisible =
        state.resultCount < state.resultLimit &&
        (!state.scanAll || state.visibleBaseCount + state.resultCount < kMaxStringResultBatchLimit);
    if (mayAppendVisible)
    {
        QVariantMap row;
        row.insert(QStringLiteral("offset"), offset);
        row.insert(QStringLiteral("length"), byteLength);
        row.insert(QStringLiteral("text"), text);
        row.insert(QStringLiteral("encoding"), encoding);
        state.results.append(row);
        ++state.resultCount;
    }

    if (!state.scanAll && state.resultCount >= state.resultLimit)
    {
        if (state.elapsed.isValid() && state.elapsed.elapsed() < kMinimumStringScanMs &&
            state.resultLimit < kMaxStringResultBatchLimit)
        {
            state.resultLimit = qMin(kMaxStringResultBatchLimit, state.resultLimit * 10);
            return;
        }
        state.capped = true;
        state.nextOffset = resumeOffset;
    }
}

void flushAsciiRun(StringScanState &state, int minLength, qulonglong resumeOffset, bool terminated,
                   QTextStream *exportStream, bool prefixHexOffset)
{
    const bool allowRun = state.runLength >= static_cast<qulonglong>(minLength) && terminated;
    if (allowRun)
    {
        const QString text = displayString(QByteArrayView(state.run.constData(), state.run.size()));
        appendScanResult(state, text, state.runStart, state.runLength, resumeOffset, exportStream, prefixHexOffset,
                         QStringLiteral("ASCII"));
    }
    state.run.clear();
    state.runLength = 0;
}

void scanAsciiChunk(StringScanState &state, const QByteArray &chunk, int minLength, StringScanMode mode,
                    bool includeWhitespace, QTextStream *exportStream, bool prefixHexOffset)
{
    for (char byte : chunk)
    {
        if (state.capped)
            return;
        const unsigned char ch = static_cast<unsigned char>(byte);
        const bool accepted = state.runLength == 0 ? acceptsFirstByte(mode, ch, includeWhitespace)
                                                   : acceptsByte(mode, ch, includeWhitespace);
        if (accepted)
        {
            if (state.runLength == 0)
                state.runStart = state.offset;
            if (state.run.size() < kMaxBufferedStringLength)
                state.run.append(byte);
            ++state.runLength;
        }
        else
        {
            flushAsciiRun(state, minLength, state.offset + 1, mode != StringScanMode::CIdentifiers || ch == '\0',
                          exportStream, prefixHexOffset);
        }
        ++state.offset;
    }
}

bool isUnicodeWhitespace(char32_t codePoint)
{
    return codePoint == U' ' || codePoint == U'\t';
}

bool acceptsUnicodeCodePoint(StringScanMode mode, char32_t codePoint, bool includeWhitespace)
{
    if (codePoint == 0 || codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF))
        return false;

    if (codePoint < 0x80)
        return acceptsByte(mode, static_cast<unsigned char>(codePoint), includeWhitespace);

    if (mode == StringScanMode::CIdentifiers)
        return false;

    switch (mode)
    {
    case StringScanMode::Alphanumeric:
        return true;
    case StringScanMode::AsciiText:
        return includeWhitespace || !isUnicodeWhitespace(codePoint);
    case StringScanMode::PrintableAscii:
    default:
        return true;
    }
}

bool isLikelyUtf16AsciiStart(char32_t codePoint)
{
    return isAlphanumericByte(static_cast<unsigned char>(codePoint)) || codePoint == U'_' || codePoint == U'\\' ||
           codePoint == U'/' || codePoint == U'.';
}

void appendCodePoint(QString &text, char32_t codePoint)
{
    if (codePoint <= 0xFFFF)
    {
        text.append(QChar(static_cast<ushort>(codePoint)));
        return;
    }

    codePoint -= 0x10000;
    text.append(QChar(static_cast<ushort>(0xD800 + (codePoint >> 10))));
    text.append(QChar(static_cast<ushort>(0xDC00 + (codePoint & 0x3FF))));
}

struct Utf8ScanState
{
    QString text;
    qulonglong runStart = 0;
    qulonglong byteLength = 0;
    qulonglong charLength = 0;
    bool hasNonAscii = false;
    char32_t pendingCodePoint = 0;
    int pendingExpected = 0;
    int pendingSeen = 0;
    int pendingSequenceLength = 0;
    qulonglong pendingStart = 0;

    bool inRun() const
    {
        return charLength > 0;
    }

    void resetPending()
    {
        pendingCodePoint = 0;
        pendingExpected = 0;
        pendingSeen = 0;
        pendingSequenceLength = 0;
        pendingStart = 0;
    }

    void resetRun()
    {
        text.clear();
        runStart = 0;
        byteLength = 0;
        charLength = 0;
        hasNonAscii = false;
    }

    void flush(StringScanState &shared, int minLength, qulonglong resumeOffset, QTextStream *exportStream,
               bool prefixHexOffset)
    {
        if (charLength >= static_cast<qulonglong>(minLength) && hasNonAscii)
        {
            appendScanResult(shared, text, runStart, byteLength, resumeOffset, exportStream, prefixHexOffset,
                             QStringLiteral("UTF-8"));
        }
        resetRun();
    }

    void accept(StringScanState &shared, char32_t codePoint, qulonglong startOffset, int encodedLength,
                int minLength, StringScanMode mode, bool includeWhitespace, QTextStream *exportStream,
                bool prefixHexOffset)
    {
        if (!acceptsUnicodeCodePoint(mode, codePoint, includeWhitespace))
        {
            flush(shared, minLength, startOffset + encodedLength, exportStream, prefixHexOffset);
            return;
        }

        if (!inRun())
            runStart = startOffset;
        if (text.size() < kMaxBufferedStringLength)
            appendCodePoint(text, codePoint);
        byteLength += static_cast<qulonglong>(encodedLength);
        ++charLength;
        if (codePoint > 0x7F)
            hasNonAscii = true;
    }

    bool consume(StringScanState &shared, unsigned char byte, qulonglong offset, int minLength,
                 StringScanMode mode, bool includeWhitespace, QTextStream *exportStream, bool prefixHexOffset)
    {
        if (pendingExpected == 0)
        {
            if (byte < 0x80)
            {
                accept(shared, byte, offset, 1, minLength, mode, includeWhitespace, exportStream, prefixHexOffset);
                return true;
            }
            if (byte >= 0xC2 && byte <= 0xDF)
            {
                pendingCodePoint = byte & 0x1F;
                pendingExpected = 1;
                pendingSeen = 0;
                pendingSequenceLength = 2;
                pendingStart = offset;
                return true;
            }
            if (byte >= 0xE0 && byte <= 0xEF)
            {
                pendingCodePoint = byte & 0x0F;
                pendingExpected = 2;
                pendingSeen = 0;
                pendingSequenceLength = 3;
                pendingStart = offset;
                return true;
            }
            if (byte >= 0xF0 && byte <= 0xF4)
            {
                pendingCodePoint = byte & 0x07;
                pendingExpected = 3;
                pendingSeen = 0;
                pendingSequenceLength = 4;
                pendingStart = offset;
                return true;
            }

            flush(shared, minLength, offset + 1, exportStream, prefixHexOffset);
            return true;
        }

        if ((byte & 0xC0) != 0x80)
        {
            flush(shared, minLength, offset, exportStream, prefixHexOffset);
            resetPending();
            return false;
        }

        pendingCodePoint = (pendingCodePoint << 6) | (byte & 0x3F);
        ++pendingSeen;
        if (pendingSeen < pendingExpected)
            return true;

        const char32_t codePoint = pendingCodePoint;
        const int encodedLength = pendingSequenceLength;
        const qulonglong startOffset = pendingStart;
        resetPending();

        const bool overlong = (encodedLength == 2 && codePoint < 0x80) ||
                              (encodedLength == 3 && codePoint < 0x800) ||
                              (encodedLength == 4 && codePoint < 0x10000);
        if (overlong || codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF))
        {
            flush(shared, minLength, offset + 1, exportStream, prefixHexOffset);
            return true;
        }

        accept(shared, codePoint, startOffset, encodedLength, minLength, mode, includeWhitespace, exportStream,
               prefixHexOffset);
        return true;
    }
};

struct Utf16ScanState
{
    enum class Endian
    {
        Little,
        Big
    };

    Endian endian = Endian::Little;
    int alignment = 0;
    QString encodingName;
    QString text;
    qulonglong runStart = 0;
    qulonglong byteLength = 0;
    qulonglong charLength = 0;
    int wideAsciiEvidence = 0;
    int wrongEndianAsciiEvidence = 0;
    bool hasNonAscii = false;
    bool haveFirstByte = false;
    unsigned char firstByte = 0;
    qulonglong firstByteOffset = 0;
    bool haveHighSurrogate = false;
    char32_t highSurrogate = 0;
    qulonglong highSurrogateOffset = 0;
    int highSurrogateEvidence = 0;
    int highSurrogateWrongEndianEvidence = 0;
    bool havePreviousInputByte = false;
    unsigned char previousInputByte = 0;
    bool haveByteBeforeFirst = false;
    unsigned char byteBeforeFirst = 0;

    bool inRun() const
    {
        return charLength > 0;
    }

    void resetRun()
    {
        text.clear();
        runStart = 0;
        byteLength = 0;
        charLength = 0;
        wideAsciiEvidence = 0;
        wrongEndianAsciiEvidence = 0;
        hasNonAscii = false;
        haveHighSurrogate = false;
        highSurrogate = 0;
        highSurrogateOffset = 0;
        highSurrogateEvidence = 0;
        highSurrogateWrongEndianEvidence = 0;
        haveByteBeforeFirst = false;
        byteBeforeFirst = 0;
    }

    bool hasEncodingEvidence(bool nullTerminated) const
    {
        if (wideAsciiEvidence >= std::min<int>(2, static_cast<int>(charLength)))
            return true;
        return nullTerminated && hasNonAscii && wrongEndianAsciiEvidence == 0;
    }

    void flush(StringScanState &shared, int minLength, qulonglong resumeOffset, bool nullTerminated,
               QTextStream *exportStream, bool prefixHexOffset)
    {
        if (charLength >= static_cast<qulonglong>(minLength) && hasEncodingEvidence(nullTerminated))
        {
            appendScanResult(shared, text, runStart, byteLength, resumeOffset, exportStream, prefixHexOffset,
                             encodingName);
        }
        resetRun();
    }

    void processCodePoint(StringScanState &shared, char32_t codePoint, qulonglong unitOffset, int unitBytes,
                          int evidence, int wrongEvidence, bool nullTerminated, int minLength, StringScanMode mode,
                          bool includeWhitespace, QTextStream *exportStream, bool prefixHexOffset)
    {
        if (nullTerminated || !acceptsUnicodeCodePoint(mode, codePoint, includeWhitespace))
        {
            flush(shared, minLength, unitOffset + unitBytes, nullTerminated, exportStream, prefixHexOffset);
            return;
        }

        if (!inRun())
        {
            runStart = unitOffset;
            if (evidence > 0 && haveByteBeforeFirst && byteBeforeFirst != 0 &&
                !isLikelyUtf16AsciiStart(codePoint))
                wrongEvidence += 1000;
        }
        if (text.size() < kMaxBufferedStringLength)
            appendCodePoint(text, codePoint);
        byteLength += static_cast<qulonglong>(unitBytes);
        ++charLength;
        wideAsciiEvidence += evidence;
        wrongEndianAsciiEvidence += wrongEvidence;
        if (codePoint > 0x7F)
            hasNonAscii = true;
    }

    void processUnit(StringScanState &shared, quint16 unit, unsigned char lowByte, unsigned char highByte,
                     qulonglong unitOffset, int minLength, StringScanMode mode, bool includeWhitespace,
                     QTextStream *exportStream, bool prefixHexOffset)
    {
        const bool asciiUnit = unit == '\t' || (unit >= 0x20 && unit <= 0x7E);
        const int evidence = asciiUnit ? 1 : 0;
        const int wrongEvidence = (lowByte == 0 && ((highByte >= 0x20 && highByte <= 0x7E) || highByte == '\t')) ? 1 : 0;

        if (unit == 0)
        {
            flush(shared, minLength, unitOffset + 2, true, exportStream, prefixHexOffset);
            return;
        }

        if (unit >= 0xD800 && unit <= 0xDBFF)
        {
            if (haveHighSurrogate)
                flush(shared, minLength, unitOffset + 2, false, exportStream, prefixHexOffset);
            haveHighSurrogate = true;
            highSurrogate = unit;
            highSurrogateOffset = unitOffset;
            highSurrogateEvidence = evidence;
            highSurrogateWrongEndianEvidence = wrongEvidence;
            return;
        }

        if (unit >= 0xDC00 && unit <= 0xDFFF)
        {
            if (!haveHighSurrogate)
            {
                flush(shared, minLength, unitOffset + 2, false, exportStream, prefixHexOffset);
                return;
            }

            const char32_t codePoint =
                0x10000 + ((highSurrogate - 0xD800) << 10) + (static_cast<char32_t>(unit) - 0xDC00);
            const int combinedEvidence = highSurrogateEvidence + evidence;
            const int combinedWrongEvidence = highSurrogateWrongEndianEvidence + wrongEvidence;
            const qulonglong combinedOffset = highSurrogateOffset;
            haveHighSurrogate = false;
            processCodePoint(shared, codePoint, combinedOffset, 4, combinedEvidence, combinedWrongEvidence, false,
                             minLength, mode, includeWhitespace, exportStream, prefixHexOffset);
            return;
        }

        if (haveHighSurrogate)
        {
            flush(shared, minLength, unitOffset, false, exportStream, prefixHexOffset);
            haveHighSurrogate = false;
        }

        processCodePoint(shared, unit, unitOffset, 2, evidence, wrongEvidence, false, minLength, mode,
                         includeWhitespace, exportStream, prefixHexOffset);
    }

    void consume(StringScanState &shared, unsigned char byte, qulonglong offset, int minLength, StringScanMode mode,
                 bool includeWhitespace, QTextStream *exportStream, bool prefixHexOffset)
    {
        if (!haveFirstByte)
        {
            if (static_cast<int>(offset % 2) == alignment)
            {
                firstByte = byte;
                firstByteOffset = offset;
                haveFirstByte = true;
                haveByteBeforeFirst = havePreviousInputByte;
                byteBeforeFirst = previousInputByte;
            }
            previousInputByte = byte;
            havePreviousInputByte = true;
            return;
        }

        if (offset != firstByteOffset + 1)
        {
            haveFirstByte = false;
            previousInputByte = byte;
            havePreviousInputByte = true;
            return;
        }

        const unsigned char secondByte = byte;
        const quint16 unit = endian == Endian::Little
                                 ? static_cast<quint16>(firstByte | (secondByte << 8))
                                 : static_cast<quint16>((firstByte << 8) | secondByte);
        const unsigned char lowByte = endian == Endian::Little ? firstByte : secondByte;
        const unsigned char highByte = endian == Endian::Little ? secondByte : firstByte;
        haveFirstByte = false;
        processUnit(shared, unit, lowByte, highByte, firstByteOffset, minLength, mode, includeWhitespace,
                    exportStream, prefixHexOffset);
        previousInputByte = byte;
        havePreviousInputByte = true;
    }
};

bool scanDevice(QIODevice &source, StringScanState &state, int minLength, const StringScanOptions &options,
                QTextStream *exportStream, qint64 chunkSize, const StringScanDeviceCallbacks &callbacks)
{
    if (!source.isOpen() || !source.isReadable())
        return false;

    if (chunkSize <= 0)
        chunkSize = kDefaultStringScanChunkSize;

    auto shouldContinue = [&callbacks]()
    {
        return !callbacks.shouldContinue || callbacks.shouldContinue();
    };
    auto drainResults = [&state, &callbacks]()
    {
        if (callbacks.resultsReady && !state.results.isEmpty())
        {
            QVector<QVariantMap> batch = std::move(state.results);
            state.results.clear();
            callbacks.resultsReady(std::move(batch));
        }
    };

    const bool scanUnicode = options.includeUnicode && options.mode != StringScanMode::CIdentifiers;
    Utf8ScanState utf8;
    Utf16ScanState utf16le{Utf16ScanState::Endian::Little, 0, QStringLiteral("UTF-16LE")};
    Utf16ScanState utf16be{Utf16ScanState::Endian::Big, 0, QStringLiteral("UTF-16BE")};

    while (!source.atEnd() && !state.capped)
    {
        if (!shouldContinue())
            return false;

        const QByteArray chunk = source.read(chunkSize);
        if (chunk.isEmpty())
            return source.atEnd();

        const qulonglong chunkStart = state.offset;
        scanAsciiChunk(state, chunk, minLength, options.mode, options.includeWhitespace, exportStream,
                       options.prefixHexOffset);
        if (scanUnicode && !state.capped)
        {
            for (qsizetype i = 0; i < chunk.size() && !state.capped; ++i)
            {
                const qulonglong offset = chunkStart + static_cast<qulonglong>(i);
                const unsigned char byte = static_cast<unsigned char>(chunk.at(i));
                bool consumed = false;
                do
                {
                    consumed = utf8.consume(state, byte, offset, minLength, options.mode, options.includeWhitespace,
                                            exportStream, options.prefixHexOffset);
                } while (!consumed && !state.capped);
                utf16le.consume(state, byte, offset, minLength, options.mode, options.includeWhitespace,
                                exportStream, options.prefixHexOffset);
                utf16be.consume(state, byte, offset, minLength, options.mode, options.includeWhitespace,
                                exportStream, options.prefixHexOffset);
            }
        }
        drainResults();

        if (callbacks.progress)
        {
            const qint64 scanned = state.capped ? static_cast<qint64>(state.nextOffset) : source.pos();
            callbacks.progress(std::max<qint64>(0, scanned));
        }
    }

    if (!shouldContinue())
        return false;

    if (!state.capped)
    {
        flushAsciiRun(state, minLength, state.offset, options.mode != StringScanMode::CIdentifiers, exportStream,
                      options.prefixHexOffset);
        if (scanUnicode)
        {
            utf8.flush(state, minLength, state.offset, exportStream, options.prefixHexOffset);
            utf16le.flush(state, minLength, state.offset, false, exportStream, options.prefixHexOffset);
            utf16be.flush(state, minLength, state.offset, false, exportStream, options.prefixHexOffset);
        }
    }
    drainResults();

    return true;
}

bool scanAsciiDevice(QIODevice &source, StringScanState &state, int minLength, StringScanMode mode,
                     bool includeWhitespace, QTextStream *exportStream, bool prefixHexOffset, qint64 chunkSize,
                     const StringScanDeviceCallbacks &callbacks)
{
    return scanDevice(source, state, minLength,
                      StringScanOptions{mode, includeWhitespace, false, prefixHexOffset}, exportStream, chunkSize,
                      callbacks);
}

} // namespace stringscan
