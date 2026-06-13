#include "filestats/sidepanel.h"

#include "HexView/hexview.h"
#include "filestats/widgets.h"
#include "settings/settingscard.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

using namespace filestats;

QString FilePropertiesPanel::formatSize(qulonglong bytes)
{
    const QLocale locale;
    return tr("%1 (%2 bytes)").arg(locale.formattedDataSize(bytes),
                                  locale.toString(bytes));
}

void FilePropertiesPanel::refresh()
{
    refreshDocumentState(true);
}

void FilePropertiesPanel::resetForCurrentDocument()
{
    refreshDocumentState(false);
}

void FilePropertiesPanel::refreshDocumentState(bool contentsChanged)
{
    if (!m_hexView)
        return;

    const QString path = m_hexView->filePath();
    const QFileInfo info(path);

    m_nameValue->setText(path.isEmpty() ? tr("Untitled") : info.fileName());
    m_locationValue->setText(path.isEmpty() ? tr("Memory") : info.absolutePath());
    m_sizeValue->setText(formatSize(static_cast<qulonglong>(m_hexView->size())));
    updateStringsOffsetColumnWidth();

    for (PanelSection &section : m_sections) {
        if (!contentsChanged && section.onResetForCurrentDocument)
            section.onResetForCurrentDocument();
        if (section.onRefreshDocumentState)
            section.onRefreshDocumentState(contentsChanged);
    }
}

void FilePropertiesPanel::markChecksumContentsChanged()
{
    m_checksumState.started = false;
    m_checksumState.pausedByCollapse = false;
    ++m_checksumState.generation;
    if (m_checksumState.cancel)
        m_checksumState.cancel->store(true);
    if (m_checksumState.pause)
        m_checksumState.pause->wake();
    resetChecksumTitle();

    m_checksumState.rescanRequired = true;
    m_checksumState.rescanMessage = tr("File contents changed");
    if (m_checksumOperation)
        m_checksumOperation->showRescan(m_checksumState.rescanMessage);
    requestSectionLayoutRefresh(SectionId::Checksums);
}

void FilePropertiesPanel::resetChecksumForCurrentDocument()
{
    m_checksumState.started = false;
    m_checksumState.pausedByCollapse = false;
    ++m_checksumState.generation;
    if (m_checksumState.cancel)
        m_checksumState.cancel->store(true);
    if (m_checksumState.pause)
        m_checksumState.pause->wake();
    resetChecksumTitle();
    m_checksumState.autoStartConsumed = false;
    m_checksumState.rescanRequired = false;
    m_checksumState.rescanMessage.clear();
    if (m_checksumOperation)
        m_checksumOperation->clear();
    for (QLabel *label : m_checksumValues) {
        if (label)
            label->clear();
    }
    requestSectionLayoutRefresh(SectionId::Checksums);
}

void FilePropertiesPanel::markStringsContentsChanged()
{
    m_stringsState.started = false;
    m_stringsState.pausedByCollapse = false;
    ++m_stringsState.generation;
    if (m_stringsState.cancel)
        m_stringsState.cancel->store(true);
    if (m_stringsState.pause)
        m_stringsState.pause->wake();
    m_stringMoreAvailable = false;
    m_stringNextOffset = 0;
    clearStringExportTemp();
    resetStringsTitle();
    if (m_stringsStatusRow)
        m_stringsStatusRow->hide();

    m_stringsState.rescanRequired = true;
    m_stringsState.rescanMessage = tr("File contents changed");
    if (m_stringsOperation)
        m_stringsOperation->showRescan(m_stringsState.rescanMessage);
    requestSectionLayoutRefresh(SectionId::Strings);
}

void FilePropertiesPanel::resetStringsForCurrentDocument()
{
    m_stringsState.started = false;
    m_stringsState.pausedByCollapse = false;
    ++m_stringsState.generation;
    if (m_stringsState.cancel)
        m_stringsState.cancel->store(true);
    if (m_stringsState.pause)
        m_stringsState.pause->wake();
    m_stringMoreAvailable = false;
    m_stringNextOffset = 0;
    clearStringExportTemp();
    resetStringsTitle();
    if (m_stringsStatusRow)
        m_stringsStatusRow->hide();
    m_stringsState.autoStartConsumed = false;
    m_stringsState.rescanRequired = false;
    m_stringsState.rescanMessage.clear();
    if (m_stringsOperation)
        m_stringsOperation->clear();
    if (m_stringsListFrame)
        m_stringsListFrame->clearList();
    else if (m_stringsList)
        m_stringsList->clear();
    requestSectionLayoutRefresh(SectionId::Strings);
}

void FilePropertiesPanel::buildPropertiesSection(QWidget *parent, QVBoxLayout *contentLayout)
{
    m_fileHeader = new SectionHeader(tr("File Information"), parent);
    m_fileHeader->setClickedCallback(
        [this]() { setSectionCollapsed(SectionId::Properties, !isSectionCollapsed(SectionId::Properties)); });
    contentLayout->addWidget(m_fileHeader);
    m_fileHeader->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_fileHeaderGap = new QSpacerItem(0, kHeaderControlGap, QSizePolicy::Minimum, QSizePolicy::Fixed);
    contentLayout->addSpacerItem(m_fileHeaderGap);

    m_fileSectionBody = new QWidget(parent);
    m_fileSectionBody->setMinimumWidth(0);
    auto *fileBodyLayout = new QVBoxLayout(m_fileSectionBody);
    fileBodyLayout->setContentsMargins(kSectionHeaderOuterMargin + kCardLeftInset, 0,
                                       kSectionHeaderOuterMargin + kCardScrollbarInset, 0);
    fileBodyLayout->setSpacing(0);

    auto *card = new SettingsCard(
        {
            new PropertyRow(tr("Name"), &m_nameValue, m_fileSectionBody),
            new PropertyRow(tr("Location"), &m_locationValue, m_fileSectionBody, PropertyRow::Action::OpenExternal,
                            [this]()
                            {
                                if (!m_hexView)
                                    return;
                                const QString path = m_hexView->filePath();
                                if (!path.isEmpty())
                                    QDesktopServices::openUrl(
                                        QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
                            }),
            new PropertyRow(tr("Size"), &m_sizeValue, m_fileSectionBody),
        },
        SettingsCard::Style::Spaced, m_fileSectionBody);
    card->setMinimumWidth(0);
    fileBodyLayout->addWidget(card);
    contentLayout->addWidget(m_fileSectionBody);

    registerPanelSection({
        SectionId::Properties,
        tr("File Information"),
        m_fileHeader,
        m_fileSectionBody,
        m_fileHeaderGap,
    });
}
