#include "filestats/sidepanel.h"

#include "HexView/hexview.h"
#include "HexView/sequencedevice.h"
#include "filestats/checksumscan.h"
#include "filestats/widgets.h"
#include "settings/settingscard.h"
#include "theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHash>
#include <QLabel>
#include <QMetaObject>
#include <QPointer>
#include <QProgressBar>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QToolButton>

#include <atomic>
#include <functional>
#include <memory>
#include <utility>

using namespace filestats;

QStringList FilePropertiesPanel::selectedChecksumAlgorithms() const
{
    QStringList algorithms;
    for (const QString &name : checksumAlgorithmNames()) {
        if (QCheckBox *checkBox = m_checksumChecks.value(name)) {
            if (checkBox->isChecked())
                algorithms.append(name);
        }
    }
    return algorithms;
}

void FilePropertiesPanel::markChecksumAlgorithmsChanged()
{
    m_checksumState.started = false;
    m_checksumState.pausedByCollapse = false;
    m_checksumState.rescanRequired = true;
    m_checksumState.rescanMessage = tr("Algorithm changed");
    ++m_checksumState.generation;
    if (m_checksumState.cancel)
        m_checksumState.cancel->store(true);
    if (m_checksumState.pause)
        m_checksumState.pause->wake();
    resetChecksumTitle();
    if (m_checksumOperation)
        m_checksumOperation->showRescan(m_checksumState.rescanMessage);
    requestSectionLayoutRefresh(SectionId::Checksums);
}

void FilePropertiesPanel::maybeStartChecksumCalculation()
{
    if (!m_panelFullyOpened)
        return;
    if (isSectionCollapsed(SectionId::Checksums))
        return;
    if (m_checksumState.started)
        return;
    if (m_checksumState.rescanRequired) {
        if (m_checksumOperation)
            m_checksumOperation->showRescan(m_checksumState.rescanMessage.isEmpty()
                                            ? tr("File contents changed")
                                            : m_checksumState.rescanMessage);
        requestSectionLayoutRefresh(SectionId::Checksums);
        return;
    }
    if (m_checksumState.autoStartConsumed)
        return;
    if (!shouldAutoStartOperations()) {
        if (m_checksumOperation && !m_checksumOperation->hasOperation())
            m_checksumOperation->showStart(tr("Begin scan"));
        return;
    }
    m_checksumState.started = true;
    startChecksumCalculation();
}

void FilePropertiesPanel::setChecksumRowsPending()
{
    const QStringList algorithms = selectedChecksumAlgorithms();
    const QSet<QString> selected(algorithms.cbegin(), algorithms.cend());
    for (auto it = m_checksumValues.begin(); it != m_checksumValues.end(); ++it)
        it.value()->setText(selected.contains(it.key()) ? tr("Calculating...")
                                                        : tr("Not selected"));
    setChecksumProgressTitle(0);
    if (m_checksumOperation)
        m_checksumOperation->showProgress();
    requestSectionLayoutRefresh(SectionId::Checksums);
}

