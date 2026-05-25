#include "filestats/sidepanel.h"

#include "HexView/hexview.h"

#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QToolButton>
#include <QTreeWidget>

QString FilePropertiesPanel::formatSize(qulonglong bytes)
{
    return tr("%1 bytes").arg(QLocale().toString(bytes));
}

void FilePropertiesPanel::refresh()
{
    if (!m_hexView)
        return;

    const QString path = m_hexView->filePath();
    const QFileInfo info(path);

    m_nameValue->setText(path.isEmpty() ? tr("Untitled") : info.fileName());
    m_locationValue->setText(path.isEmpty() ? tr("Memory") : info.absolutePath());
    m_sizeValue->setText(formatSize(static_cast<qulonglong>(m_hexView->size())));
    m_stateValue->setText(m_hexView->canUndo() ? tr("Modified") : tr("Saved"));
    m_checksumStarted = false;
    ++m_checksumGeneration;
    if (m_checksumCancel)
        m_checksumCancel->store(true);
    m_stringsStarted = false;
    if (m_checksumProgress)
        m_checksumProgress->hide();
    if (m_checksumStopButton)
        m_checksumStopButton->hide();
    if (m_checksumProgressRow)
        m_checksumProgressRow->hide();
    if (m_checksumRecalculateStrip)
        m_checksumRecalculateStrip->hide();
    cancelStringScan();
    if (m_stringsRecalculateStrip)
        m_stringsRecalculateStrip->hide();
    if (m_stringsList)
        m_stringsList->clear();
}
