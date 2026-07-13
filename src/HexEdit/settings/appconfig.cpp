#include "settings/appconfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace
{
QString organizationName()
{
    const QString name = QCoreApplication::organizationName();
    return name.isEmpty() ? QStringLiteral("catch22") : name;
}

QString applicationName()
{
    const QString name = QCoreApplication::applicationName();
    return name.isEmpty() ? QStringLiteral("q22") : name;
}

QString legacySettingsFilePath()
{
    QSettings probe(QSettings::IniFormat,
                    QSettings::UserScope,
                    organizationName(),
                    applicationName());
    return probe.fileName();
}

bool copyFileIfMissing(const QString &from, const QString &to)
{
    if (!QFile::exists(from) || QFile::exists(to))
        return false;

    QDir().mkpath(QFileInfo(to).absolutePath());
    return QFile::copy(from, to);
}

void copyDirEntriesIfMissing(const QString &fromDir, const QString &toDir)
{
    QDir source(fromDir);
    if (!source.exists())
        return;

    QDir().mkpath(toDir);
    const QFileInfoList entries = source.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries)
    {
        const QString target = QDir(toDir).filePath(entry.fileName());
        if (entry.isDir())
            copyDirEntriesIfMissing(entry.absoluteFilePath(), target);
        else
            copyFileIfMissing(entry.absoluteFilePath(), target);
    }
}
}

QString AppSettings::appConfigDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return QDir(base).filePath(organizationName()
                               + QLatin1Char('/')
                               + applicationName());
}

QString AppSettings::settingsFilePath()
{
    return QDir(appConfigDir()).filePath(applicationName() + QStringLiteral(".ini"));
}

void AppSettings::migrateLegacyConfig()
{
    const QString oldSettings = legacySettingsFilePath();
    const QString oldRoot = QFileInfo(oldSettings).absolutePath();
    const QString newRoot = appConfigDir();
    if (QFileInfo(oldRoot).absoluteFilePath() == QFileInfo(newRoot).absoluteFilePath())
        return;

    copyFileIfMissing(oldSettings, settingsFilePath());
    copyDirEntriesIfMissing(QDir(oldRoot).filePath(QStringLiteral("palettes")),
                            QDir(newRoot).filePath(QStringLiteral("palettes")));
    copyDirEntriesIfMissing(QDir(oldRoot).filePath(QStringLiteral("structs")),
                            QDir(newRoot).filePath(QStringLiteral("structs")));
    copyFileIfMissing(QDir(oldRoot).filePath(QStringLiteral("bookmarks.json")),
                      QDir(newRoot).filePath(QStringLiteral("bookmarks.json")));
    copyFileIfMissing(QDir(oldRoot).filePath(QStringLiteral("bookmarks.ini")),
                      QDir(newRoot).filePath(QStringLiteral("bookmarks.ini")));
}
