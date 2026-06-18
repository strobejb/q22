#ifndef STRUCTVIEW_STRUCTUREDEFINITIONMANAGER_H
#define STRUCTVIEW_STRUCTUREDEFINITIONMANAGER_H

#include "TypeLib/parser.h"

#include <QObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <memory>

class QFileSystemWatcher;
struct TypeDecl;

struct ExportedStructureType
{
    TypeDecl *typeDecl = nullptr;
    QString  filePath;
    QString  fileName;
    QStringList assocExtensions;
};

class StructureDefinitionManager : public QObject
{
    Q_OBJECT
public:
    explicit StructureDefinitionManager(QObject *parent = nullptr);

    TypeLibrary *library() const;
    QStringList definitionFiles() const;
    QList<ExportedStructureType> exportedTypes() const;
    QString lastError() const;
    QString loadLog() const;
    bool isLoaded() const;

    QString userStructsDir() const;
    QStringList builtinStructDirs() const;

    void setBuiltinStructDirsForTests(const QStringList &dirs);
    void setUserStructsDirForTests(const QString &dir);

public slots:
    bool reload();
    void ensureLoaded();

signals:
    void definitionsReloaded();
    void reloadFailed(const QString &message);

private:
    QStringList discoverDefinitionFiles() const;
    bool parseFiles(const QStringList &files, TypeLibrary *library, QString *errorMessage) const;
    void updateWatchedFiles(const QStringList &files);
    void scheduleReload();

    std::unique_ptr<TypeLibrary> m_library;
    QFileSystemWatcher          *m_watcher = nullptr;
    QTimer                       m_reloadTimer;
    QStringList                  m_definitionFiles;
    QStringList                  m_builtinDirsOverride;
    QString                      m_userDirOverride;
    QString                      m_lastError;
    QStringList                  m_loadLog;
    bool                         m_loaded = false;
};

#endif // STRUCTVIEW_STRUCTUREDEFINITIONMANAGER_H
