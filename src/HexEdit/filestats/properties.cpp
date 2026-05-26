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
    if (m_checksumOperation)
        m_checksumOperation->clear();
    cancelStringScan();
    if (m_stringsOperation)
        m_stringsOperation->clear();
    if (m_stringsList)
        m_stringsList->clear();
}
