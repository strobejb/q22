#include "filestats/stringscan.h"

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

void flushAsciiRun(StringScanState &state, int minLength, qulonglong resumeOffset, bool terminated,
                   QTextStream *exportStream, bool prefixHexOffset)
{
    const bool allowRun = state.runLength >= static_cast<qulonglong>(minLength) && terminated;
    if (allowRun)
    {
        const QString text = displayString(QByteArrayView(state.run.constData(), state.run.size()));
        ++state.totalResultCount;
        if (exportStream)
            *exportStream << exportStringLine(text, state.runStart, prefixHexOffset) << '\n';

        const bool mayAppendVisible =
            state.resultCount < state.resultLimit &&
            (!state.scanAll || state.visibleBaseCount + state.resultCount < kMaxStringResultBatchLimit);
        if (mayAppendVisible)
        {
            QVariantMap row;
            row.insert(QStringLiteral("offset"), state.runStart);
            row.insert(QStringLiteral("length"), state.runLength);
            row.insert(QStringLiteral("text"), text);
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
            state.capped     = true;
            state.nextOffset = resumeOffset;
        }
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
        const unsigned char ch       = static_cast<unsigned char>(byte);
        const bool          accepted = state.runLength == 0 ? acceptsFirstByte(mode, ch, includeWhitespace)
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

} // namespace stringscan
