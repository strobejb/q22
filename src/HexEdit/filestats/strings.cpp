#include "filestats/sidepanel.h"
#include "filestats/stringscan.h"

#include "HexView/hexview.h"
#include "combos/menucombobox.h"
#include "filestats/widgets.h"
#include "settings/settingscard.h"
#include "settings/settings.h"
#include "theme.h"

#include <QApplication>
#include <QAction>
#include <QActionGroup>
#include <QCursor>
#include <QEvent>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMenu>
#include <QPainter>
#include <QPoint>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStyleOptionHeader>
#include <QTimer>
#include <QVBoxLayout>
#include <QBrush>
#include <QElapsedTimer>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QMetaObject>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
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

class StringsHeaderCorner : public QWidget
{
public:
    explicit StringsHeaderCorner(QTreeWidget *tree) : QWidget(tree), m_tree(tree)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        if (m_tree)
        {
            m_tree->installEventFilter(this);
            if (m_tree->header())
                m_tree->header()->installEventFilter(this);
            if (m_tree->verticalScrollBar())
                m_tree->verticalScrollBar()->installEventFilter(this);
        }
        updateGeometryFromTree();
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        Q_UNUSED(object)
        switch (event->type())
        {
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::Hide:
        case QEvent::LayoutRequest:
            updateGeometryFromTree();
            break;
        default:
            break;
        }
        return QWidget::eventFilter(object, event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().base());
    }

private:
    void updateGeometryFromTree()
    {
        if (!m_tree || !m_tree->header())
            return;
        const int width = m_tree->verticalScrollBar()->width();
        if (width <= 0 || m_tree->header()->isHidden() || !m_tree->verticalScrollBar()->isVisible())
        {
            hide();
            return;
        }
        setGeometry(m_tree->width() - width, 0, width, m_tree->header()->height());
        raise();
        show();
    }

    QTreeWidget *m_tree = nullptr;
};

static constexpr int kStringsResultHeaderBottomGap = 3;

class InlineSortHeader : public QHeaderView
{
public:
    explicit InlineSortHeader(Qt::Orientation orientation, QWidget *parent = nullptr)
        : QHeaderView(orientation, parent)
    {
        setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }

protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override
    {
        if (!painter || !rect.isValid())
            return;

        painter->save();
        painter->fillRect(rect, palette().base());
        QFont headerFont = font();
        if (headerFont.pointSizeF() > 0)
            headerFont.setPointSizeF(qMax(1.0, headerFont.pointSizeF() - 1.0));
        else if (headerFont.pixelSize() > 0)
            headerFont.setPixelSize(qMax(1, headerFont.pixelSize() - 1));
        headerFont.setWeight(QFont::DemiBold);
        const QFontMetrics headerMetrics(headerFont);
        painter->setFont(headerFont);

        QStyleOptionHeader opt;
        initStyleOption(&opt);
        initStyleOptionForIndex(&opt, logicalIndex);
        opt.rect              = rect.adjusted(0, 0, 0, -kStringsResultHeaderBottomGap);
        opt.fontMetrics       = QFontMetrics(headerFont);
        const QPoint localPos = mapFromGlobal(QCursor::pos());
        const bool   hovered  = rect.contains(localPos);
        if (hovered)
        {
            opt.state |= QStyle::State_MouseOver;
            opt.palette.setColor(QPalette::Button, palette().button().color());
            opt.palette.setColor(QPalette::Window, palette().button().color());
        }
        else
        {
            opt.palette.setColor(QPalette::Button, palette().base().color());
            opt.palette.setColor(QPalette::Window, palette().base().color());
        }
        const QColor headerTextColor = hovered ? palette().windowText().color() : stringsHeaderTextColor(palette());
        opt.palette.setColor(QPalette::ButtonText, headerTextColor);
        opt.palette.setColor(QPalette::WindowText, headerTextColor);
        const QString text = opt.text;
        opt.text.clear();
        opt.sortIndicator = QStyleOptionHeader::None;
        style()->drawControl(QStyle::CE_Header, &opt, painter, this);

        const int     kTextPadding = stringsHeaderPadding(headerMetrics);
        constexpr int kIconGap     = 4;
        constexpr int kIconSize    = 10;
        const QRect   textRect     = opt.rect.adjusted(kTextPadding, kTextPadding, -kTextPadding, -kTextPadding);
        painter->setPen(headerTextColor);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);

