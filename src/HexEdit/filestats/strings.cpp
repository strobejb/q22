#include "filestats/sidepanel.h"

#include "HexView/hexview.h"
#include "filestats/widgets.h"
#include "settings/settingscard.h"

#include <QApplication>
#include <QAction>
#include <QBrush>
#include <QElapsedTimer>
#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLocale>
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

static constexpr int kInitialStringResultBatchLimit = 1000;
static constexpr int kMaxStringResultBatchLimit = 100000;
static constexpr qint64 kMinimumStringScanMs = 10000;
static constexpr int kMaxBufferedStringLength = 4096;

enum class StringScanMode {
    PrintableAscii = 0,
    Alphanumeric,
    AsciiText,
    CIdentifiers,
};

StringScanMode stringScanModeFromIndex(int index)
{
    if (index < 0 || index > static_cast<int>(StringScanMode::CIdentifiers))
        return StringScanMode::Alphanumeric;
    return static_cast<StringScanMode>(index);
}

bool isAsciiStringByte(unsigned char ch)
{
    return ch >= 0x20 && ch <= 0x7E;
}

bool isAlphanumericByte(unsigned char ch)
{
    return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

bool isAsciiWhitespaceByte(unsigned char ch)
{
    return ch == ' ' || ch == '\t';
}

bool isAlphaOrUnderscoreByte(unsigned char ch)
{
    return ch == '_' || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

bool isIdentifierByte(unsigned char ch)
{
    return isAlphaOrUnderscoreByte(ch) || (ch >= '0' && ch <= '9');
}

bool isAsciiTextByte(unsigned char ch, bool includeWhitespace)
{
    if (includeWhitespace)
        return isAsciiStringByte(ch) || isAsciiWhitespaceByte(ch);
    return ch >= 0x21 && ch <= 0x7E;
}

bool acceptsByte(StringScanMode mode, unsigned char ch, bool includeWhitespace)
{
    switch (mode) {
    case StringScanMode::Alphanumeric:
        return isAlphanumericByte(ch) || (includeWhitespace && isAsciiWhitespaceByte(ch));
    case StringScanMode::AsciiText:
        return isAsciiTextByte(ch, includeWhitespace);
    case StringScanMode::CIdentifiers:
        return isIdentifierByte(ch);
    case StringScanMode::PrintableAscii:
    default:
        return isAsciiStringByte(ch);
    }
}

bool acceptsFirstByte(StringScanMode mode, unsigned char ch, bool includeWhitespace)
{
    if (mode == StringScanMode::CIdentifiers)
        return isAlphaOrUnderscoreByte(ch);
    return acceptsByte(mode, ch, includeWhitespace);
}

struct StringScanState {
    QVector<QVariantMap> results;
    QByteArray run;
    qulonglong runStart = 0;
    qulonglong runLength = 0;
    qulonglong offset = 0;
    int resultCount = 0;
    int resultLimit = kInitialStringResultBatchLimit;
    bool scanAll = false;
    bool capped = false;
    qulonglong nextOffset = 0;
    QElapsedTimer elapsed;
};

QString displayString(QByteArrayView bytes)
{
    QString text = QString::fromLatin1(bytes.data(), bytes.size());
    text.replace(QLatin1Char('\t'), QLatin1Char(' '));

    qsizetype firstText = 0;
    while (firstText < text.size() && text.at(firstText).isSpace())
        ++firstText;
    if (firstText > 0 && firstText < text.size())
        text.remove(0, firstText);

    return text;
}

void flushAsciiRun(StringScanState &state, int minLength, qulonglong resumeOffset, bool terminated)
{
    const bool allowRun = state.runLength >= static_cast<qulonglong>(minLength) && terminated;
    if (allowRun && state.resultCount < state.resultLimit) {
        QVariantMap row;
        row.insert(QStringLiteral("offset"), state.runStart);
        row.insert(QStringLiteral("length"), state.runLength);
        row.insert(QStringLiteral("text"), displayString(QByteArrayView(state.run.constData(), state.run.size())));
        state.results.append(row);
        ++state.resultCount;
        if (state.resultCount >= state.resultLimit) {
            if (state.scanAll)
                return;
            if (state.elapsed.isValid()
                    && state.elapsed.elapsed() < kMinimumStringScanMs
                    && state.resultLimit < kMaxStringResultBatchLimit) {
                state.resultLimit = qMin(kMaxStringResultBatchLimit, state.resultLimit * 10);
                return;
            }
            state.capped = true;
            state.nextOffset = resumeOffset;
        }
    }
    state.run.clear();
    state.runLength = 0;
}

void scanAsciiChunk(StringScanState &state, const QByteArray &chunk, int minLength,
                    StringScanMode mode, bool includeWhitespace)
{
    for (char byte : chunk) {
        if (state.capped)
            return;
        const unsigned char ch = static_cast<unsigned char>(byte);
        const bool accepted = state.runLength == 0 ? acceptsFirstByte(mode, ch, includeWhitespace)
                                                   : acceptsByte(mode, ch, includeWhitespace);
        if (accepted) {
            if (state.runLength == 0)
                state.runStart = state.offset;
            if (state.run.size() < kMaxBufferedStringLength)
                state.run.append(byte);
            ++state.runLength;
        } else {
            flushAsciiRun(state, minLength, state.offset + 1,
                          mode != StringScanMode::CIdentifiers || ch == '\0');
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
    if (m_stringsRescanRequired) {
        if (m_stringsOperation)
            m_stringsOperation->showRescan(m_stringsRescanMessage.isEmpty()
                                           ? tr("File contents changed")
                                           : m_stringsRescanMessage);
        return;
    }
    if (m_stringsAutoStartConsumed)
        return;
    if (!shouldAutoStartOperations()) {
        if (m_stringsOperation && !m_stringsOperation->hasOperation())
            m_stringsOperation->showStart(tr("Begin scan"));
        return;
    }
    m_stringsStarted = true;
    startStringScan();
}

void FilePropertiesPanel::startStringScan(qulonglong startOffset, bool append, bool scanAll)
{
    if (!m_hexView || !m_minStringLength) {
        m_stringsStarted = false;
        return;
    }

    m_stringsAutoStartConsumed = true;
    const int generation = ++m_stringGeneration;
    const int minLength = m_minStringLength->value();
    const StringScanMode mode = stringScanModeFromIndex(m_stringEncoding ? m_stringEncoding->currentIndex() : 0);
    const bool includeWhitespace = !m_includeWhitespaceAction || m_includeWhitespaceAction->isChecked();
    const QString path = m_hexView->filePath();
    m_stringsRescanRequired = false;
    m_stringsRescanMessage.clear();
    m_stringMoreAvailable = false;
    m_stringProgress = 0;
    if (!append)
        m_stringNextOffset = 0;
    if (m_stringCancel)
        m_stringCancel->store(true);
    auto cancelFlag = std::make_shared<std::atomic_bool>(false);
    m_stringCancel = cancelFlag;
    if (m_stringsOperation)
        m_stringsOperation->showProgress();
    const qint64 fileSize = QFileInfo(path).size();
    const int initialProgress = fileSize > 0
                                    ? static_cast<int>((qMin<qint64>(static_cast<qint64>(startOffset), fileSize) * 1000) / fileSize)
                                    : 0;
    m_stringProgress = qBound(0, initialProgress, 1000);
    setStringsProgressTitle(m_stringProgress);
    if (m_stringsStatusRow)
        m_stringsStatusRow->hide();
    if (m_stringsList && !append)
        m_stringsList->clear();

    QPointer<FilePropertiesPanel> guard(this);
    auto *thread = QThread::create([guard, generation, minLength, mode, includeWhitespace, path, startOffset, scanAll, cancelFlag]() {
        QVector<QVariantMap> results;
        bool capped = false;
        qulonglong nextOffset = 0;
        int finalProgress = 1000;
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            StringScanState state;
            const qint64 total = file.size();
            const qint64 seekOffset = qMin<qint64>(static_cast<qint64>(startOffset), total);
            file.seek(seekOffset);
            state.offset = static_cast<qulonglong>(seekOffset);
            state.scanAll = scanAll;
            state.elapsed.start();
            qint64 scanned = seekOffset;
            int lastProgress = -1;
            static constexpr qint64 kChunkSize = 1024 * 1024;

            while (!cancelFlag->load() && !file.atEnd() && !state.capped) {
                const QByteArray chunk = file.read(kChunkSize);
                if (chunk.isEmpty())
                    break;
                scanAsciiChunk(state, chunk, minLength, mode, includeWhitespace);
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
                flushAsciiRun(state, minLength, state.offset, mode != StringScanMode::CIdentifiers);
                if (!state.results.isEmpty())
                    results = std::move(state.results);
                capped = state.capped && state.nextOffset < static_cast<qulonglong>(total);
                nextOffset = state.nextOffset;
                finalProgress = total > 0
                                    ? static_cast<int>((qMin<qint64>(scanned, total) * 1000) / total)
                                    : 1000;
            }
        }
        if (cancelFlag->load())
            return;
        QMetaObject::invokeMethod(qApp, [guard, generation, results, capped, nextOffset, finalProgress]() {
            if (guard) {
                if (!results.isEmpty())
                    guard->appendStringResults(generation, results);
                guard->finishStringScan(generation, {}, capped, nextOffset, finalProgress);
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
    const int progress = qBound(0, value, 1000);
    m_stringProgress = progress;
    m_stringsOperation->progressBar()->setValue(progress);
    setStringsProgressTitle(progress);
}

void FilePropertiesPanel::cancelStringScan()
{
    ++m_stringGeneration;
    m_stringsStarted = false;
    m_stringMoreAvailable = false;
    if (m_stringCancel)
        m_stringCancel->store(true);
    if (m_stringsStatusRow)
        m_stringsStatusRow->hide();
    if (m_stringsOperation)
        m_stringsOperation->showRetry(tr("Operation cancelled"));
    resetStringsTitle();
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

void FilePropertiesPanel::finishStringScan(int generation, const QVector<QVariantMap> &results,
                                           bool capped, qulonglong nextOffset, int progress)
{
    if (generation != m_stringGeneration)
        return;

    appendStringResults(generation, results);
    m_stringsStarted = false;
    m_stringProgress = qBound(0, progress, 1000);
    m_stringMoreAvailable = capped;
    m_stringNextOffset = nextOffset;
    if (m_stringsStatusRow && m_stringsStatusLabel && m_stringsProgressLabel) {
        const int count = m_stringsList ? m_stringsList->topLevelItemCount() : 0;
        if (capped) {
            m_stringsStatusLabel->setText(tr("Showing first %1 results")
                                              .arg(QLocale().toString(count)));
            m_stringsProgressLabel->setText(tr("(%1% complete)").arg(m_stringProgress / 10));
            m_stringsProgressLabel->show();
            if (m_stringsNextButton)
                m_stringsNextButton->setVisible(true);
            if (m_stringsAllButton)
                m_stringsAllButton->setVisible(true);
            if (m_stringsExportButton)
                m_stringsExportButton->hide();
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
        }
        m_stringsStatusRow->show();
    }
    if (m_stringsOperation)
        m_stringsOperation->clear();
    resetStringsTitle();
}
