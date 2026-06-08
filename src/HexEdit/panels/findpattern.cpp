#include "findpattern.h"

#include <QRegularExpression>
#include <algorithm>
#include <cstring>

static QByteArray hex2Binary(const QString &text)
{
    const QString hex = QString(text).remove(u' ').remove(u'\t');
    if (hex.isEmpty()) return {};
    QByteArray result;
    result.reserve((hex.size() + 1) / 2);
    for (int i = 0; i < hex.size(); i += 2) {
        bool ok;
        uint v = hex.mid(i, qMin(2, hex.size() - i)).toUInt(&ok, 16);
        if (!ok) return {};
        result.append((char)(uint8_t)v);
    }
    return result;
}

static QByteArray text2Binary(const QString &text, SearchDataType type, bool bigEndian)
{
    switch (type) {
    case SearchUTF8:
        return text.toUtf8();
    case SearchUTF16: {
        QByteArray ba(text.size() * 2, '\0');
        memcpy(ba.data(), text.constData(), text.size() * 2);
        if (bigEndian)
            for (int i = 0; i < ba.size(); i += 2)
                std::reverse(ba.data() + i, ba.data() + i + 2);
        return ba;
    }
    case SearchUTF32: {
        const std::u32string u32 = text.toStdU32String();
        QByteArray ba((int)u32.size() * 4, '\0');
        memcpy(ba.data(), u32.data(), u32.size() * 4);
        if (bigEndian)
            for (int i = 0; i < ba.size(); i += 4)
                std::reverse(ba.data() + i, ba.data() + i + 4);
        return ba;
    }
    default:
        return {};
    }
}

static QByteArray num2Binary(const QString &text, int width, bool bigEndian, bool isSigned)
{
    if (text.trimmed().isEmpty()) return {};
    const QStringList tokens =
        text.split(QRegularExpression(QString("[,\\s]+")), Qt::SkipEmptyParts);
    QByteArray result;
    result.reserve(tokens.size() * width);
    for (const QString &tok : tokens) {
        char buf[8] = {};
        bool ok = false;
        quint64 v = 0;
        if (isSigned) {
            const qint64 sv = tok.toLongLong(&ok, 0);
            if (!ok) return {};
            const qint64 min = -(qint64(1) << (width * 8 - 1));
            const qint64 max =  (qint64(1) << (width * 8 - 1)) - 1;
            if (sv < min || sv > max) return {};
            v = quint64(sv);
        } else {
            v = tok.toULongLong(&ok, 0);
            if (!ok) return {};
            const quint64 max = (width == 8) ? ~quint64(0) : ((quint64(1) << (width * 8)) - 1);
            if (v > max) return {};
        }
        memcpy(buf, &v, width);
        if (bigEndian) std::reverse(buf, buf + width);
        result.append(buf, width);
    }
    return result;
}

QByteArray buildFindPattern(const QString &text,
                            SearchDataType type,
                            bool textBigEndian,
                            bool integerBigEndian,
                            bool signedIntegers)
{
    if (text.isEmpty())
        return {};

    switch (type) {
    case SearchHex:
        return hex2Binary(text);
    case SearchUTF8:
    case SearchUTF16:
    case SearchUTF32:
        return text2Binary(text, type, textBigEndian);
    case SearchByte:
        return num2Binary(text, 1, /*bigEndian=*/false, signedIntegers);
    case SearchWord:
        return num2Binary(text, 2, integerBigEndian, signedIntegers);
    case SearchDword:
        return num2Binary(text, 4, integerBigEndian, signedIntegers);
    }
    return {};
}
