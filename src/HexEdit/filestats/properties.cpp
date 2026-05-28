#include "filestats/sidepanel.h"

#include "HexView/hexview.h"
#include "filestats/widgets.h"

#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QToolButton>
#include <QTreeWidget>

QString FilePropertiesPanel::formatSize(qulonglong bytes)
{
    const QLocale locale;
    return tr("%1 (%2 bytes)").arg(locale.formattedDataSize(bytes),
                                  locale.toString(bytes));
}

void FilePropertiesPanel::refresh()
{
    if (!m_hexView)
        return;

    const bool contentsChanged = m_hasRefreshed;
    m_hasRefreshed = true;
    const QString path = m_hexView->filePath();
    const QFileInfo info(path);

    m_nameValue->setText(path.isEmpty() ? tr("Untitled") : info.fileName());
    m_locationValue->setText(path.isEmpty() ? tr("Memory") : info.absolutePath());
    m_sizeValue->setText(formatSize(static_cast<qulonglong>(m_hexView->size())));

    m_checksumStarted = false;
    ++m_checksumGeneration;
    if (m_checksumCancel)
        m_checksumCancel->store(true);
    resetChecksumTitle();

    m_stringsStarted = false;
    ++m_stringGeneration;
    if (m_stringCancel)
        m_stringCancel->store(true);
    m_stringMoreAvailable = false;
    m_stringNextOffset = 0;
    clearStringExportTemp();
    resetStringsTitle();
    if (m_stringsStatusRow)
        m_stringsStatusRow->hide();

    if (!contentsChanged) {
        m_checksumRescanRequired = false;
        m_stringsRescanRequired = false;
        m_checksumRescanMessage.clear();
        m_stringsRescanMessage.clear();
        if (m_checksumOperation)
            m_checksumOperation->clear();
        if (m_stringsOperation)
            m_stringsOperation->clear();
        if (m_stringsList)
            m_stringsList->clear();
        return;
    }

    m_checksumRescanRequired = true;
    m_stringsRescanRequired = true;
    m_checksumRescanMessage = tr("File contents changed");
    m_stringsRescanMessage = tr("File contents changed");
    if (m_checksumOperation)
        m_checksumOperation->showRescan(m_checksumRescanMessage);
    if (m_stringsOperation)
        m_stringsOperation->showRescan(m_stringsRescanMessage);
}
