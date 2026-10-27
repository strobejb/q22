#include "filemap/filemapengine.h"

#include "HexView/hexview.h"
#include "HexView/sequencedevice.h"
#include "structview/structuredefinitionmanager.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QPointer>
#include <QThread>

#include <limits>

namespace filemap
{

FileMapEngine::FileMapEngine(QObject *parent)
    : QObject(parent)
{
}

FileMapEngine::~FileMapEngine()
{
    if (m_cancelFlag)
        m_cancelFlag->store(true);
}

void FileMapEngine::build(HexView *hv)
{
    if (m_cancelFlag)
        m_cancelFlag->store(true); // the prior run's result is discarded below
    m_building = false;

    if (!hv)
        return;

    auto cancelFlag = std::make_shared<std::atomic_bool>(false);
    m_cancelFlag = cancelFlag;

    const uint64_t fileSize = static_cast<uint64_t>(hv->size());
    const QString fileName = QFileInfo(hv->filePath()).fileName();
    QPointer<FileMapEngine> guard(this);

    auto inputDeviceOwner = hv->createReadOnlyDeviceSnapshot();
    if (!inputDeviceOwner || fileSize == 0)
    {
        QMetaObject::invokeMethod(this, [guard, cancelFlag, fileSize]() {
            if (guard && !cancelFlag->load())
            {
                FileMap empty;
                empty.fileSize = fileSize;
                emit guard->finished(empty);
            }
        }, Qt::QueuedConnection);
        return;
    }
    auto *inputDevice = inputDeviceOwner.release();
    m_building = true;

    QThread *thread = QThread::create([guard, cancelFlag, inputDevice, fileSize, fileName]() {
        std::unique_ptr<SequenceDevice> inputOwner(inputDevice);
        QIODevice &input = *inputOwner;

        const ByteReader reader = [&input](uint64_t offset, uint8_t *buf, size_t len) -> size_t {
            if (offset > static_cast<uint64_t>(std::numeric_limits<qint64>::max())
                || len > static_cast<size_t>(std::numeric_limits<qint64>::max()))
                return 0;
            if (!input.seek(static_cast<qint64>(offset)))
                return 0;
            const qint64 got = input.read(reinterpret_cast<char *>(buf), static_cast<qint64>(len));
            return got > 0 ? static_cast<size_t>(got) : 0;
        };

        BuildOptions options;
        options.cancelFlag = cancelFlag;
        StructureDefinitionManager definitions;
        FileMap map = buildFileMap(definitions, fileName, fileSize, reader, options);
        map.fileSize = fileSize;

        if (cancelFlag->load())
            return;
        QMetaObject::invokeMethod(qApp, [guard, cancelFlag, map = std::move(map)]() {
            if (!guard || cancelFlag->load())
                return;
            guard->m_building = false;
            emit guard->finished(map);
        }, Qt::QueuedConnection);
    });

    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start(QThread::LowPriority);
}

} // namespace filemap
