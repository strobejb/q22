#include "filestats/sidepanel.h"
#include "filestats/stringscan.h"

#include "HexView/hexview.h"
#include "filestats/widgets.h"
#include "settings/settingscard.h"

#include <QApplication>
#include <QAction>
#include <QBrush>
#include <QElapsedTimer>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMetaObject>
#include <QPointer>
#include <QProgressBar>
#include <QThread>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTemporaryFile>
#include <QTextStream>
#include <QVariantMap>

#include <atomic>
#include <memory>
#include <utility>

using namespace filestats;
using namespace stringscan;

namespace {

class StringResultItem : public QTreeWidgetItem
{
public:
    using QTreeWidgetItem::QTreeWidgetItem;

    bool operator<(const QTreeWidgetItem &other) const override
    {
        const bool thisTruncation = data(0, kStringFooterRole).toBool();
        const bool otherTruncation = other.data(0, kStringFooterRole).toBool();
        if (thisTruncation != otherTruncation) {
            const bool asc = !treeWidget() ||
                treeWidget()->header()->sortIndicatorOrder() == Qt::AscendingOrder;
            return asc ? !thisTruncation : thisTruncation;
        }

        const int column = treeWidget() ? treeWidget()->sortColumn() : 0;
        if (column == 1)
            return data(0, Qt::UserRole).toULongLong()
                   < other.data(0, Qt::UserRole).toULongLong();

        return QString::localeAwareCompare(text(column), other.text(column)) < 0;
    }
};

} // namespace

void FilePropertiesPanel::maybeStartStringScan()
{
    if (!m_panelFullyOpened)
        return;
    if (isSectionCollapsed(SectionId::Strings))
        return;
    if (m_stringsState.started)
        return;
    if (m_stringsState.rescanRequired) {
        if (m_stringsOperation)
            m_stringsOperation->showRescan(m_stringsState.rescanMessage.isEmpty()
                                           ? tr("File contents changed")
                                           : m_stringsState.rescanMessage);
        requestSectionLayoutRefresh(SectionId::Strings);
        return;
    }
    if (m_stringsState.autoStartConsumed)
        return;
    if (!shouldAutoStartOperations()) {
        if (m_stringsOperation && !m_stringsOperation->hasOperation())
            m_stringsOperation->showStart(tr("Begin scan"));
        return;
    }
    m_stringsState.started = true;
    startStringScan();
}

void FilePropertiesPanel::clearStringExportTemp()
{
    if (!m_stringsExportTempPath.isEmpty())
        QFile::remove(m_stringsExportTempPath);
    m_stringsExportTempPath.clear();
    m_stringsExportTempComplete = false;
    m_stringResultCount = 0;
}

QString FilePropertiesPanel::createStringExportTemp()
{
    QTemporaryFile temp(QDir::tempPath() + QStringLiteral("/qexed-strings-XXXXXX.txt"));
    temp.setAutoRemove(false);
    if (!temp.open())
        return {};

    QTextStream out(&temp);
    const bool prefixHexOffset = m_prefixHexOffsetAction && m_prefixHexOffsetAction->isChecked();
    if (m_stringsList) {
        for (int i = 0; i < m_stringsList->topLevelItemCount(); ++i) {
            if (QTreeWidgetItem *item = m_stringsList->topLevelItem(i)) {
                if (item->data(0, kStringFooterRole).toBool())
                    continue;
                const qulonglong offset = item->data(0, Qt::UserRole).toULongLong();
                out << exportStringLine(item->text(0), offset, prefixHexOffset) << '\n';
            }
        }
    }
    const QString path = temp.fileName();
    temp.close();
    return path;
}

