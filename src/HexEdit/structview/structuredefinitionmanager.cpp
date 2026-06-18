#include "structview/structuredefinitionmanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QSettings>
#include <QStandardPaths>

namespace
{
void collectAssocExtensions(ExprNode *expr, QStringList *extensions)
{
    if (!expr || !extensions)
        return;

    if (expr->type == EXPR_COMMA)
    {
        collectAssocExtensions(expr->left, extensions);
        collectAssocExtensions(expr->right, extensions);
        return;
    }

    if (expr->type == EXPR_STRINGBUF && expr->str)
    {
        QString ext = QString::fromLocal8Bit(expr->str).trimmed().toLower();
        if (!ext.isEmpty() && !ext.startsWith(QLatin1Char('.')))
            ext.prepend(QLatin1Char('.'));
        if (!ext.isEmpty() && !extensions->contains(ext))
            extensions->push_back(ext);
    }
}

QString settingsSiblingDir(const QString &name)
{
    const QFileInfo settingsFile(QSettings().fileName());
    return settingsFile.dir().filePath(name);
}

QStringList existingDefinitionFilesInDir(const QString &dirPath, bool includeLegacyExtensions)
{
    QDir dir(dirPath);
    if (!dir.exists())
        return {};

    QStringList nameFilters = { QStringLiteral("*.struct") };
    if (includeLegacyExtensions)
    {
        nameFilters.push_back(QStringLiteral("*.txt"));
        nameFilters.push_back(QStringLiteral("*.bstruct"));
    }

    QStringList files;
    const QFileInfoList entries = dir.entryInfoList(
        nameFilters,
        QDir::Files | QDir::Readable,
        QDir::Name | QDir::IgnoreCase);

    for (const QFileInfo &entry : entries)
        files.push_back(entry.absoluteFilePath());

    return files;
}
}

StructureDefinitionManager::StructureDefinitionManager(QObject *parent)
    : QObject(parent)
    , m_watcher(new QFileSystemWatcher(this))
{
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(200);

    connect(&m_reloadTimer, &QTimer::timeout,
            this, &StructureDefinitionManager::reload);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, [this](const QString &) { scheduleReload(); });
    connect(m_watcher, &QFileSystemWatcher::fileChanged,
            this, [this](const QString &) { scheduleReload(); });
}

TypeLibrary *StructureDefinitionManager::library() const
{
    return m_library.get();
}

QStringList StructureDefinitionManager::definitionFiles() const
{
    return m_definitionFiles;
}

QList<ExportedStructureType> StructureDefinitionManager::exportedTypes() const
{
    QList<ExportedStructureType> types;
    if (!m_library)
        return types;

    for (TypeDecl *decl : m_library->globalTypeDeclList)
    {
        if (!decl || !FindTag(decl->tagList, TOK_EXPORT, nullptr))
            continue;

        FILE_DESC *fileDesc = decl->fileRef.fileDesc;
        ExportedStructureType type;
        type.typeDecl = decl;
        if (fileDesc)
        {
            type.filePath = QString::fromLocal8Bit(fileDesc->filePath);
            type.fileName = QString::fromLocal8Bit(fileDesc->fileName);
        }
        ExprNode *assocExpr = nullptr;
        if (FindTag(decl->tagList, TOK_ASSOC, &assocExpr))
            collectAssocExtensions(assocExpr, &type.assocExtensions);
        types.push_back(type);
    }

    return types;
}

QString StructureDefinitionManager::lastError() const
{
    return m_lastError;
}

bool StructureDefinitionManager::isLoaded() const
{
    return m_loaded;
}

QString StructureDefinitionManager::userStructsDir() const
{
    if (!m_userDirOverride.isEmpty())
        return m_userDirOverride;

    return settingsSiblingDir(QStringLiteral("structs"));
}

QStringList StructureDefinitionManager::builtinStructDirs() const
{
    if (!m_builtinDirsOverride.isEmpty())
        return m_builtinDirsOverride;

    QStringList dirs;
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty())
        dirs.push_back(QDir(appDir).filePath(QStringLiteral("typelib")));

    const QStringList dataDirs = QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation,
        QStringLiteral("hexedit/typelib"),
        QStandardPaths::LocateDirectory);
    for (const QString &dir : dataDirs)
        if (!dirs.contains(dir))
            dirs.push_back(dir);

    return dirs;
}

void StructureDefinitionManager::setBuiltinStructDirsForTests(const QStringList &dirs)
{
    m_builtinDirsOverride = dirs;
}

void StructureDefinitionManager::setUserStructsDirForTests(const QString &dir)
{
    m_userDirOverride = dir;
}

bool StructureDefinitionManager::reload()
{
    QDir().mkpath(userStructsDir());

    const QStringList files = discoverDefinitionFiles();
    auto              nextLibrary = std::make_unique<TypeLibrary>();
    QString           errorMessage;
    if (!parseFiles(files, nextLibrary.get(), &errorMessage))
    {
        m_loaded = true;
        m_lastError = errorMessage;
        updateWatchedFiles(files);
        emit reloadFailed(errorMessage);
        return false;
    }

    m_library = std::move(nextLibrary);
    m_definitionFiles = files;
    m_lastError.clear();
    m_loaded = true;
    updateWatchedFiles(files);
    emit definitionsReloaded();
    return true;
}

void StructureDefinitionManager::ensureLoaded()
{
    if (!m_loaded)
        reload();
}

QStringList StructureDefinitionManager::discoverDefinitionFiles() const
{
    QStringList files;

    for (const QString &dir : builtinStructDirs())
        files.append(existingDefinitionFilesInDir(dir, false));

    const QStringList userFiles = existingDefinitionFilesInDir(userStructsDir(), true);
    for (const QString &file : userFiles)
        if (!files.contains(file))
            files.push_back(file);

    return files;
}

bool StructureDefinitionManager::parseFiles(const QStringList &files, TypeLibrary *library, QString *errorMessage) const
{
    for (const QString &file : files)
    {
        Parser parser(library);
        const QByteArray nativePath = QDir::toNativeSeparators(file).toLocal8Bit();
        if (!parser.Ooof(nativePath.constData()))
        {
            if (errorMessage)
            {
                const QString detail = QString::fromLocal8Bit(parser.LastErrStr());
                *errorMessage = detail.isEmpty()
                                    ? tr("Failed to parse %1").arg(QFileInfo(file).fileName())
                                    : tr("%1: %2").arg(QFileInfo(file).fileName(), detail);
            }
            return false;
        }
    }

    return true;
}

void StructureDefinitionManager::updateWatchedFiles(const QStringList &files)
{
    const QStringList watchedFiles = m_watcher->files();
    if (!watchedFiles.isEmpty())
        m_watcher->removePaths(watchedFiles);

    const QStringList watchedDirs = m_watcher->directories();
    if (!watchedDirs.isEmpty())
        m_watcher->removePaths(watchedDirs);

    const QString userDir = userStructsDir();
    QDir().mkpath(userDir);
    m_watcher->addPath(userDir);

    QStringList userFiles;
    const QString userDirAbs = QFileInfo(userDir).absoluteFilePath();
    for (const QString &file : files)
    {
        if (QFileInfo(file).absolutePath() == userDirAbs)
            userFiles.push_back(file);
    }
    if (!userFiles.isEmpty())
        m_watcher->addPaths(userFiles);
}

void StructureDefinitionManager::scheduleReload()
{
    m_reloadTimer.start();
}
