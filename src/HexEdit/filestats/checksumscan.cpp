#include "filestats/checksumscan.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QIODevice>
#include <QSet>

#include <array>

namespace filestats
{

QStringList checksumAlgorithmNames()
{
    return {
        QStringLiteral("SHA512"),
        QStringLiteral("SHA256"),
        QStringLiteral("SHA1"),
        QStringLiteral("MD5"),
        QStringLiteral("CRC32"),
        QStringLiteral("CRC32C"),
        QStringLiteral("CRC16"),
    };
}

static QString hexDigest(const QByteArray &digest)
{
    return QString::fromLatin1(digest.toHex());
}

static QString hexNumber(quint32 value, int width)
{
    return QStringLiteral("%1").arg(value, width, 16, QLatin1Char('0')).toUpper();
}

static quint16 updateCrc16Ccitt(quint16 crc, const QByteArray &data)
{
    for (unsigned char byte : data)
    {
        crc ^= quint16(byte) << 8;
        for (int i = 0; i < 8; ++i)
            crc = (crc & 0x8000) ? quint16((crc << 1) ^ 0x1021) : quint16(crc << 1);
    }
    return crc;
}

static quint32 updateCrc32Iso(quint32 crc, const QByteArray &data)
{
    for (unsigned char byte : data)
    {
        crc ^= byte;
        for (int i = 0; i < 8; ++i)
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    return crc;
}

static quint32 updateCrc32Castagnoli(quint32 crc, const QByteArray &data)
{
    for (unsigned char byte : data)
    {
        crc ^= byte;
        for (int i = 0; i < 8; ++i)
            crc = (crc & 1u) ? (crc >> 1) ^ 0x82F63B78u : crc >> 1;
    }
    return crc;
}

QHash<QString, QString> unavailableChecksums(const QStringList &algorithms, const QString &message)
{
    QHash<QString, QString> results;
    for (const QString &name : algorithms)
        results.insert(name, message);
    return results;
}

QHash<QString, QString> calculateChecksums(QIODevice &input,
                                           const QStringList &algorithms,
                                           const ChecksumScanCallbacks &callbacks)
{
    const QSet<QString> selected(algorithms.cbegin(), algorithms.cend());
    if (selected.isEmpty())
        return {};
    if (!input.isOpen() || !input.isReadable())
        return unavailableChecksums(algorithms,
                                    QCoreApplication::translate("FilePropertiesPanel", "Unable to read"));

    QCryptographicHash md5(QCryptographicHash::Md5);
    QCryptographicHash sha1(QCryptographicHash::Sha1);
    QCryptographicHash sha256(QCryptographicHash::Sha256);
    QCryptographicHash sha512(QCryptographicHash::Sha512);
    quint16 crc16  = 0xFFFF;
    quint32 crc32  = 0xFFFFFFFFu;
    quint32 crc32c = 0xFFFFFFFFu;
    const qint64 total = input.size();
    qint64 scanned = 0;
    int lastProgress = -1;

    auto shouldContinue = [&]() -> bool
    {
        return !callbacks.shouldContinue || callbacks.shouldContinue();
    };

    while (!input.atEnd())
    {
        if (!shouldContinue())
            return {};

        const QByteArray chunk = input.read(1024 * 1024);
        if (chunk.isEmpty())
        {
            if (input.atEnd())
                break;
            return unavailableChecksums(algorithms,
                                        QCoreApplication::translate("FilePropertiesPanel", "Read failed"));
        }

        if (selected.contains(QStringLiteral("MD5")))
            md5.addData(QByteArrayView(chunk.constData(), chunk.size()));
        if (selected.contains(QStringLiteral("SHA1")))
            sha1.addData(QByteArrayView(chunk.constData(), chunk.size()));
        if (selected.contains(QStringLiteral("SHA256")))
            sha256.addData(QByteArrayView(chunk.constData(), chunk.size()));
        if (selected.contains(QStringLiteral("SHA512")))
            sha512.addData(QByteArrayView(chunk.constData(), chunk.size()));
        if (selected.contains(QStringLiteral("CRC16")))
            crc16 = updateCrc16Ccitt(crc16, chunk);
        if (selected.contains(QStringLiteral("CRC32")))
            crc32 = updateCrc32Iso(crc32, chunk);
        if (selected.contains(QStringLiteral("CRC32C")))
            crc32c = updateCrc32Castagnoli(crc32c, chunk);

        scanned += chunk.size();
        if (callbacks.progress)
        {
            const int progress = total > 0 ? static_cast<int>((scanned * 1000) / total) : 1000;
            if (progress != lastProgress)
            {
                lastProgress = progress;
                callbacks.progress(progress);
            }
        }
    }

    if (!shouldContinue())
        return {};

    QHash<QString, QString> results;
    if (selected.contains(QStringLiteral("MD5")))
        results.insert(QStringLiteral("MD5"), hexDigest(md5.result()));
    if (selected.contains(QStringLiteral("SHA1")))
        results.insert(QStringLiteral("SHA1"), hexDigest(sha1.result()));
    if (selected.contains(QStringLiteral("SHA256")))
        results.insert(QStringLiteral("SHA256"), hexDigest(sha256.result()));
    if (selected.contains(QStringLiteral("SHA512")))
        results.insert(QStringLiteral("SHA512"), hexDigest(sha512.result()));
    if (selected.contains(QStringLiteral("CRC16")))
        results.insert(QStringLiteral("CRC16"), hexNumber(crc16, 4));
    if (selected.contains(QStringLiteral("CRC32")))
        results.insert(QStringLiteral("CRC32"), hexNumber(crc32 ^ 0xFFFFFFFFu, 8));
    if (selected.contains(QStringLiteral("CRC32C")))
        results.insert(QStringLiteral("CRC32C"), hexNumber(crc32c ^ 0xFFFFFFFFu, 8));
    return results;
}

} // namespace filestats