void FilePropertiesPanel::startChecksumCalculation()
{
    if (!m_hexView) {
        m_checksumState.started = false;
        m_checksumState.pausedByCollapse = false;
        return;
    }

    m_checksumState.autoStartConsumed = true;
    m_checksumState.rescanRequired = false;
    m_checksumState.rescanMessage.clear();
    m_checksumState.pausedByCollapse = isSectionCollapsed(SectionId::Checksums);
    const int generation = ++m_checksumState.generation;
    if (m_checksumState.cancel)
        m_checksumState.cancel->store(true);
    if (m_checksumState.pause)
        m_checksumState.pause->wake();
    auto cancelFlag = std::make_shared<std::atomic_bool>(false);
    m_checksumState.cancel = cancelFlag;
    auto pause = std::make_shared<filestats::OperationPause>();
    pause->setPaused(isSectionCollapsed(SectionId::Checksums));
    m_checksumState.pause = pause;
    setChecksumRowsPending();

    const QStringList algorithms = selectedChecksumAlgorithms();
    auto inputDeviceOwner = m_hexView->createReadOnlyDeviceSnapshot();
    if (!inputDeviceOwner)
    {
        applyChecksumResults(generation, unavailableChecksums(algorithms, tr("Unable to read")));
        return;
    }
    auto *inputDevice = inputDeviceOwner.release();

    QPointer<FilePropertiesPanel> guard(this);
    auto *thread = QThread::create([guard, generation, inputDevice, algorithms, cancelFlag, pause]() {
        std::unique_ptr<SequenceDevice> inputOwner(inputDevice);
        QIODevice &input = *inputOwner;
        ChecksumScanCallbacks callbacks;
        callbacks.shouldContinue = [cancelFlag, pause]() -> bool
        {
            if (cancelFlag->load())
                return false;
            return !pause || pause->waitIfPaused(cancelFlag);
        };
        callbacks.progress = [guard, generation](int progress)
        {
            QMetaObject::invokeMethod(qApp, [guard, generation, progress]() {
                if (guard)
                    guard->updateChecksumProgress(generation, progress);
            }, Qt::QueuedConnection);
        };

        const QHash<QString, QString> results = calculateChecksums(input, algorithms, callbacks);
        if (cancelFlag->load())
            return;
        QMetaObject::invokeMethod(qApp, [guard, generation, results]() {
            if (guard)
                guard->applyChecksumResults(generation, results);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void FilePropertiesPanel::updateChecksumProgress(int generation, int value)
{
    if (generation != m_checksumState.generation || !m_checksumOperation)
        return;
    const int progress = qBound(0, value, 1000);
    m_checksumOperation->progressBar()->setValue(progress);
    setChecksumProgressTitle(progress);
}

void FilePropertiesPanel::applyChecksumResults(int generation, const QHash<QString, QString> &results)
{
    if (generation != m_checksumState.generation)
        return;

    m_checksumState.started = false;
    m_checksumState.pausedByCollapse = false;
    const QStringList algorithms = selectedChecksumAlgorithms();
    const QSet<QString> selected(algorithms.cbegin(), algorithms.cend());
    for (auto it = m_checksumValues.begin(); it != m_checksumValues.end(); ++it) {
        if (!selected.contains(it.key()))
            it.value()->setText(tr("Not selected"));
    }
    for (auto it = results.cbegin(); it != results.cend(); ++it) {
        if (QLabel *label = m_checksumValues.value(it.key()))
            label->setText(it.value());
    }
    if (m_checksumOperation)
        m_checksumOperation->clear();
    resetChecksumTitle();
    requestSectionLayoutRefresh(SectionId::Checksums);
    QTimer::singleShot(0, this, [this]() { repairExpandedSectionGeometry(SectionId::Checksums); });
}

void FilePropertiesPanel::cancelChecksumCalculation()
{
    ++m_checksumState.generation;
    m_checksumState.started = false;
    m_checksumState.pausedByCollapse = false;
    if (m_checksumState.cancel)
        m_checksumState.cancel->store(true);
    if (m_checksumState.pause)
        m_checksumState.pause->wake();
    for (QLabel *label : std::as_const(m_checksumValues))
        label->setText(tr("Cancelled"));
    if (m_checksumOperation)
        m_checksumOperation->showRetry(tr("Operation cancelled"));
    resetChecksumTitle();
    requestSectionLayoutRefresh(SectionId::Checksums);
}

void FilePropertiesPanel::resumeChecksumCalculation()
{
    if (!m_checksumState.started || !m_checksumState.pause)
        return;

    m_checksumState.pausedByCollapse = false;
    m_checksumState.pause->wake();
    if (m_checksumOperation)
        m_checksumOperation->setProgressActionStop();
    const QStringList algorithms = selectedChecksumAlgorithms();
    const QSet<QString> selected(algorithms.cbegin(), algorithms.cend());
    for (auto it = m_checksumValues.begin(); it != m_checksumValues.end(); ++it)
        if (selected.contains(it.key()) && it.value()->text() == tr("Paused"))
            it.value()->setText(tr("Calculating..."));
    setChecksumProgressTitle(m_checksumState.progress);
    requestSectionLayoutRefresh(SectionId::Checksums);
}

void FilePropertiesPanel::buildChecksumSection(QWidget *parent, QVBoxLayout *contentLayout)
{
    m_checksumHeader = new SectionHeader(tr("Checksums"), parent);
    m_checksumHeader->setClickedCallback(
        [this]() { setSectionCollapsed(SectionId::Checksums, !isSectionCollapsed(SectionId::Checksums)); });
    contentLayout->addWidget(m_checksumHeader);
    m_checksumHeader->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_checksumHeaderGap = new QSpacerItem(0, kHeaderControlGap, QSizePolicy::Minimum, QSizePolicy::Fixed);
    contentLayout->addSpacerItem(m_checksumHeaderGap);

    m_checksumSectionBody = new QWidget(parent);
    m_checksumSectionBody->setMinimumWidth(0);
    auto *checksumBodyLayout = new QVBoxLayout(m_checksumSectionBody);
    checksumBodyLayout->setContentsMargins(kSectionHeaderOuterMargin + kCardLeftInset, 0,
                                           kSectionHeaderOuterMargin + kCardScrollbarInset, 0);
    checksumBodyLayout->setSpacing(0);

    auto startChecksums = [this]()
    {
        if (m_checksumState.started)
            return;
        m_checksumState.started = true;
        startChecksumCalculation();
    };
    m_checksumOperation = new SectionOperationStrip(
        parent, startChecksums,
        [this]() { cancelChecksumCalculation(); },
        [this]() { resumeChecksumCalculation(); },
        startChecksums);
    contentLayout->addWidget(m_checksumOperation->widget());

    auto checksumRow = [this](const QString &name, bool checked)
    {
        QLabel    *value    = nullptr;
        QCheckBox *checkBox = nullptr;
        auto *row = new PropertyRow(name, &value, m_checksumSectionBody, PropertyRow::Action::CopyValue, {},
                                    &checkBox, checked);
        m_checksumValues.insert(name, value);
        if (checkBox)
        {
            m_checksumChecks.insert(name, checkBox);
            connect(checkBox, &QCheckBox::toggled, this, &FilePropertiesPanel::markChecksumAlgorithmsChanged);
        }
        return row;
    };

    auto *checksumCard = new SettingsCard(
        {
            checksumRow(QStringLiteral("SHA512"),  false),
            checksumRow(QStringLiteral("SHA256"),  true),
            checksumRow(QStringLiteral("SHA1"),    false),
            checksumRow(QStringLiteral("MD5"),     true),
            checksumRow(QStringLiteral("CRC32"),   false),
            checksumRow(QStringLiteral("CRC32C"),  false),
            checksumRow(QStringLiteral("CRC16"),   false),
        },
        SettingsCard::Style::Spaced, m_checksumSectionBody);
    checksumCard->setMinimumWidth(0);
    checksumBodyLayout->addWidget(checksumCard);
    contentLayout->addWidget(m_checksumSectionBody);

    registerPanelSection({
        SectionId::Checksums,
        tr("Checksums"),
        m_checksumHeader,
        m_checksumSectionBody,
        m_checksumHeaderGap,
        m_checksumOperation,
        nullptr,
        0,
        [this]() { maybeStartChecksumCalculation(); },
        [this](bool collapsed)
        {
            if (m_checksumState.pause && m_checksumState.started)
            {
                if (collapsed)
                {
                    m_checksumState.pausedByCollapse = true;
                    m_checksumState.pause->setPaused(true);
                    const QStringList    algorithms = selectedChecksumAlgorithms();
                    const QSet<QString> selected(algorithms.cbegin(), algorithms.cend());
                    for (auto it = m_checksumValues.begin(); it != m_checksumValues.end(); ++it)
                        if (selected.contains(it.key()))
                            it.value()->setText(tr("Paused"));
                }
                else if (m_checksumState.pausedByCollapse && m_checksumOperation)
                {
                    m_checksumOperation->setProgressActionResume();
                }
            }
            if (m_checksumState.started)
                setChecksumProgressTitle(m_checksumState.progress);
        },
        [this](bool contentsChanged) { if (contentsChanged) markChecksumContentsChanged(); },
        [this]() { resetChecksumForCurrentDocument(); },
    });
}
