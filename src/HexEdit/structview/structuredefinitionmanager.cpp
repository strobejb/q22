#include "structview/structuredefinitionmanager.h"
#include "structview/structurerenderengine.h"
#include "settings/appconfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHash>
#include <QMap>
#include <QSet>
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
    if (!FindTag(tagList, TOK_EXPORT, &expr)
        || !expr
        || expr->type != EXPR_STRINGBUF
        || !expr->str)
    {
        return {};
    }

    return QString::fromLocal8Bit(expr->str).trimmed();
}

QString categoryFromTags(Tag *tagList)
{
    ExprNode *expr = nullptr;
    if (!FindTag(tagList, TOK_CATEGORY, &expr)
        || !expr
        || expr->type != EXPR_STRINGBUF
        || !expr->str)
    {
        return {};
    }

    return QString::fromLocal8Bit(expr->str).trimmed().toLower();
}

int versionFromTags(Tag *tagList)
{
    ExprNode *expr = nullptr;
    if (!FindTag(tagList, TOK_VERSION, &expr) || !expr)
        return 0;

    const INUMTYPE value = Evaluate(expr);
    return value < 0 ? 0 : static_cast<int>(value);
}

QString exportedRootName(TypeDecl *decl)
{
    if (!decl)
        return {};

    for (Type *type : decl->declList)
        if (type && type->sym)
            return QString::fromLocal8Bit(type->sym->name);

    Type *base = BaseNode(decl->baseType);
    if (base && (base->ty == typeSTRUCT || base->ty == typeUNION) && base->sptr && base->sptr->symbol)
        return QString::fromLocal8Bit(base->sptr->symbol->name);

    return {};
}

QStringList existingDefinitionFilesInDir(const QString &dirPath, bool includeLegacyExtensions)
{
    QDir dir(dirPath);
    if (!dir.exists())
        return {};

    QStringList nameFilters = {
        QStringLiteral("*.strata"),
        QStringLiteral("*.struct")
    };
    if (includeLegacyExtensions)
    {
        nameFilters.push_back(QStringLiteral("*.txt"));
        nameFilters.push_back(QStringLiteral("*.bstruct"));
    }

    const QFileInfoList entries = dir.entryInfoList(
        nameFilters,
        QDir::Files | QDir::Readable,
        QDir::Name | QDir::IgnoreCase);

    QSet<QString> strataBaseNames;
    for (const QFileInfo &entry : entries)
        if (entry.suffix().compare(QStringLiteral("strata"), Qt::CaseInsensitive) == 0)
            strataBaseNames.insert(entry.completeBaseName().toCaseFolded());

    QStringList files;
    for (const QFileInfo &entry : entries)
    {
        if (entry.suffix().compare(QStringLiteral("struct"), Qt::CaseInsensitive) == 0
            && strataBaseNames.contains(entry.completeBaseName().toCaseFolded()))
        {
            continue;
        }
        files.push_back(entry.absoluteFilePath());
    }

    return files;
}

bool fileAlreadyLoadedAsInclude(StrataLibrary *library, const QString &filePath)
{
    if (!library)
        return false;

    const QString canonical = QDir::toNativeSeparators(QFileInfo(filePath).canonicalFilePath());
    const QString absolute = QDir::toNativeSeparators(QFileInfo(filePath).absoluteFilePath());
    for (FILE_DESC *fileDesc : library->globalFileHistory)
    {
        if (!fileDesc || !fileDesc->included)
            continue;
        const QString loadedPath = QDir::toNativeSeparators(QString::fromLocal8Bit(fileDesc->filePath));
        if (loadedPath == canonical || loadedPath == absolute)
            return true;
    }

    return false;
}

