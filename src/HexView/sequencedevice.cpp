#include "sequencedevice.h"

#include "sequence.h"

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

struct SequenceDevice::Private
{
    struct Span
    {
        qint64 logicalOffset = 0;
        qint64 length = 0;
        QByteArray memory;
        QFile *file = nullptr;
        qint64 sourceOffset = 0;
    };

    std::vector<Span> spans;
    qint64 size = 0;
    bool valid = false;
};

namespace
{

bool fitsQint64(size_w value)
{
    return value <= static_cast<size_w>(std::numeric_limits<qint64>::max());
}

} // namespace

SequenceDevice::SequenceDevice(const sequence &source, QObject *parent)
    : QIODevice(parent), d(std::make_unique<Private>())
{
    const sequence *resolved = &source;
    size_w baseOffset = 0;
    size_w logicalLength = source.size();

    for (int depth = 0; depth < 16; ++depth)
    {
        const sequence *next = nullptr;
        size_w nextBase = 0;
        size_w nextLength = 0;
        resolved->resolveDeviceSource(next, nextBase, nextLength);

        if (!next)
        {
            setErrorString(QStringLiteral("Unable to resolve sequence data source"));
            return;
        }

        logicalLength = std::min(logicalLength, nextLength);
        if (next == resolved)
            break;

        if (baseOffset > MAX_SEQUENCE_LENGTH - nextBase)
        {
            setErrorString(QStringLiteral("Sequence view offset overflow"));
            return;
        }

        baseOffset += nextBase;
        resolved = next;

        if (depth == 15)
        {
            setErrorString(QStringLiteral("Sequence view nesting is too deep"));
            return;
        }
    }

    if (baseOffset > resolved->size() || logicalLength > resolved->size() - baseOffset)
    {
        setErrorString(QStringLiteral("Sequence view range is invalid"));
        return;
    }

    if (!fitsQint64(logicalLength))
    {
        setErrorString(QStringLiteral("Sequence is too large for QIODevice"));
        return;
    }

    d->size = static_cast<qint64>(logicalLength);
    if (logicalLength == 0)
    {
        d->valid = true;
        return;
    }

    size_w spanIndex = 0;
    sequence::span *sptr = resolved->spanfromindex(baseOffset, &spanIndex);
    if (!sptr || sptr == resolved->tail)
    {
        setErrorString(QStringLiteral("Unable to locate sequence data"));
        return;
    }

    QHash<const sequence::buffer_control *, QFile *> fileCopies;
    size_w sourceIndex = baseOffset;
    size_w remaining = logicalLength;
    qint64 logicalOffset = 0;

    while (remaining > 0 && sptr && sptr != resolved->tail)
    {
        const size_w withinSpan = sourceIndex - spanIndex;
        const size_w take = std::min(remaining, sptr->length - withinSpan);
        const size_w bufferOffset = sptr->offset + withinSpan;

        if (!fitsQint64(take) || !fitsQint64(bufferOffset))
        {
            setErrorString(QStringLiteral("Sequence span is too large for QIODevice"));
            return;
        }

        if (sptr->buffer >= resolved->buffer_list.size())
        {
            setErrorString(QStringLiteral("Sequence span references an invalid buffer"));
            return;
        }

        sequence::buffer_control *buffer = resolved->buffer_list[sptr->buffer];
        if (!buffer)
        {
            setErrorString(QStringLiteral("Sequence span has no backing buffer"));
            return;
        }

        Private::Span snapshotSpan;
        snapshotSpan.logicalOffset = logicalOffset;
        snapshotSpan.length = static_cast<qint64>(take);

        if (buffer->hFile)
        {
            QFile *snapshotFile = fileCopies.value(buffer, nullptr);
            if (!snapshotFile)
            {
                auto *sourceFile = static_cast<QFile *>(buffer->hFile);
                snapshotFile = new QFile(sourceFile->fileName(), this);
                if (!snapshotFile->open(QIODevice::ReadOnly))
                {
                    const QString fileName = sourceFile->fileName();
                    delete snapshotFile;
                    setErrorString(QStringLiteral("Unable to snapshot file-backed sequence data: %1").arg(fileName));
                    return;
                }
                fileCopies.insert(buffer, snapshotFile);
            }

            snapshotSpan.file = snapshotFile;
            snapshotSpan.sourceOffset = static_cast<qint64>(bufferOffset);
        }
        else
        {
            if (take > static_cast<size_w>(std::numeric_limits<qsizetype>::max()))
            {
                setErrorString(QStringLiteral("Memory-backed sequence span is too large"));
                return;
            }

            snapshotSpan.memory.resize(static_cast<qsizetype>(take));
            seqchar *src = buffer->getptr(bufferOffset, take);
            if (!src)
            {
                setErrorString(QStringLiteral("Unable to snapshot memory-backed sequence data"));
                return;
            }
            std::memcpy(snapshotSpan.memory.data(), src, static_cast<size_t>(take));
        }

        d->spans.push_back(std::move(snapshotSpan));
        logicalOffset += static_cast<qint64>(take);
        sourceIndex += take;
        remaining -= take;
        spanIndex += sptr->length;
        sptr = sptr->next;
    }

    if (remaining != 0 || logicalOffset != d->size)
    {
        setErrorString(QStringLiteral("Sequence snapshot is incomplete"));
        return;
    }

    d->valid = true;
}

SequenceDevice::~SequenceDevice() = default;

bool SequenceDevice::isValid() const
{
    return d && d->valid;
}

qint64 SequenceDevice::size() const
{
    return d ? d->size : 0;
}

bool SequenceDevice::seek(qint64 newPos)
{
    if (!d || !d->valid || newPos < 0 || newPos > d->size)
        return false;
    return QIODevice::seek(newPos);
}

qint64 SequenceDevice::readData(char *data, qint64 maxSize)
{
    if (!d || !d->valid)
        return -1;
    if (!data || maxSize <= 0)
        return 0;

    qint64 offset = pos();
    if (offset >= d->size)
        return 0;

    const qint64 requested = std::min(maxSize, d->size - offset);
    qint64 totalRead = 0;

    while (totalRead < requested)
    {
        auto it = std::upper_bound(d->spans.begin(), d->spans.end(), offset,
                                   [](qint64 value, const Private::Span &span)
                                   {
                                       return value < span.logicalOffset;
                                   });

        if (it == d->spans.begin())
            return totalRead > 0 ? totalRead : -1;
        --it;

        const qint64 withinSpan = offset - it->logicalOffset;
        if (withinSpan < 0 || withinSpan >= it->length)
            return totalRead > 0 ? totalRead : -1;

        const qint64 count = std::min(requested - totalRead, it->length - withinSpan);
        if (it->file)
        {
            if (!it->file->seek(it->sourceOffset + withinSpan))
            {
                setErrorString(QStringLiteral("Unable to seek sequence backing file"));
                return totalRead > 0 ? totalRead : -1;
            }

            const qint64 got = it->file->read(data + totalRead, count);
            if (got <= 0)
            {
                setErrorString(it->file->errorString());
                return totalRead > 0 ? totalRead : -1;
            }

            totalRead += got;
            offset += got;
            if (got < count)
                break;
        }
        else
        {
            std::memcpy(data + totalRead,
                        it->memory.constData() + static_cast<qsizetype>(withinSpan),
                        static_cast<size_t>(count));
            totalRead += count;
            offset += count;
        }
    }

    return totalRead;
}

qint64 SequenceDevice::writeData(const char *, qint64)
{
    setErrorString(QStringLiteral("SequenceDevice is read-only"));
    return -1;
}
