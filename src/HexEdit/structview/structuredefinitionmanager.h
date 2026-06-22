#ifndef STRUCTVIEW_STRUCTUREDEFINITIONMANAGER_H
#define STRUCTVIEW_STRUCTUREDEFINITIONMANAGER_H

#include "causeway/parser.h"

#include <QByteArray>
#include <QObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <memory>

class QFileSystemWatcher;
struct TypeDecl;

struct StructureMagicSignature
{
    uint64_t   offset = 0;
    QByteArray bytes;
};

struct ExportedStructureType
{
    TypeDecl *typeDecl = nullptr;
    QString  filePath;
    QString  fileName;
    QString  description;
    QStringList assocExtensions;
    QList<StructureMagicSignature> magicSignatures;
};

class StructureDefinitionManager : public QObject
{
    Q_OBJECT
public:
    explicit StructureDefinitionManager(QObject *parent = nullptr);

    StrataLibrary *library() const;
    QStringList definitionFiles() const;
    QList<ExportedStructureType> exportedTypes() const;
    QString lastError() const;
    QString loadLog() const;
    bool isLoaded() const;

    QString userStructsDir() const;
    QStringList builtinStructDirs() const;

    void setBuiltinStructDirsForTests(const QStringList &dirs);
    void setUserStructsDirForTests(const QString &dir);

    void suppressNextChangeNotification();

public slots:
    bool reload();
    void ensureLoaded();

signals:
    void definitionsReloaded();
    void reloadFailed(const QString &message);
    void definitionFilesChanged();

private:
    QStringList discoverDefinitionFiles() const;
    bool parseFiles(const QStringList &files,
                    StrataLibrary *library,
                    QString *errorSummary,
                    QString *errorDiagnostic) const;
    void updateWatchedFiles(const QStringList &files);
    void scheduleChangeNotification();

    std::unique_ptr<StrataLibrary> m_library;
    QFileSystemWatcher          *m_watcher = nullptr;
    QTimer                       m_reloadTimer;
    QStringList                  m_definitionFiles;
    QStringList                  m_builtinDirsOverride;
    QString                      m_userDirOverride;
    QString                      m_lastError;
    QStringList                  m_loadLog;
    bool                         m_loaded = false;
    bool                         m_suppressNextChange = false;
};

#endif // STRUCTVIEW_STRUCTUREDEFINITIONMANAGER_H
