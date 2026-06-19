#include "structview/structuredefinitionmanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QSettings>
#include <QStandardPaths>

#include <cstring>

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

bool appendMagicBytes(ExprNode *expr, QByteArray *bytes)
{
    if (!expr || !bytes)
        return false;

    if (expr->type == EXPR_COMMA)
    {
        if (!appendMagicBytes(expr->left, bytes))
            return false;
        return expr->right ? appendMagicBytes(expr->right, bytes) : true;
    }

    if (expr->type == EXPR_STRINGBUF && expr->str)
    {
        bytes->append(expr->str, int(strlen(expr->str)));
        return true;
    }

    if (expr->type == EXPR_NUMBER)
    {
        const INUMTYPE value = Evaluate(expr);
        if (value < 0 || value > 0xff)
            return false;
        bytes->append(char(value & 0xff));
        return true;
    }

    return false;
}

void collectMagicSignatures(Tag *tagList, QList<StructureMagicSignature> *signatures)
{
    if (!signatures)
        return;

    for (Tag *tag = tagList; tag; tag = tag->link)
    {
        if (tag->tok != TOK_MAGIC || !tag->expr)
            continue;

        ExprNode *offsetExpr = tag->expr;
        ExprNode *bytesExpr = nullptr;
        if (tag->byteSequence.empty())
        {
            if (tag->expr->type != EXPR_COMMA)
                continue;
            offsetExpr = tag->expr->left;
            bytesExpr = tag->expr->right;
        }

        if (!offsetExpr)
            continue;

        const INUMTYPE offset = Evaluate(offsetExpr);
        if (offset < 0)
            continue;

        StructureMagicSignature signature;
        signature.offset = static_cast<uint64_t>(offset);
        if (!tag->byteSequence.empty())
        {
            for (const uint8_t byte : tag->byteSequence)
                signature.bytes.append(char(byte));
        }
        else
        {
            if (!bytesExpr)
                continue;
            appendMagicBytes(bytesExpr, &signature.bytes);
        }

        if (!signature.bytes.isEmpty())
            signatures->push_back(signature);
    }
}

QString descriptionFromTags(Tag *tagList)
{
    ExprNode *expr = nullptr;
    if (!FindTag(tagList, TOK_DESCRIPTION, &expr)
        || !expr
        || expr->type != EXPR_STRINGBUF
        || !expr->str)
    {
        return {};
    }

    return QString::fromLocal8Bit(expr->str).trimmed();
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
            this, &StructureDefinitionManager::definitionFilesChanged);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, [this](const QString &) { scheduleChangeNotification(); });
    connect(m_watcher, &QFileSystemWatcher::fileChanged,
            this, [this](const QString &) { scheduleChangeNotification(); });
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
        type.description = descriptionFromTags(decl->tagList);

        ExprNode *assocExpr = nullptr;
        if (FindTag(decl->tagList, TOK_ASSOC, &assocExpr))
            collectAssocExtensions(assocExpr, &type.assocExtensions);
        collectMagicSignatures(decl->tagList, &type.magicSignatures);
        types.push_back(type);
    }

    return types;
}

QString StructureDefinitionManager::lastError() const
{
    return m_lastError;
}

QString StructureDefinitionManager::loadLog() const
{
    return m_loadLog.join(QLatin1Char('\n'));
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
    m_loadLog.clear();
    m_loadLog.push_back(tr("Reloading structure definitions"));
    for (const QString &file : files)
        m_loadLog.push_back(tr("  %1").arg(QDir::toNativeSeparators(file)));

    auto              nextLibrary = std::make_unique<TypeLibrary>();
    QString           errorSummary;
    QString           errorDiagnostic;
    if (!parseFiles(files, nextLibrary.get(), &errorSummary, &errorDiagnostic))
    {
        m_loaded = true;
        m_lastError = errorSummary;
        m_loadLog.push_back(errorSummary);
        if (!errorDiagnostic.isEmpty())
            m_loadLog.push_back(errorDiagnostic);
        updateWatchedFiles(files);
        emit reloadFailed(errorSummary);
        return false;
    }

    m_library = std::move(nextLibrary);
    m_definitionFiles = files;
    m_lastError.clear();
    m_loadLog.push_back(tr("Loaded %1 definition file(s)").arg(files.size()));
    m_loadLog.push_back(tr("Exported type(s): %1").arg(exportedTypes().size()));
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

bool StructureDefinitionManager::parseFiles(const QStringList &files,
                                            TypeLibrary *library,
                                            QString *errorSummary,
                                            QString *errorDiagnostic) const
{
    for (const QString &file : files)
    {
        Parser parser(library);
        const QByteArray nativePath = QDir::toNativeSeparators(file).toLocal8Bit();
        if (!parser.Ooof(nativePath.constData()))
        {
            const QString fileName = QFileInfo(file).fileName();
            if (errorSummary)
                *errorSummary = tr("Failed: %1").arg(fileName);
            if (errorDiagnostic)
                *errorDiagnostic = QString::fromLocal8Bit(parser.LastErrStr()).trimmed();
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

    QStringList dirs;
    for (const QString &dir : builtinStructDirs())
        if (QFileInfo::exists(dir))
            dirs.push_back(QFileInfo(dir).absoluteFilePath());

    const QString userDir = QFileInfo(userStructsDir()).absoluteFilePath();
    QDir().mkpath(userDir);
    dirs.push_back(userDir);

    QStringList watchedDefinitionFiles;
    for (const QString &file : files)
    {
        const QFileInfo info(file);
        if (!info.exists())
            continue;
        const QString absolutePath = info.absoluteFilePath();
        const QString absoluteDir = info.absolutePath();
        if (!watchedDefinitionFiles.contains(absolutePath))
            watchedDefinitionFiles.push_back(absolutePath);
        if (!dirs.contains(absoluteDir))
            dirs.push_back(absoluteDir);
    }

    dirs.removeDuplicates();
    if (!dirs.isEmpty())
        m_watcher->addPaths(dirs);
    if (!watchedDefinitionFiles.isEmpty())
        m_watcher->addPaths(watchedDefinitionFiles);
}

void StructureDefinitionManager::scheduleChangeNotification()
{
    m_reloadTimer.start();
}
