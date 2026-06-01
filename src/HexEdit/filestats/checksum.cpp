#include "filestats/sidepanel.h"

#include "HexView/hexview.h"
#include "filestats/widgets.h"
#include "theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLabel>
#include <QMetaObject>
#include <QPointer>
#include <QProgressBar>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QToolButton>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <utility>

static QStringList checksumAlgorithmNames()
{
    return {
        QStringLiteral("CRC32"),
        QStringLiteral("CRC16"),
        QStringLiteral("SHA256"),
        QStringLiteral("SHA1"),
        QStringLiteral("MD5"),
        QStringLiteral("MD4"),
        QStringLiteral("MD2"),
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
    for (unsigned char byte : data) {
        crc ^= quint16(byte) << 8;
        for (int i = 0; i < 8; ++i)
            crc = (crc & 0x8000) ? quint16((crc << 1) ^ 0x1021) : quint16(crc << 1);
    }
    return crc;
}

static quint32 updateCrc32Iso(quint32 crc, const QByteArray &data)
{
    for (unsigned char byte : data) {
        crc ^= byte;
        for (int i = 0; i < 8; ++i)
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    return crc;
}

class Md2Hasher
{
public:
    void addData(const QByteArray &data)
    {
        m_partial.append(data);
        while (m_partial.size() >= 16) {
            processMessageBlock(reinterpret_cast<const unsigned char *>(m_partial.constData()));
            m_partial.remove(0, 16);
        }
    }

    QByteArray result()
    {
        const int padLen = 16 - (m_partial.size() % 16);
        m_partial.append(QByteArray(padLen, char(padLen)));
        while (!m_partial.isEmpty()) {
            processMessageBlock(reinterpret_cast<const unsigned char *>(m_partial.constData()));
            m_partial.remove(0, 16);
        }
        transformBlock(m_checksum.data());
        return QByteArray(reinterpret_cast<const char *>(m_state.data()), 16);
    }

private:
    static constexpr std::array<unsigned char, 256> s = {
        41, 46, 67, 201, 162, 216, 124, 1, 61, 54, 84, 161, 236, 240, 6, 19,
        98, 167, 5, 243, 192, 199, 115, 140, 152, 147, 43, 217, 188, 76, 130, 202,
        30, 155, 87, 60, 253, 212, 224, 22, 103, 66, 111, 24, 138, 23, 229, 18,
        190, 78, 196, 214, 218, 158, 222, 73, 160, 251, 245, 142, 187, 47, 238, 122,
        169, 104, 121, 145, 21, 178, 7, 63, 148, 194, 16, 137, 11, 34, 95, 33,
        128, 127, 93, 154, 90, 144, 50, 39, 53, 62, 204, 231, 191, 247, 151, 3,
        255, 25, 48, 179, 72, 165, 181, 209, 215, 94, 146, 42, 172, 86, 170, 198,
        79, 184, 56, 210, 150, 164, 125, 182, 118, 252, 107, 226, 156, 116, 4, 241,
        69, 157, 112, 89, 100, 113, 135, 32, 134, 91, 207, 101, 230, 45, 168, 2,
        27, 96, 37, 173, 174, 176, 185, 246, 28, 70, 97, 105, 52, 64, 126, 15,
        85, 71, 163, 35, 221, 81, 175, 58, 195, 92, 249, 206, 186, 197, 234, 38,
        44, 83, 13, 110, 133, 40, 132, 9, 211, 223, 205, 244, 65, 129, 77, 82,
        106, 220, 55, 200, 108, 193, 171, 250, 36, 225, 123, 8, 12, 189, 177, 74,
        120, 136, 149, 139, 227, 99, 232, 109, 233, 203, 213, 254, 59, 0, 29, 57,
        242, 239, 183, 14, 102, 88, 208, 228, 166, 119, 114, 248, 235, 117, 75, 10,
        49, 68, 80, 180, 143, 237, 31, 26, 219, 153, 141, 51, 159, 17, 131, 20
    };

    void processMessageBlock(const unsigned char *block)
    {
        unsigned char l = m_checksum[15];
        for (int i = 0; i < 16; ++i) {
            m_checksum[i] ^= s[block[i] ^ l];
            l = m_checksum[i];
        }
        transformBlock(block);
    }

    void transformBlock(const unsigned char *block)
    {
        for (int i = 0; i < 16; ++i) {
            m_state[16 + i] = block[i];
            m_state[32 + i] = m_state[16 + i] ^ m_state[i];
        }
        unsigned char t = 0;
        for (int round = 0; round < 18; ++round) {
            for (int i = 0; i < 48; ++i) {
                m_state[i] ^= s[t];
                t = m_state[i];
            }
            t = static_cast<unsigned char>(t + round);
        }
    }

    QByteArray m_partial;
    std::array<unsigned char, 16> m_checksum = {};
    std::array<unsigned char, 48> m_state = {};
};

static QHash<QString, QString> unavailableChecksums(const QStringList &algorithms, const QString &message)
{
    QHash<QString, QString> results;
    for (const QString &name : algorithms)
        results.insert(name, message);
    return results;
}

static QHash<QString, QString> calculateChecksums(
    const QString &path,
    const QStringList &algorithms,
    const std::shared_ptr<std::atomic_bool> &cancelFlag,
    const std::shared_ptr<filestats::OperationPause> &pause,
    const std::function<void(int)> &progressCallback)
{
    const QSet<QString> selected(algorithms.cbegin(), algorithms.cend());
    if (selected.isEmpty())
        return {};

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return unavailableChecksums(algorithms, QFileInfo(path).exists()
                                    ? FilePropertiesPanel::tr("Unable to read")
                                    : FilePropertiesPanel::tr("No file"));

    Md2Hasher md2;
    QCryptographicHash md4(QCryptographicHash::Md4);
    QCryptographicHash md5(QCryptographicHash::Md5);
    QCryptographicHash sha1(QCryptographicHash::Sha1);
    QCryptographicHash sha256(QCryptographicHash::Sha256);
    quint16 crc16 = 0xFFFF;
    quint32 crc32 = 0xFFFFFFFFu;
    const qint64 total = file.size();
    qint64 scanned = 0;
    int lastProgress = -1;

    while (!cancelFlag->load() && !file.atEnd()) {
        if (pause && !pause->waitIfPaused(cancelFlag))
            return {};
        const QByteArray chunk = file.read(1024 * 1024);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError)
            return unavailableChecksums(algorithms, FilePropertiesPanel::tr("Read failed"));

        if (selected.contains(QStringLiteral("MD2")))
            md2.addData(chunk);
        if (selected.contains(QStringLiteral("MD4")))
            md4.addData(QByteArrayView(chunk.constData(), chunk.size()));
        if (selected.contains(QStringLiteral("MD5")))
            md5.addData(QByteArrayView(chunk.constData(), chunk.size()));
        if (selected.contains(QStringLiteral("SHA1")))
            sha1.addData(QByteArrayView(chunk.constData(), chunk.size()));
        if (selected.contains(QStringLiteral("SHA256")))
            sha256.addData(QByteArrayView(chunk.constData(), chunk.size()));
        if (selected.contains(QStringLiteral("CRC16")))
            crc16 = updateCrc16Ccitt(crc16, chunk);
        if (selected.contains(QStringLiteral("CRC32")))
            crc32 = updateCrc32Iso(crc32, chunk);

        scanned += chunk.size();
        const int progress = total > 0
                                 ? static_cast<int>((scanned * 1000) / total)
                                 : 1000;
        if (progress != lastProgress) {
            lastProgress = progress;
            progressCallback(progress);
        }
    }
    if (cancelFlag->load())
        return {};

    QHash<QString, QString> results;
    if (selected.contains(QStringLiteral("MD2")))
        results.insert(QStringLiteral("MD2"), hexDigest(md2.result()));
    if (selected.contains(QStringLiteral("MD4")))
        results.insert(QStringLiteral("MD4"), hexDigest(md4.result()));
    if (selected.contains(QStringLiteral("MD5")))
        results.insert(QStringLiteral("MD5"), hexDigest(md5.result()));
    if (selected.contains(QStringLiteral("SHA1")))
        results.insert(QStringLiteral("SHA1"), hexDigest(sha1.result()));
    if (selected.contains(QStringLiteral("SHA256")))
        results.insert(QStringLiteral("SHA256"), hexDigest(sha256.result()));
    if (selected.contains(QStringLiteral("CRC16")))
        results.insert(QStringLiteral("CRC16"), hexNumber(crc16, 4));
    if (selected.contains(QStringLiteral("CRC32")))
        results.insert(QStringLiteral("CRC32"), hexNumber(crc32 ^ 0xFFFFFFFFu, 8));
    return results;
}

QStringList FilePropertiesPanel::selectedChecksumAlgorithms() const
{
    QStringList algorithms;
    for (const QString &name : checksumAlgorithmNames()) {
        if (QCheckBox *checkBox = m_checksumChecks.value(name)) {
            if (checkBox->isChecked())
                algorithms.append(name);
        }
    }
    return algorithms;
}

void FilePropertiesPanel::markChecksumAlgorithmsChanged()
{
    m_checksumStarted = false;
    m_checksumRescanRequired = true;
    m_checksumRescanMessage = tr("Algorithm changed");
    ++m_checksumGeneration;
    if (m_checksumCancel)
        m_checksumCancel->store(true);
    if (m_checksumPause)
        m_checksumPause->wake();
    resetChecksumTitle();
    if (m_checksumOperation)
        m_checksumOperation->showRescan(m_checksumRescanMessage);
}

void FilePropertiesPanel::maybeStartChecksumCalculation()
{
    if (!m_panelFullyOpened)
        return;
    if (m_checksumSectionCollapsed)
        return;
    if (m_checksumStarted)
        return;
    if (m_checksumRescanRequired) {
        if (m_checksumOperation)
            m_checksumOperation->showRescan(m_checksumRescanMessage.isEmpty()
                                            ? tr("File contents changed")
                                            : m_checksumRescanMessage);
        return;
    }
    if (m_checksumAutoStartConsumed)
        return;
    if (!shouldAutoStartOperations()) {
        if (m_checksumOperation && !m_checksumOperation->hasOperation())
            m_checksumOperation->showStart(tr("Begin scan"));
        return;
    }
    m_checksumStarted = true;
    startChecksumCalculation();
}

void FilePropertiesPanel::setChecksumRowsPending()
{
    const QStringList algorithms = selectedChecksumAlgorithms();
    const QSet<QString> selected(algorithms.cbegin(), algorithms.cend());
    for (auto it = m_checksumValues.begin(); it != m_checksumValues.end(); ++it)
        it.value()->setText(selected.contains(it.key()) ? tr("Calculating...")
                                                        : tr("Not selected"));
    setChecksumProgressTitle(0);
    if (m_checksumOperation)
        m_checksumOperation->showProgress();
}

void FilePropertiesPanel::startChecksumCalculation()
{
    if (!m_hexView) {
        m_checksumStarted = false;
        return;
    }

    m_checksumAutoStartConsumed = true;
    m_checksumRescanRequired = false;
    m_checksumRescanMessage.clear();
    const int generation = ++m_checksumGeneration;
    if (m_checksumCancel)
        m_checksumCancel->store(true);
    if (m_checksumPause)
        m_checksumPause->wake();
    auto cancelFlag = std::make_shared<std::atomic_bool>(false);
    m_checksumCancel = cancelFlag;
    auto pause = std::make_shared<filestats::OperationPause>();
    pause->setPaused(m_checksumSectionCollapsed);
    m_checksumPause = pause;
    setChecksumRowsPending();

    const QString path = m_hexView->filePath();
    const QStringList algorithms = selectedChecksumAlgorithms();
    QPointer<FilePropertiesPanel> guard(this);
    auto *thread = QThread::create([guard, generation, path, algorithms, cancelFlag, pause]() {
        const QHash<QString, QString> results = calculateChecksums(path, algorithms, cancelFlag, pause, [guard, generation](int progress) {
            QMetaObject::invokeMethod(qApp, [guard, generation, progress]() {
                if (guard)
                    guard->updateChecksumProgress(generation, progress);
            }, Qt::QueuedConnection);
        });
        if (cancelFlag->load())
            return;
        QMetaObject::invokeMethod(qApp, [guard, generation, results]() {
            if (guard)
                guard->applyChecksumResults(generation, results);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void FilePropertiesPanel::updateChecksumProgress(int generation, int value)
{
    if (generation != m_checksumGeneration || !m_checksumOperation)
        return;
    const int progress = qBound(0, value, 1000);
    m_checksumOperation->progressBar()->setValue(progress);
    setChecksumProgressTitle(progress);
}

void FilePropertiesPanel::applyChecksumResults(int generation, const QHash<QString, QString> &results)
{
    if (generation != m_checksumGeneration)
        return;

    m_checksumStarted = false;
    const QStringList algorithms = selectedChecksumAlgorithms();
    const QSet<QString> selected(algorithms.cbegin(), algorithms.cend());
    for (auto it = m_checksumValues.begin(); it != m_checksumValues.end(); ++it) {
        if (!selected.contains(it.key()))
            it.value()->setText(tr("Not selected"));
    }
    for (auto it = results.cbegin(); it != results.cend(); ++it) {
        if (QLabel *label = m_checksumValues.value(it.key()))
            label->setText(it.value());
    }
    if (m_checksumOperation)
        m_checksumOperation->clear();
    resetChecksumTitle();
    QTimer::singleShot(0, this, [this]() { repairExpandedSectionGeometry(Section::Checksums); });
}

void FilePropertiesPanel::cancelChecksumCalculation()
{
    ++m_checksumGeneration;
    m_checksumStarted = false;
    if (m_checksumCancel)
        m_checksumCancel->store(true);
    if (m_checksumPause)
        m_checksumPause->wake();
    for (QLabel *label : std::as_const(m_checksumValues))
        label->setText(tr("Cancelled"));
    if (m_checksumOperation)
        m_checksumOperation->showRetry(tr("Operation cancelled"));
    resetChecksumTitle();
}