void appendDuplicateDefinitionFileLog(const QStringList &selectedFiles,
                                      const QStringList &builtinDirs,
                                      const QString &userDirPath,
                                      const QList<ExportedStructureType> &activeExports,
                                      QStringList *log)
{
    if (!log)
        return;

    const QString userDir = QFileInfo(userDirPath).absoluteFilePath();
    QMap<QString, QStringList> builtinsByName;
    QMap<QString, QStringList> usersByName;
    QSet<QString> pickedPaths;
    QSet<QString> selectedPaths;

    for (const ExportedStructureType &type : activeExports)
        if (!type.filePath.isEmpty())
            pickedPaths.insert(QDir::toNativeSeparators(QFileInfo(type.filePath).absoluteFilePath()));

    for (const QString &file : selectedFiles)
        selectedPaths.insert(QDir::toNativeSeparators(QFileInfo(file).absoluteFilePath()));

    QStringList candidateFiles;
    for (const QString &dir : builtinDirs)
    {
        for (const QString &file : existingDefinitionFilesInDir(dir, false))
            if (!candidateFiles.contains(file))
                candidateFiles.push_back(file);
    }
    for (const QString &file : existingDefinitionFilesInDir(userDirPath, true))
        if (!candidateFiles.contains(file))
            candidateFiles.push_back(file);

    for (const QString &file : candidateFiles)
    {
        const QFileInfo info(file);
        const QString name = info.completeBaseName();
        const QString path = QDir::toNativeSeparators(info.absoluteFilePath());
        if (info.absoluteFilePath().startsWith(userDir + QLatin1Char('/')))
            usersByName[name].push_back(path);
        else
            builtinsByName[name].push_back(path);
    }

    for (auto it = usersByName.constBegin(); it != usersByName.constEnd(); ++it)
    {
        const QString name = it.key();
        if (!builtinsByName.contains(name))
            continue;

        log->push_back(QObject::tr("Definition file %1: user and built-in copies are both present").arg(name));
        for (const QString &path : builtinsByName.value(name))
        {
            const QString prefix = pickedPaths.contains(path) ? QObject::tr("picked")
                : selectedPaths.contains(path) ? QObject::tr("loaded")
                                               : QObject::tr("ignored");
            log->push_back(QObject::tr("  built-in(%1): %2").arg(prefix, path));
        }
        for (const QString &path : it.value())
        {
            const QString prefix = pickedPaths.contains(path) ? QObject::tr("picked")
                : selectedPaths.contains(path) ? QObject::tr("loaded")
                                               : QObject::tr("ignored");
            log->push_back(QObject::tr("  user(%1): %2").arg(prefix, path));
        }
    }
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

StrataLibrary *StructureDefinitionManager::library() const
{
    return m_library.get();
}

QStringList StructureDefinitionManager::definitionFiles() const
{
    return m_definitionFiles;
}

QList<ExportedStructureType> StructureDefinitionManager::exportedTypes() const
{
    return resolvedExportedTypes();
}

QList<ExportedStructureType> StructureDefinitionManager::resolvedExportedTypes(QStringList *resolutionLog) const
{
    QList<ExportedStructureType> types;
    if (!m_library)
        return types;

    // A file that fails partway through can still have left some declarations
    // parsed (whatever came before the syntax error) in the shared library. Treat
    // the whole file as failed for listing purposes so the dropdown doesn't show a
    // stale/partial type alongside its own "failed to load" entry.
    QSet<QString> failedFilePaths;
    for (const FailedStructureFile &failure : m_failedFiles)
        failedFilePaths.insert(QDir::fromNativeSeparators(QFileInfo(failure.filePath).absoluteFilePath()));

    const QString userDir = QFileInfo(userStrataDir()).absoluteFilePath();

    for (TypeDecl *decl : m_library->globalTypeDeclList)
    {
        if (!decl || !decl->exported || !FindTag(decl->tagList, TOK_EXPORT, nullptr))
            continue;

        FILE_DESC *fileDesc = decl->fileRef.fileDesc;
        if (fileDesc && fileDesc->included)
            continue;
        if (fileDesc
            && failedFilePaths.contains(QDir::fromNativeSeparators(QFileInfo(QString::fromLocal8Bit(fileDesc->filePath)).absoluteFilePath())))
        {
            continue;
        }

        ExportedStructureType type;
        type.typeDecl = decl;
        if (fileDesc)
        {
            type.filePath = QString::fromLocal8Bit(fileDesc->filePath);
            type.fileName = QFileInfo(type.filePath).fileName();
        }
        type.description = descriptionFromTags(decl->tagList);
        type.category = categoryFromTags(decl->tagList);
        type.version = versionFromTags(decl->tagList);
        type.userDefinition = !type.filePath.isEmpty()
            && QFileInfo(type.filePath).absoluteFilePath().startsWith(userDir + QLatin1Char('/'));

        ExprNode *assocExpr = nullptr;
        if (FindTag(decl->tagList, TOK_ASSOC, &assocExpr))
            collectAssocExtensions(assocExpr, &type.assocExtensions);
        collectMagicSignatures(decl->tagList, &type.magicSignatures);
        types.push_back(type);
    }

    QHash<QString, QVector<int>> groups;
    QStringList keysInOrder;
    for (int i = 0; i < types.size(); ++i)
    {
        QString key = types[i].description;
        if (key.isEmpty())
            key = exportedRootName(types[i].typeDecl);
        if (key.isEmpty())
            key = types[i].fileName;

        if (!groups.contains(key))
            keysInOrder.push_back(key);
        groups[key].push_back(i);
    }

    QList<ExportedStructureType> selected;
    for (const QString &key : keysInOrder)
    {
        const QVector<int> indexes = groups.value(key);
        if (indexes.isEmpty())
            continue;

        int best = indexes.first();
        for (int index : indexes)
        {
            const ExportedStructureType &candidate = types[index];
            const ExportedStructureType &current = types[best];
            if (candidate.version > current.version
                || (candidate.version == current.version && candidate.userDefinition && !current.userDefinition))
            {
                best = index;
            }
        }

        selected.push_back(types[best]);
        if (resolutionLog && indexes.size() > 1)
        {
            const ExportedStructureType &winner = types[best];
            resolutionLog->push_back(tr("Export %1: picked: %2 %3 version %4")
                                         .arg(key,
                                              winner.userDefinition ? tr("user") : tr("built-in"),
                                              winner.fileName)
                                         .arg(winner.version));
            for (int index : indexes)
            {
                if (index == best)
                    continue;
                const ExportedStructureType &ignored = types[index];
                resolutionLog->push_back(tr("Export %1: ignored: %2 %3 version %4")
                                             .arg(key,
                                                  ignored.userDefinition ? tr("user") : tr("built-in"),
                                                  ignored.fileName)
                                             .arg(ignored.version));
            }
        }
    }

    return selected;
}

QList<FailedStructureFile> StructureDefinitionManager::failedFiles() const
{
    return m_failedFiles;
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

QString StructureDefinitionManager::userStrataDir() const
{
    if (!m_userDirOverride.isEmpty())
        return m_userDirOverride;

    return QDir(AppSettings::appConfigDir()).filePath(QStringLiteral("strata"));
}

QStringList StructureDefinitionManager::builtinStructDirs() const
{
    if (!m_builtinDirsOverride.isEmpty())
        return m_builtinDirsOverride;

    QStringList dirs;
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty())
        dirs.push_back(QDir(appDir).filePath(QStringLiteral("strata")));

    for (const QString &relativePath : {QStringLiteral("q22/strata"),
                                        QStringLiteral("hexedit/strata")}) {
        const QStringList dataDirs = QStandardPaths::locateAll(
            QStandardPaths::GenericDataLocation,
            relativePath,
            QStandardPaths::LocateDirectory);
        for (const QString &dir : dataDirs)
            if (!dirs.contains(dir))
                dirs.push_back(dir);
    }

    return dirs;
}

void StructureDefinitionManager::setBuiltinStructDirsForTests(const QStringList &dirs)
{
    m_builtinDirsOverride = dirs;
}

void StructureDefinitionManager::setUserStrataDirForTests(const QString &dir)
{
    m_userDirOverride = dir;
}

bool StructureDefinitionManager::reload()
{
    QDir().mkpath(userStrataDir());

    const QStringList files = discoverDefinitionFiles();
    m_loadLog.clear();
    m_loadLog.push_back(tr("Reloading structure definitions"));
    for (const QString &file : files)
        m_loadLog.push_back(tr("  %1").arg(QDir::toNativeSeparators(file)));

    // Each file is parsed independently into the same shared library: a broken
    // file is skipped (and reported via m_failedFiles) rather than discarding the
    // declarations already parsed from every other, perfectly valid file.
    auto                        nextLibrary = std::make_unique<StrataLibrary>();
    QList<FailedStructureFile>  failures;
    const bool                  allOk = parseFiles(files, nextLibrary.get(), &failures);

    m_library = std::move(nextLibrary);
    m_definitionFiles = files;
    m_failedFiles = failures;

    // Catches expression mistakes that only fail silently at render time
    // otherwise: unresolvable static field references, and root offset(...)
    // expressions that need the live render context before one exists.
    const QStringList staticFieldErrors = StructureRenderEngine::validateStaticFieldReferences(m_library.get());

    if (!failures.isEmpty())
    {
        m_lastError = failures.size() == 1
            ? tr("Failed: %1").arg(failures.first().fileName)
            : tr("%1 of %2 definition files failed to load").arg(failures.size()).arg(files.size());
        for (const FailedStructureFile &failure : failures)
        {
            m_loadLog.push_back(tr("Failed: %1").arg(failure.fileName));
            if (!failure.message.isEmpty())
                m_loadLog.push_back(failure.message);
        }
    }
    else if (!staticFieldErrors.isEmpty())
    {
        m_lastError = staticFieldErrors.size() == 1
            ? staticFieldErrors.first()
            : tr("%1 unresolvable field reference(s) in loaded definitions").arg(staticFieldErrors.size());
    }
    else
    {
        m_lastError.clear();
    }

    if (!staticFieldErrors.isEmpty())
    {
        m_loadLog.push_back(tr("%1 static expression issue(s):").arg(staticFieldErrors.size()));
        for (const QString &message : staticFieldErrors)
            m_loadLog.push_back(QStringLiteral("  ") + message);
    }

    QStringList exportResolutionLog;
    const QList<ExportedStructureType> activeExports = resolvedExportedTypes(&exportResolutionLog);
    appendDuplicateDefinitionFileLog(files, builtinStructDirs(), userStrataDir(), activeExports, &m_loadLog);
    for (const QString &message : exportResolutionLog)
        m_loadLog.push_back(message);

    m_loadLog.push_back(tr("Loaded %1 definition file(s)").arg(files.size()));
    m_loadLog.push_back(tr("Exported type(s): %1").arg(activeExports.size()));
    m_loaded = true;
    updateWatchedFiles(files);
    emit definitionsReloaded();
    return allOk && staticFieldErrors.isEmpty();
}

void StructureDefinitionManager::ensureLoaded()
{
    if (!m_loaded)
        reload();
}

QStringList StructureDefinitionManager::discoverDefinitionFiles() const
{
    const QStringList userFiles = existingDefinitionFilesInDir(userStrataDir(), true);
    QSet<QString> userBaseNames;
    for (const QString &file : userFiles)
        userBaseNames.insert(QFileInfo(file).completeBaseName().toCaseFolded());

    QStringList files;
    for (const QString &dir : builtinStructDirs())
    {
        const QStringList builtinFiles = existingDefinitionFilesInDir(dir, false);
        for (const QString &file : builtinFiles)
        {
            if (userBaseNames.contains(QFileInfo(file).completeBaseName().toCaseFolded()))
                continue;
            files.push_back(file);
        }
    }

    for (const QString &file : userFiles)
        if (!files.contains(file))
            files.push_back(file);

    return files;
}

bool StructureDefinitionManager::parseFiles(const QStringList &files,
                                            StrataLibrary *library,
                                            QList<FailedStructureFile> *failures) const
{
    bool allOk = true;
    for (const QString &file : files)
    {
        if (fileAlreadyLoadedAsInclude(library, file))
            continue;

        Parser parser(library);
        parser.SetErrorStream(nullptr);
        const QStringList includeDirs = QStringList{ userStrataDir() } + builtinStructDirs();
        for (const QString &includeDir : includeDirs)
        {
            const QByteArray nativeIncludeDir = QDir::toNativeSeparators(includeDir).toLocal8Bit();
            parser.AddIncludePath(nativeIncludeDir.constData());
        }
        const QByteArray nativePath = QDir::toNativeSeparators(file).toLocal8Bit();
        if (!parser.Ooof(nativePath.constData()))
        {
            allOk = false;
            if (failures)
            {
                FailedStructureFile failure;
                failure.filePath = file;
                failure.fileName = QFileInfo(file).fileName();
                failure.message = QString::fromLocal8Bit(parser.LastErrStr()).trimmed();
                failures->push_back(failure);
            }
        }
    }

    return allOk;
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

    const QString userDir = QFileInfo(userStrataDir()).absoluteFilePath();
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

void StructureDefinitionManager::suppressNextChangeNotification()
{
    m_suppressNextChange = true;
}

void StructureDefinitionManager::scheduleChangeNotification()
{
    if (m_suppressNextChange)
    {
        m_suppressNextChange = false;
        return;
    }
    m_reloadTimer.start();
}