        if (logicalIndex == sortIndicatorSection())
        {
            const int     textWidth = painter->fontMetrics().horizontalAdvance(text);
            const int     x         = qMin(textRect.left() + textWidth + kIconGap, textRect.right() - kIconSize + 1);
            const QRect   iconRect(x, textRect.top() + (textRect.height() - kIconSize) / 2, kIconSize, kIconSize);
            const QString iconName = sortIndicatorOrder() == Qt::AscendingOrder
                                         ? QStringLiteral("ui/go-up-symbolic")
                                         : QStringLiteral("ui/go-down-symbolic");
            recoloredIcon(iconName, headerTextColor, kIconSize).paint(painter, iconRect);
        }
        painter->restore();
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
    // to re-sort the entire list after every item insertion (O(nÂ²) total). We disable it
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
    // This restores the sectionClickedâ†’sortByColumn connection for header toggle.
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

void FilePropertiesPanel::buildStringsSection(QWidget *parent, QVBoxLayout *contentLayout)
{
    m_stringsHeader = new SectionHeader(tr("Strings"), parent);
    m_stringsHeader->setClickedCallback(
        [this]() { setSectionCollapsed(SectionId::Strings, !isSectionCollapsed(SectionId::Strings)); });
    contentLayout->addWidget(m_stringsHeader);
    m_stringsHeader->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_stringsHeaderGap = new QSpacerItem(0, kHeaderControlGap, QSizePolicy::Minimum, QSizePolicy::Fixed);
    contentLayout->addSpacerItem(m_stringsHeaderGap);

    m_stringsSectionBody = new QWidget(parent);
    m_stringsSectionBody->setMinimumWidth(0);
    auto *stringsBodyLayout = new QVBoxLayout(m_stringsSectionBody);
    stringsBodyLayout->setContentsMargins(kSectionHeaderOuterMargin + kCardLeftInset, 0,
                                          kSectionHeaderOuterMargin + kCardScrollbarInset, 0);
    stringsBodyLayout->setSpacing(0);

    auto startStrings = [this]()
    {
        if (m_stringsState.started)
            return;
        m_stringsState.started = true;
        startStringScan();
    };
    m_stringsOperation = new SectionOperationStrip(
        parent, startStrings,
        [this]() { cancelStringScan(); },
        [this]() { resumeStringScan(); },
        [this, startStrings]()
        {
            if (m_stringsListFrame)
                m_stringsListFrame->clearList();
            startStrings();
        });

    auto *stringsControlsStack       = new QWidget(m_stringsSectionBody);
    auto *stringsControlsStackLayout = new QVBoxLayout(stringsControlsStack);
    stringsControlsStackLayout->setContentsMargins(kSettingsCardShadowInset, 0, kSettingsCardShadowInset, 0);
    stringsControlsStackLayout->setSpacing(0);

    auto *stringsOptionsMenu = new QMenu(this);
    themeMenu(stringsOptionsMenu);
    m_includeWhitespaceAction = stringsOptionsMenu->addAction(tr("Include whitespace"));
    m_includeWhitespaceAction->setCheckable(true);
    m_includeWhitespaceAction->setChecked(true);
    stringsOptionsMenu->addSeparator();
    m_prefixHexOffsetAction = stringsOptionsMenu->addAction(tr("Prefix hex offset"));
    m_prefixHexOffsetAction->setCheckable(true);
    m_prefixHexOffsetAction->setChecked(false);

    m_stringOptionsButton = new QToolButton(m_stringsSectionBody);
    m_stringOptionsButton->setFixedSize(28, 28);
    m_stringOptionsButton->setFocusPolicy(Qt::TabFocus);
    m_stringOptionsButton->setToolTip(tr("String options"));
    m_stringOptionsButton->setAutoRaise(true);
    m_stringOptionsButton->setPopupMode(QToolButton::InstantPopup);
    m_stringOptionsButton->setProperty("iconThemeName", QStringLiteral("permissions"));
    m_stringOptionsButton->setProperty("iconSize", 16);
    m_stringOptionsButton->setIconSize(QSize(16, 16));
    m_stringOptionsButton->setIcon(
        recoloredIcon(QStringLiteral("permissions"), palette().buttonText().color(), 16));
    {
        const bool    dark    = QApplication::palette().window().color().lightness() < 128;
        const QString hover   = dark ? QStringLiteral("rgba(255,255,255,0.15)") : QStringLiteral("rgba(0,0,0,0.10)");
        const QString pressed = dark ? QStringLiteral("rgba(255,255,255,0.25)") : QStringLiteral("rgba(0,0,0,0.18)");
        m_stringOptionsButton->setStyleSheet(QStringLiteral(R"(
            QToolButton {
                border: none;
                border-radius: 6px;
                background: transparent;
            }
            QToolButton:hover { background: %1; }
            QToolButton:focus { border: 2px solid palette(highlight); }
            QToolButton:pressed { background: %2; }
            QToolButton::menu-indicator { image: none; width: 0; }
        )").arg(hover, pressed));
    }

    m_stringEncoding = new MenuComboBox(m_stringsSectionBody);
    m_stringEncoding->addItem(tr("Printable ASCII"), QStringLiteral("[ -~]"));
    m_stringEncoding->addItem(tr("Alphanumeric"),    QStringLiteral("[A-Za-z0-9]"));
    m_stringEncoding->addItem(tr("ASCII text"),      QStringLiteral("[\\t -~]"));
    m_stringEncoding->addItem(tr("C identifiers"),   QStringLiteral("[A-Za-z_][A-Za-z0-9_]*\\0"));
    m_stringEncoding->setCurrentIndex(1);
    m_stringEncoding->setFocusPolicy(Qt::StrongFocus);
    m_stringEncoding->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_stringEncoding->setFixedHeight(qMax(24, m_stringEncoding->sizeHint().height() - 4));
    m_stringEncoding->setToolTip(tr("Character filter"));

    m_minStringLength = new StepSpinBox(tr("Min:"), 3, 128, 1, m_stringsSectionBody);
    m_minStringLength->setValue(5);
    m_minStringLength->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_minStringLength->setLabelAlignment(Qt::AlignRight);
    m_minStringLength->setLabelValueSpacing(4);
    m_minStringLength->setValueBold(true);

    auto *stringsControls       = new QWidget(m_stringsSectionBody);
    auto *stringsControlsLayout = new QHBoxLayout(stringsControls);
    stringsControlsLayout->setContentsMargins(0, 0, 0, 0);
    stringsControlsLayout->setSpacing(kContentMargin + 6);
    stringsControlsLayout->addWidget(m_stringOptionsButton, 0, Qt::AlignVCenter);
    stringsControlsLayout->addWidget(m_stringEncoding, 0, Qt::AlignVCenter);
    stringsControlsLayout->addStretch(1);
    stringsControlsLayout->addWidget(m_minStringLength, 0);
    stringsControlsStackLayout->addWidget(stringsControls);
    stringsControlsStackLayout->addSpacing(kHeaderControlGap + 4);
    stringsControlsStackLayout->addWidget(m_stringsOperation->widget());
    stringsControlsStackLayout->addSpacing(kHeaderControlGap + 4);

    m_stringsListFrame = new StringListFrame(m_stringsSectionBody);
    m_stringsListFrame->setObjectName(QStringLiteral("stringsListFrame"));
    m_stringsListFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_stringsListFrame->setFixedHeight(kStringsListMinHeight);
    m_stringsListFrame->setStyleSheet(QStringLiteral(R"(
        QFrame#stringsListFrame {
            background: palette(base);
            border: 1px solid palette(mid);
            border-radius: 6px;
        }
    )"));

    m_stringsList = new QTreeWidget(m_stringsListFrame);
    m_stringsList->setHeader(new InlineSortHeader(Qt::Horizontal, m_stringsList));
    m_stringsList->setProperty("filePropertiesStringsList", true);
    m_stringsList->setMinimumSize(0, 0);
    m_stringsList->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_stringsList->setColumnCount(2);
    m_stringsList->setHeaderLabels({tr("String"), tr("Offset")});
    m_stringsList->setHeaderHidden(false);
    m_stringsList->setRootIsDecorated(false);
    m_stringsList->setAlternatingRowColors(false);
    m_stringsList->setUniformRowHeights(true);
    m_stringsList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_stringsList->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_stringsList->header()->setStretchLastSection(false);
    m_stringsList->header()->setSortIndicatorShown(true);
    {
        QFont headerFont = m_stringsList->header()->font();
        if (headerFont.pointSizeF() > 0)
            headerFont.setPointSizeF(qMax(1.0, headerFont.pointSizeF() - 1.0));
        else if (headerFont.pixelSize() > 0)
            headerFont.setPixelSize(qMax(1, headerFont.pixelSize() - 1));
        headerFont.setWeight(QFont::DemiBold);
        const QFontMetrics headerMetrics(headerFont);
        const int          headerPad             = stringsHeaderPadding(headerMetrics);
        constexpr int      kStringsItemCellInset = 3;
        const int          stringsItemLeftPad    = qMax(0, headerPad - kStringsItemCellInset);
        const int          headerHeight =
            headerMetrics.height() + 2 * headerPad + kStringsResultHeaderBottomGap;
        m_stringsList->header()->setFixedHeight(headerHeight);
        m_stringsList->setStyleSheet(
            QStringLiteral(
                "QTreeWidget[filePropertiesStringsList=true]::item { padding: 3px 6px 3px %1px; }")
                .arg(stringsItemLeftPad));
    }
    m_stringsList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_stringsList->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_stringsList->setSortingEnabled(true);
    m_stringsList->header()->setSortIndicator(1, Qt::AscendingOrder);
    new StringsHeaderCorner(m_stringsList);
    updateStringsOffsetColumnWidth();

    m_stringsListFrame->setListWidget(m_stringsList);
    stringsControlsStackLayout->addWidget(m_stringsListFrame);

    // Status bar: label + Next/All/Export buttons
    m_stringsStatusRow        = new QWidget(m_stringsSectionBody);
    auto *stringsStatusLayout = new QGridLayout(m_stringsStatusRow);
    stringsStatusLayout->setContentsMargins(6, 6, 6, 2);
    stringsStatusLayout->setHorizontalSpacing(8);
    stringsStatusLayout->setVerticalSpacing(0);
    m_stringsStatusLabel = new QLabel(m_stringsStatusRow);
    m_stringsStatusLabel->setWordWrap(true);
    m_stringsStatusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    {
        QFont statusFont = m_stringsStatusLabel->font();
        if (statusFont.pointSizeF() > 0)
            statusFont.setPointSizeF(qMax(1.0, statusFont.pointSizeF() - 1.0));
        else if (statusFont.pixelSize() > 0)
            statusFont.setPixelSize(qMax(1, statusFont.pixelSize() - 1));
        m_stringsStatusLabel->setFont(statusFont);
        m_stringsStatusLabel->setStyleSheet(
            QStringLiteral("color: %1;").arg(cssColor(subduedTextColor(palette()))));
    }
    m_stringsProgressLabel = new QLabel(m_stringsStatusRow);
    {
        QFont progressFont = m_stringsProgressLabel->font();
        if (progressFont.pointSizeF() > 0)
            progressFont.setPointSizeF(qMax(1.0, progressFont.pointSizeF() - 1.0));
        else if (progressFont.pixelSize() > 0)
            progressFont.setPixelSize(qMax(1, progressFont.pixelSize() - 1));
        m_stringsProgressLabel->setFont(progressFont);
        m_stringsProgressLabel->setStyleSheet(
            QStringLiteral("color: %1;").arg(cssColor(subduedTextColor(palette()))));
    }

    static constexpr int kStringsFooterButtonSize = 24;
    m_stringsNextButton = new QPushButton(m_stringsStatusRow);
    m_stringsNextButton->setFlat(false);
    m_stringsNextButton->setCursor(Qt::PointingHandCursor);
    m_stringsNextButton->setToolTip(tr("Next"));
    m_stringsNextButton->setFixedSize(kStringsFooterButtonSize, kStringsFooterButtonSize);
    m_stringsNextButton->setIcon(
        recoloredIcon(QStringLiteral("ui/go-next-symbolic"), palette().buttonText().color(), 12));
    m_stringsNextButton->setIconSize(QSize(12, 12));

    m_stringsAllButton = new QPushButton(m_stringsStatusRow);
    m_stringsAllButton->setFlat(false);
    m_stringsAllButton->setCursor(Qt::PointingHandCursor);
    m_stringsAllButton->setToolTip(tr("Complete scan to end of file"));
    m_stringsAllButton->setFixedSize(kStringsFooterButtonSize, kStringsFooterButtonSize);
    m_stringsAllButton->setIcon(
        recoloredIcon(QStringLiteral("actions/go-last-symbolic"), palette().buttonText().color(), 12));
    m_stringsAllButton->setIconSize(QSize(12, 12));

    m_stringsExportButton = new QPushButton(m_stringsStatusRow);
    m_stringsExportButton->setFlat(false);
    m_stringsExportButton->setCursor(Qt::PointingHandCursor);
    m_stringsExportButton->setToolTip(tr("Export results"));
    m_stringsExportButton->setFixedSize(kStringsFooterButtonSize, kStringsFooterButtonSize);
    m_stringsExportButton->setIcon(
        recoloredIcon(QStringLiteral("actions/downloads"), palette().buttonText().color(), 12));
    m_stringsExportButton->setIconSize(QSize(12, 12));

    {
        const QColor nextBg       = palette().button().color();
        const bool   nextDark     = palette().window().color().lightness() < 128;
        auto         overlayColor = [](const QColor &base, const QColor &overlay, int alpha)
        {
            const int inv = 255 - alpha;
            return QColor((base.red() * inv + overlay.red() * alpha) / 255,
                          (base.green() * inv + overlay.green() * alpha) / 255,
                          (base.blue() * inv + overlay.blue() * alpha) / 255);
        };
        const QColor nextHover    = nextDark ? overlayColor(nextBg, QColor(255, 255, 255), 30)
                                             : overlayColor(nextBg, QColor(0, 0, 0), 22);
        const QColor nextPressed  = palette().mid().color();
        const QColor nextBorder   = palette().mid().color();
        const QString stringsFooterButtonStyle =
            QStringLiteral(R"(
            QPushButton {
                border: 1px solid %1;
                border-radius: 6px;
                min-width: %6px;
                max-width: %6px;
                min-height: %6px;
                max-height: %6px;
                padding: 0px;
                color: %2;
                background: %3;
            }
            QPushButton:hover    { background: %4; }
            QPushButton:pressed  { background: %5; }
        )")
                .arg(cssColor(nextBorder), cssColor(palette().buttonText().color()), cssColor(nextBg),
                     cssColor(nextHover), cssColor(nextPressed), QString::number(kStringsFooterButtonSize));
        m_stringsNextButton->setStyleSheet(stringsFooterButtonStyle);
        m_stringsAllButton->setStyleSheet(stringsFooterButtonStyle);
        m_stringsExportButton->setStyleSheet(stringsFooterButtonStyle);
    }

    stringsStatusLayout->addWidget(m_stringsStatusLabel,   0, 0, Qt::AlignVCenter);
    stringsStatusLayout->addWidget(m_stringsNextButton,    0, 1, Qt::AlignVCenter);
    stringsStatusLayout->addWidget(m_stringsAllButton,     0, 2, Qt::AlignVCenter);
    stringsStatusLayout->addWidget(m_stringsExportButton,  0, 3, Qt::AlignVCenter);
    stringsStatusLayout->addWidget(m_stringsProgressLabel, 1, 0);
    stringsStatusLayout->setColumnStretch(0, 1);
    m_stringsStatusRow->hide();

    auto *stringsResizeWrap   = new QWidget(m_stringsSectionBody);
    auto *stringsResizeLayout = new QVBoxLayout(stringsResizeWrap);
    stringsResizeLayout->setContentsMargins(0, 0, 0, 0);
    stringsResizeLayout->setSpacing(0);
    m_stringsResizeHandle = new VerticalResizeHandle(
        [this](int dy) { resizeSection(SectionId::Strings, dy); }, stringsResizeWrap);
    stringsResizeLayout->addWidget(m_stringsResizeHandle);
    stringsResizeLayout->setAlignment(m_stringsResizeHandle, Qt::AlignTop);
    stringsControlsStackLayout->addWidget(stringsResizeWrap);
    stringsControlsStackLayout->addWidget(m_stringsStatusRow);
    stringsControlsStackLayout->addSpacing(2 * kStringsFooterButtonSize / 3);
    stringsBodyLayout->addWidget(stringsControlsStack);
    contentLayout->addWidget(m_stringsSectionBody);

    registerPanelSection({
        SectionId::Strings,
        tr("Strings"),
        m_stringsHeader,
        m_stringsSectionBody,
        m_stringsHeaderGap,
        nullptr,
        m_stringsListFrame,
        kStringsListMinHeight,
        [this]() { maybeStartStringScan(); },
        [this](bool collapsed)
        {
            if (m_stringsState.pause && m_stringsState.started)
            {
                if (collapsed)
                {
                    m_stringsState.pausedByCollapse = true;
                    m_stringsState.pause->setPaused(true);
                }
                else if (m_stringsState.pausedByCollapse && m_stringsOperation)
                {
                    m_stringsOperation->setProgressActionResume();
                }
            }
            if (m_stringsState.started)
                setStringsProgressTitle(m_stringsState.progress);
        },
        [this](bool contentsChanged) { if (contentsChanged) markStringsContentsChanged(); },
        [this]() { resetStringsForCurrentDocument(); },
    });

    // Signal connections for strings controls
    auto markStringsOptionsChanged = [this]()
    {
        m_stringsState.started          = false;
        m_stringsState.pausedByCollapse = false;
        ++m_stringsState.generation;
        if (m_stringsState.cancel)
            m_stringsState.cancel->store(true);
        if (m_stringsState.pause)
            m_stringsState.pause->wake();
        m_stringMoreAvailable         = false;
        m_stringsState.rescanRequired = true;
        m_stringsState.rescanMessage  = tr("Options changed");
        m_stringNextOffset            = 0;
        clearStringExportTemp();
        if (m_stringsListFrame)
            m_stringsListFrame->clearList();
        if (m_stringsStatusRow)
            m_stringsStatusRow->hide();
        if (m_stringsOperation)
            m_stringsOperation->showRescan(m_stringsState.rescanMessage);
        requestSectionLayoutRefresh(SectionId::Strings);
    };
    connect(m_minStringLength, &StepSpinBox::valueChanged, this,
            [markStringsOptionsChanged](int) { markStringsOptionsChanged(); });
    connect(m_stringEncoding, &QComboBox::currentIndexChanged, this,
            [markStringsOptionsChanged](int) { markStringsOptionsChanged(); });
    connect(m_includeWhitespaceAction, &QAction::toggled, this,
            [markStringsOptionsChanged](bool) { markStringsOptionsChanged(); });
    connect(m_prefixHexOffsetAction, &QAction::toggled, this,
            [markStringsOptionsChanged](bool) { markStringsOptionsChanged(); });

    connect(m_stringOptionsButton, &QToolButton::clicked, this,
            [this, stringsOptionsMenu]()
            {
                if (stringsOptionsMenu->isVisible())
                {
                    stringsOptionsMenu->hide();
                    return;
                }
                const QPoint cur            = QCursor::pos();
                const bool   same           = (m_stringOptionsMenuClosePos == cur);
                m_stringOptionsMenuClosePos = {-1, -1};
                if (same)
                    return;
                connect(
                    stringsOptionsMenu, &QMenu::aboutToHide, this,
                    [this]() { m_stringOptionsMenuClosePos = QCursor::pos(); },
                    Qt::SingleShotConnection);
                stringsOptionsMenu->popup(smartMenuPos(m_stringOptionsButton, stringsOptionsMenu));
            });
    connect(m_stringsNextButton, &QPushButton::clicked, this,
            [this]()
            {
                if (m_stringsState.started || !m_stringMoreAvailable)
                    return;
                m_stringsState.started = true;
                startStringScan(m_stringNextOffset, true);
            });
    connect(m_stringsAllButton, &QPushButton::clicked, this,
            [this]()
            {
                if (m_stringsState.started || !m_stringMoreAvailable)
                    return;
                m_stringsState.started = true;
                startStringScan(m_stringNextOffset, true, true);
            });
    connect(m_stringsExportButton, &QPushButton::clicked, this,
            &FilePropertiesPanel::exportStringResults);
    connect(m_stringsList, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem *item, int)
            {
                if (!item || !m_hexView || item->data(0, kStringFooterRole).toBool())
                    return;
                const size_w offset = static_cast<size_w>(item->data(0, Qt::UserRole).toULongLong());
                const size_w length = static_cast<size_w>(item->data(0, Qt::UserRole + 1).toULongLong());
                m_hexView->scrollCenterIfOffScreen(offset, length);
                m_hexView->setCurSel(offset, qMin(offset + length, m_hexView->size()));
                m_hexView->setFocus();
            });
    connect(m_stringsList, &QTreeWidget::itemActivated, this,
            [this](QTreeWidgetItem *item, int)
            {
                if (!item || !m_hexView || item->data(0, kStringFooterRole).toBool())
                    return;
                const size_w offset = static_cast<size_w>(item->data(0, Qt::UserRole).toULongLong());
                const size_w length = static_cast<size_w>(item->data(0, Qt::UserRole + 1).toULongLong());
                m_hexView->scrollCenterIfOffScreen(offset, length);
                m_hexView->setCurSel(offset, qMin(offset + length, m_hexView->size()));
                m_hexView->setFocus();
            });
}
