#ifndef FILEMAP_FILEMAPENGINE_H
#define FILEMAP_FILEMAPENGINE_H

#include "filemap/filemapbuilder.h"

#include <QObject>

#include <atomic>
#include <memory>

class HexView;

namespace filemap
{

// Builds a FileMap for HexView's current logical document on a background
// thread, reading from a read-only snapshot so the live edit buffer is never
// touched off the GUI thread. Calling build() again cancels the prior run and
// discards its result; build(nullptr) just cancels.
class FileMapEngine : public QObject
{
    Q_OBJECT
public:
    explicit FileMapEngine(QObject *parent = nullptr);
    ~FileMapEngine() override;

    void build(HexView *hv);
    bool isBuilding() const { return m_building; }

signals:
    void finished(filemap::FileMap map);

private:
    std::shared_ptr<std::atomic_bool> m_cancelFlag;
    bool m_building = false;
};

} // namespace filemap

#endif // FILEMAP_FILEMAPENGINE_H
