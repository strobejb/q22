#ifndef FILESTATS_SIDEPANEL_H
#define FILESTATS_SIDEPANEL_H

#include <QDialog>
#include <QHash>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <atomic>
#include <memory>

class HexView;
class QEvent;
class QProgressBar;
class QResizeEvent;
class QScrollArea;
class QSpacerItem;
class QComboBox;
class QLabel;
class QToolButton;
class QTreeWidget;
class StepSpinBox;
class QWidget;

namespace filestats {
class SectionHeader;
}

class FilePropertiesPanel : public QDialog
{
    Q_OBJECT
public:
    enum class Section { Properties, Checksums, Strings };
    Q_ENUM(Section)

    explicit FilePropertiesPanel(HexView *hexView, QWidget *parent = nullptr);
    ~FilePropertiesPanel() override;
    void showSection(Section section);
    void setPanelFullyOpened(bool opened);

signals:
    void closeRequested();
    void sectionExpanded(Section section);
    void sectionReady(Section section);

public slots:
    void refresh();
    void maybeStartChecksumCalculation();

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    static QString formatSize(qulonglong bytes);
    void startChecksumCalculation();
    void maybeStartStringScan();
    void startStringScan();
    void cancelChecksumCalculation();
    void cancelStringScan();
    void resizeStringsList(int dy);
    void showRecalculateStrip(QWidget *strip, const QString &message);
    void setChecksumRowsPending();
    void updateChecksumProgress(int generation, int value);
    void applyChecksumResults(int generation, const QHash<QString, QString> &results);
    void updateStringProgress(int generation, int value);
    void appendStringResults(int generation, const QVector<QVariantMap> &results);
    void applyStringResults(int generation, const QVector<QVariantMap> &results);
    void setFileSectionCollapsed(bool collapsed);
    void setChecksumSectionCollapsed(bool collapsed);
    void setStringsSectionCollapsed(bool collapsed);
    void emitSectionReadyIfPossible(Section section);
    void updateStickyHeader();
    void syncStickyHeader();

    HexView *m_hexView = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_content = nullptr;
    QWidget *m_fileSectionBody = nullptr;
    QWidget *m_checksumSectionBody = nullptr;
    QWidget *m_stringsSectionBody = nullptr;
    QSpacerItem *m_fileHeaderGap = nullptr;
    QSpacerItem *m_betweenSectionsGap = nullptr;
    QSpacerItem *m_checksumHeaderGap = nullptr;
    QSpacerItem *m_betweenChecksumStringsGap = nullptr;
    QSpacerItem *m_stringsHeaderGap = nullptr;
    QSpacerItem *m_stringsResizeSlack = nullptr;
    filestats::SectionHeader *m_fileHeader = nullptr;
    filestats::SectionHeader *m_checksumHeader = nullptr;
    filestats::SectionHeader *m_stringsHeader = nullptr;
    filestats::SectionHeader *m_stickyHeader = nullptr;
    QLabel  *m_nameValue = nullptr;
    QLabel  *m_locationValue = nullptr;
    QLabel  *m_sizeValue = nullptr;
    QLabel  *m_stateValue = nullptr;
    QWidget *m_checksumProgressRow = nullptr;
    QProgressBar *m_checksumProgress = nullptr;
    QToolButton *m_checksumStopButton = nullptr;
    QWidget *m_checksumRecalculateStrip = nullptr;
    QHash<QString, QLabel *> m_checksumValues;
    QWidget *m_stringsProgressRow = nullptr;
    QProgressBar *m_stringsProgress = nullptr;
    QToolButton *m_stringsStopButton = nullptr;
    QWidget *m_stringsRecalculateStrip = nullptr;
    StepSpinBox *m_minStringLength = nullptr;
    QComboBox *m_stringEncoding = nullptr;
    QTreeWidget *m_stringsList = nullptr;
    QWidget *m_stringsResizeHandle = nullptr;
    std::shared_ptr<std::atomic_bool> m_checksumCancel;
    std::shared_ptr<std::atomic_bool> m_stringCancel;
    int m_checksumGeneration = 0;
    int m_stringGeneration = 0;
    bool m_checksumStarted = false;
    bool m_stringsStarted = false;
    bool m_panelFullyOpened = false;
    bool m_fileSectionCollapsed = false;
    bool m_checksumSectionCollapsed = false;
    bool m_stringsSectionCollapsed = false;
    int m_stringsResizeSlackHeight = 0;
};

#endif // FILESTATS_SIDEPANEL_H
