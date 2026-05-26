#include "filestats/sidepanel.h"

#include "HexView/hexview.h"
#include "filestats/widgets.h"
#include "settings/settingscard.h"

#include <QApplication>
#include <QBrush>
#include <QFile>
#include <QLabel>
#include <QMetaObject>
#include <QPointer>
#include <QProgressBar>
#include <QThread>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVariantMap>

#include <atomic>
#include <memory>
#include <utility>

using namespace filestats;

namespace {

bool isAsciiStringByte(unsigned char ch)
{
    return ch >= 0x20 && ch <= 0x7E;
}

struct StringScanState {
    QVector<QVariantMap> results;
    QByteArray run;
    qulonglong runStart = 0;
    qulonglong runLength = 0;
    qulonglong offset = 0;
    int resultCount = 0;
};

void flushAsciiRun(StringScanState &state, int minLength)
{
    static constexpr int kMaxStringResults = 10000;
    if (state.runLength >= static_cast<qulonglong>(minLength) && state.resultCount < kMaxStringResults) {
        QVariantMap row;
        row.insert(QStringLiteral("offset"), state.runStart);
        row.insert(QStringLiteral("length"), state.runLength);
        row.insert(QStringLiteral("text"), QString::fromLatin1(state.run.constData(), state.run.size()));
        state.results.append(row);
        ++state.resultCount;
    }
    state.run.clear();
    state.runLength = 0;
}

void scanAsciiChunk(StringScanState &state, const QByteArray &chunk, int minLength)
{
    static constexpr int kMaxBufferedStringLength = 4096;
    for (char byte : chunk) {
        if (isAsciiStringByte(static_cast<unsigned char>(byte))) {
            if (state.runLength == 0)
                state.runStart = state.offset;
            if (state.run.size() < kMaxBufferedStringLength)
                state.run.append(byte);
            ++state.runLength;
        } else {
            flushAsciiRun(state, minLength);
        }
        ++state.offset;
    }
}

} // namespace

void FilePropertiesPanel::maybeStartStringScan()
{
    if (!m_panelFullyOpened)
        return;
    if (m_stringsSectionCollapsed)
        return;
    if (m_stringsStarted)
        return;
    m_stringsStarted = true;
    startStringScan();
}

void FilePropertiesPanel::startStringScan()
{
    if (!m_hexView || !m_minStringLength)
        return;

    const int generation = ++m_stringGeneration;
    const int minLength = m_minStringLength->value();
    const QString path = m_hexView->filePath();
    if (m_stringCancel)
        m_stringCancel->store(true);
    auto cancelFlag = std::make_shared<std::atomic_bool>(false);
    m_stringCancel = cancelFlag;
    if (m_stringsOperation)
        m_stringsOperation->showProgress();
    if (m_stringsList)
        m_stringsList->clear();

    QPointer<FilePropertiesPanel> guard(this);
    auto *thread = QThread::create([guard, generation, minLength, path, cancelFlag]() {
        QVector<QVariantMap> results;
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            StringScanState state;
            const qint64 total = file.size();
            qint64 scanned = 0;
            int lastProgress = -1;
            static constexpr qint64 kChunkSize = 1024 * 1024;

            while (!cancelFlag->load() && !file.atEnd()) {
                const QByteArray chunk = file.read(kChunkSize);
                if (chunk.isEmpty())
                    break;
                scanAsciiChunk(state, chunk, minLength);
                if (!state.results.isEmpty()) {
                    const QVector<QVariantMap> batch = std::move(state.results);
                    state.results.clear();
                    QMetaObject::invokeMethod(qApp, [guard, generation, batch]() {
                        if (guard)
                            guard->appendStringResults(generation, batch);
                    }, Qt::QueuedConnection);
                }
                scanned += chunk.size();
                const int progress = total > 0
                                         ? static_cast<int>((scanned * 1000) / total)
                                         : 1000;
                if (progress != lastProgress) {
                    lastProgress = progress;
                    QMetaObject::invokeMethod(qApp, [guard, generation, progress]() {
                        if (guard)
                            guard->updateStringProgress(generation, progress);
                    }, Qt::QueuedConnection);
                }
            }
            if (!cancelFlag->load()) {
                flushAsciiRun(state, minLength);
                if (!state.results.isEmpty())
                    results = std::move(state.results);
            }
        }
        if (cancelFlag->load())
            return;
        QMetaObject::invokeMethod(qApp, [guard, generation, results]() {
            if (guard) {
                if (!results.isEmpty())
                    guard->appendStringResults(generation, results);
                guard->applyStringResults(generation, {});
            }
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void FilePropertiesPanel::updateStringProgress(int generation, int value)
{
    if (generation != m_stringGeneration || !m_stringsOperation)
        return;
    m_stringsOperation->progressBar()->setValue(qBound(0, value, 1000));
}

void FilePropertiesPanel::cancelStringScan()
{
    ++m_stringGeneration;
    m_stringsStarted = false;
    if (m_stringCancel)
        m_stringCancel->store(true);
    if (m_stringsOperation)
        m_stringsOperation->showRetry(tr("Operation cancelled"));
}

void FilePropertiesPanel::resizeStringsList(int dy)
{
    if (!m_stringsList)
        return;
    QWidget *frame = m_stringsList->parentWidget();
    if (!frame)
        return;
    const int maxHeight = qMax(kStringsListMinHeight, height() - kSectionHeaderHeight * 2);
    const int newHeight = qBound(kStringsListMinHeight, frame->height() + dy, maxHeight);
    if (newHeight == frame->height())
        return;
    const int delta = newHeight - frame->height();
    frame->setFixedHeight(newHeight);
    if (m_stringsResizeSlack) {
        if (delta < 0)
            m_stringsResizeSlackHeight += -delta;
        else
            m_stringsResizeSlackHeight = qMax(0, m_stringsResizeSlackHeight - delta);
        m_stringsResizeSlack->changeSize(0, m_stringsResizeSlackHeight,
                                         QSizePolicy::Minimum, QSizePolicy::Fixed);
    }
    if (m_content && m_content->layout())
        m_content->layout()->invalidate();
    updateStickyHeader();
}

void FilePropertiesPanel::appendStringResults(int generation, const QVector<QVariantMap> &results)
{
    if (generation != m_stringGeneration)
        return;

    if (m_stringsList && !results.isEmpty()) {
        m_stringsList->setUpdatesEnabled(false);
        const QColor offsetColor = subduedTextColor(palette());
        for (const QVariantMap &row : results) {
            const qulonglong offset = row.value(QStringLiteral("offset")).toULongLong();
            const qulonglong length = row.value(QStringLiteral("length")).toULongLong();
            auto *item = new QTreeWidgetItem(m_stringsList);
            item->setText(0, row.value(QStringLiteral("text")).toString());
            item->setText(1, QStringLiteral("%1").arg(offset, 8, 16, QLatin1Char('0')).toUpper());
            item->setForeground(1, QBrush(offsetColor));
            item->setData(0, Qt::UserRole, offset);
            item->setData(0, Qt::UserRole + 1, length);
        }
        m_stringsList->setUpdatesEnabled(true);
    }
}

void FilePropertiesPanel::applyStringResults(int generation, const QVector<QVariantMap> &results)
{
    if (generation != m_stringGeneration)
        return;

    appendStringResults(generation, results);
    if (m_stringsOperation)
        m_stringsOperation->clear();
}
