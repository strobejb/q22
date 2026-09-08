#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>

class QIODevice;

namespace filestats
{

struct ChecksumScanCallbacks
{
    std::function<bool()> shouldContinue;
    std::function<void(int)> progress;
};

QStringList checksumAlgorithmNames();
QHash<QString, QString> unavailableChecksums(const QStringList &algorithms, const QString &message);

QHash<QString, QString> calculateChecksums(QIODevice &input,
                                           const QStringList &algorithms,
                                           const ChecksumScanCallbacks &callbacks = {});

} // namespace filestats