void FilePropertiesPanel::startStringScan(qulonglong startOffset, bool append, bool scanAll)
{
    if (!m_hexView || !m_minStringLength) {
        m_stringsState.started = false;
        m_stringsState.pausedByCollapse = false;
        return;
    }

    m_stringsState.autoStartConsumed = true;
    const int generation = ++m_stringsState.generation;
    const int minLength = m_minStringLength->value();
    const StringScanMode mode = stringScanModeFromIndex(m_stringEncoding ? m_stringEncoding->currentIndex() : 0);
    const bool includeWhitespace = !m_includeWhitespaceAction || m_includeWhitespaceAction->isChecked();
    const bool prefixHexOffset = m_prefixHexOffsetAction && m_prefixHexOffsetAction->isChecked();
    const QString path = m_hexView->filePath();
    if (!append)
        clearStringExportTemp();
    QString exportTempPath;
    int visibleBaseCount = m_stringsList ? m_stringsList->topLevelItemCount() : 0;
    qulonglong initialResultCount = append ? m_stringResultCount : 0;
    if (scanAll) {
        if (m_stringsExportTempPath.isEmpty())
            m_stringsExportTempPath = createStringExportTemp();
        exportTempPath = m_stringsExportTempPath;
        m_stringsExportTempComplete = false;
        if (initialResultCount == 0)
            initialResultCount = static_cast<qulonglong>(visibleBaseCount);
    }
    m_stringsState.pausedByCollapse = isSectionCollapsed(SectionId::Strings);
    m_stringsState.rescanRequired = false;
    m_stringsState.rescanMessage.clear();
    m_stringMoreAvailable = false;
    m_stringsState.progress = 0;
    if (!append)
        m_stringNextOffset = 0;
    if (m_stringsState.cancel)
        m_stringsState.cancel->store(true);
    if (m_stringsState.pause)
        m_stringsState.pause->wake();
    auto cancelFlag = std::make_shared<std::atomic_bool>(false);
    m_stringsState.cancel = cancelFlag;
    auto pause = std::make_shared<filestats::OperationPause>();
    pause->setPaused(isSectionCollapsed(SectionId::Strings));
    m_stringsState.pause = pause;
    if (m_stringsOperation)
        m_stringsOperation->showProgress();
    requestSectionLayoutRefresh(SectionId::Strings);
    const qint64 fileSize = QFileInfo(path).size();
    const int initialProgress = fileSize > 0
                                    ? static_cast<int>((qMin<qint64>(static_cast<qint64>(startOffset), fileSize) * 1000) / fileSize)
                                    : 0;
    m_stringsState.progress = qBound(0, initialProgress, 1000);
    setStringsProgressTitle(m_stringsState.progress);
    if (m_stringsStatusRow)
        m_stringsStatusRow->hide();
    if (!append) {
        if (m_stringsListFrame)
            m_stringsListFrame->clearList();
        else if (m_stringsList)
            m_stringsList->clear();
    }
    // Disable auto-sort for the duration of the scan: setSortingEnabled(true) causes Qt
    // to re-sort the entire list after every item insertion (O(n²) total). We disable it
    // once here and re-enable once in sortStringResults when the scan completes.
    if (m_stringsList && m_stringsList->isSortingEnabled()) {
        m_stringsList->setSortingEnabled(false);
        m_stringsList->header()->setSortIndicatorShown(true);
        m_stringsList->header()->setSectionsClickable(true);
    }
    requestSectionLayoutRefresh(SectionId::Strings);

    QPointer<FilePropertiesPanel> guard(this);
    auto *thread = QThread::create([guard, generation, minLength, mode, includeWhitespace,
                                    path, startOffset, scanAll, cancelFlag,
                                    visibleBaseCount, initialResultCount, exportTempPath,
                                    prefixHexOffset, pause]() {
        QVector<QVariantMap> results;
        bool capped = false;
        qulonglong nextOffset = 0;
        int finalProgress = 1000;
        qulonglong totalResults = initialResultCount;
        bool exportTempComplete = false;
        QFile file(path);
        QFile exportFile(exportTempPath);
        QTextStream exportStream(&exportFile);
        QTextStream *exportOut = nullptr;
        if (scanAll && !exportTempPath.isEmpty()
                && exportFile.open(QIODevice::Append | QIODevice::Text)) {
            exportOut = &exportStream;
        }
        if (file.open(QIODevice::ReadOnly)) {
            StringScanState state;
            const qint64 total = file.size();
            const qint64 seekOffset = qMin<qint64>(static_cast<qint64>(startOffset), total);
            file.seek(seekOffset);
            state.offset = static_cast<qulonglong>(seekOffset);
            state.scanAll = scanAll;
            state.visibleBaseCount = visibleBaseCount;
            state.totalResultCount = initialResultCount;
            if (scanAll)
                state.resultLimit = kMaxStringResultBatchLimit;
            state.elapsed.start();
            qint64 scanned = seekOffset;
            int lastProgress = -1;
            static constexpr qint64 kChunkSize = 1024 * 1024;

            while (!cancelFlag->load() && !file.atEnd() && !state.capped) {
                if (pause && !pause->waitIfPaused(cancelFlag))
                    break;
                const QByteArray chunk = file.read(kChunkSize);
                if (chunk.isEmpty())
                    break;
                scanAsciiChunk(state, chunk, minLength, mode, includeWhitespace, exportOut,
                               prefixHexOffset);
                if (state.capped)
                    scanned = qMin<qint64>(static_cast<qint64>(state.nextOffset), total);
                else
                    scanned += chunk.size();
                if (!state.results.isEmpty()) {
                    const QVector<QVariantMap> batch = std::move(state.results);
                    state.results.clear();
                    QMetaObject::invokeMethod(qApp, [guard, generation, batch]() {
                        if (guard)
                            guard->appendStringResults(generation, batch);
                    }, Qt::QueuedConnection);
                }
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
                flushAsciiRun(state, minLength, state.offset,
                              mode != StringScanMode::CIdentifiers, exportOut,
                              prefixHexOffset);
                if (!state.results.isEmpty())
                    results = std::move(state.results);
                capped = state.capped && state.nextOffset < static_cast<qulonglong>(total);
                nextOffset = state.nextOffset;
                totalResults = state.totalResultCount;
                finalProgress = total > 0
                                    ? static_cast<int>((qMin<qint64>(scanned, total) * 1000) / total)
                                    : 1000;
                exportTempComplete = scanAll && exportOut && !capped;
            }
        }
        if (exportOut) {
            exportStream.flush();
            exportFile.close();
        }
        if (cancelFlag->load()) {
            if (!exportTempPath.isEmpty())
                QFile::remove(exportTempPath);
            return;
        }
        QMetaObject::invokeMethod(qApp, [guard, generation, results, capped, nextOffset,
                                         finalProgress, totalResults, exportTempPath,
                                         exportTempComplete]() {
            if (guard) {
                if (!results.isEmpty())
                    guard->appendStringResults(generation, results);
                guard->finishStringScan(generation, {}, capped, nextOffset, finalProgress,
                                        totalResults, exportTempPath, exportTempComplete);
            }
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void FilePropertiesPanel::updateStringProgress(int generation, int value)
{
    if (generation != m_stringsState.generation || !m_stringsOperation)
        return;
    const int progress = qBound(0, value, 1000);
    m_stringsState.progress = progress;
    m_stringsOperation->progressBar()->setValue(progress);
    setStringsProgressTitle(progress);
}

void FilePropertiesPanel::cancelStringScan()
{
    ++m_stringsState.generation;
    m_stringsState.started = false;
    m_stringsState.pausedByCollapse = false;
    m_stringMoreAvailable = false;
    if (m_stringsState.cancel)
        m_stringsState.cancel->store(true);
    if (m_stringsState.pause)
        m_stringsState.pause->wake();
    clearStringExportTemp();
    if (m_stringsStatusRow)
        m_stringsStatusRow->hide();
    if (m_stringsOperation)
        m_stringsOperation->showRetry(tr("Operation cancelled"));
    resetStringsTitle();
    requestSectionLayoutRefresh(SectionId::Strings);
}

void FilePropertiesPanel::resumeStringScan()
{
    if (!m_stringsState.started || !m_stringsState.pause)
        return;

    m_stringsState.pausedByCollapse = false;
    m_stringsState.pause->wake();
    if (m_stringsOperation)
        m_stringsOperation->setProgressActionStop();
    setStringsProgressTitle(m_stringsState.progress);
    requestSectionLayoutRefresh(SectionId::Strings);
}

void FilePropertiesPanel::appendStringResults(int generation, const QVector<QVariantMap> &results)
{
    if (generation != m_stringsState.generation)
        return;

    if (m_stringsList && !results.isEmpty()) {
        m_stringsList->setUpdatesEnabled(false);
        const QColor offsetColor = subduedTextColor(palette());
        QList<QTreeWidgetItem *> items;
        items.reserve(results.size());
        for (const QVariantMap &row : results) {
            const qulonglong offset = row.value(QStringLiteral("offset")).toULongLong();
            const qulonglong length = row.value(QStringLiteral("length")).toULongLong();
            auto *item = new StringResultItem;
            item->setText(0, row.value(QStringLiteral("text")).toString());
            item->setText(1, hexOffsetString(offset));
            item->setForeground(1, QBrush(offsetColor));
            item->setData(0, Qt::UserRole, offset);
            item->setData(0, Qt::UserRole + 1, length);
            items.append(item);
        }
        m_stringsList->addTopLevelItems(items);
        m_stringsList->setUpdatesEnabled(true);
    }
}

void FilePropertiesPanel::sortStringResults(int column, Qt::SortOrder order)
{
    if (!m_stringsList)
        return;

    const bool wasUpdatesEnabled = m_stringsList->updatesEnabled();
    if (wasUpdatesEnabled)
        m_stringsList->setUpdatesEnabled(false);
    // Re-establish setSortingEnabled(true) if batch inserts left it disabled.
    // This restores the sectionClicked→sortByColumn connection for header toggle.
    if (!m_stringsList->isSortingEnabled())
        m_stringsList->setSortingEnabled(true);
    m_stringsList->sortItems(column, order);
    if (m_stringsListFrame)
        m_stringsListFrame->refreshFooterPlacement();
    if (wasUpdatesEnabled)
        m_stringsList->setUpdatesEnabled(true);
}

void FilePropertiesPanel::removeStringTruncationItem()
{
    if (!m_stringsListFrame)
        return;
    m_stringsListFrame->clearFooter();
}

void FilePropertiesPanel::addStringTruncationItem(const QString &message)
{
    if (!m_stringsListFrame)
        return;

    m_stringsListFrame->setFooterText(message);
}

int FilePropertiesPanel::visibleStringResultCount() const
{
    if (!m_stringsList)
        return 0;

    int count = 0;
    for (int i = 0; i < m_stringsList->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_stringsList->topLevelItem(i);
        if (item && !item->data(0, kStringFooterRole).toBool())
            ++count;
    }
    return count;
}

void FilePropertiesPanel::finishStringScan(int generation, const QVector<QVariantMap> &results,
                                           bool capped, qulonglong nextOffset, int progress,
                                           qulonglong totalResults,
                                           const QString &exportTempPath,
                                           bool exportTempComplete)
{
    if (generation != m_stringsState.generation)
        return;

    appendStringResults(generation, results);
    if (m_stringsList) {
        sortStringResults(m_stringsList->header()->sortIndicatorSection(),
                          m_stringsList->header()->sortIndicatorOrder());
    }
    m_stringsState.started = false;
    m_stringsState.pausedByCollapse = false;
    m_stringsState.progress = qBound(0, progress, 1000);
    m_stringMoreAvailable = capped;
    m_stringNextOffset = nextOffset;
    m_stringResultCount = totalResults;
    if (!exportTempPath.isEmpty()) {
        m_stringsExportTempPath = exportTempPath;
        m_stringsExportTempComplete = exportTempComplete && !capped;
    }
    if (m_stringsStatusRow && m_stringsStatusLabel && m_stringsProgressLabel) {
        const int visibleCount = visibleStringResultCount();
        const qulonglong count = qMax(totalResults, static_cast<qulonglong>(visibleCount));
        if (capped) {
            m_stringsStatusLabel->setText(tr("Showing first %1 results")
                                              .arg(QLocale().toString(visibleCount)));
            m_stringsProgressLabel->setText(tr("(%1% complete)").arg(m_stringsState.progress / 10));
            m_stringsProgressLabel->show();
            if (m_stringsNextButton)
                m_stringsNextButton->setVisible(true);
            if (m_stringsAllButton)
                m_stringsAllButton->setVisible(true);
            if (m_stringsExportButton)
                m_stringsExportButton->hide();
            addStringTruncationItem(tr("More results available - use Next or All"));
        } else {
            m_stringsStatusLabel->setText(tr("Found %1 results")
                                              .arg(QLocale().toString(count)));
            m_stringsProgressLabel->clear();
            m_stringsProgressLabel->hide();
            if (m_stringsNextButton)
                m_stringsNextButton->hide();
            if (m_stringsAllButton)
                m_stringsAllButton->hide();
            if (m_stringsExportButton)
                m_stringsExportButton->setVisible(count > 0);
            if (count > static_cast<qulonglong>(visibleCount))
                addStringTruncationItem(tr("List truncated - export to view full list"));
            else
                removeStringTruncationItem();
        }
        m_stringsStatusRow->show();
    }
    if (m_stringsOperation)
        m_stringsOperation->clear();
    resetStringsTitle();
    requestSectionLayoutRefresh(SectionId::Strings);
}
