#ifndef FILEPROPERTIES_H
#define FILEPROPERTIES_H

#include <QDialog>
#include <QHash>
#include <QString>

class HexView;
class QLabel;
class QProgressBar;
class QResizeEvent;
class QScrollArea;
class QSpacerItem;
class SectionHeader;
class QWidget;

class FilePropertiesPanel : public QDialog
{
    Q_OBJECT
public:
    enum class Section { Properties, Checksums };

    explicit FilePropertiesPanel(HexView *hexView, QWidget *parent = nullptr);
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
    void resizeEvent(QResizeEvent *event) override;

private:
    static QString formatSize(qulonglong bytes);
    void startChecksumCalculation();
    void setChecksumRowsPending();
    void applyChecksumResults(int generation, const QHash<QString, QString> &results);
    void setFileSectionCollapsed(bool collapsed);
    void setChecksumSectionCollapsed(bool collapsed);
    void emitSectionReadyIfPossible(Section section);
    void updateStickyHeader();
    void syncStickyHeader();

    HexView *m_hexView = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_content = nullptr;
    QWidget *m_fileSectionBody = nullptr;
    QWidget *m_checksumSectionBody = nullptr;
    QSpacerItem *m_fileHeaderGap = nullptr;
    QSpacerItem *m_betweenSectionsGap = nullptr;
    QSpacerItem *m_checksumHeaderGap = nullptr;
    SectionHeader *m_fileHeader = nullptr;
    SectionHeader *m_checksumHeader = nullptr;
    SectionHeader *m_stickyHeader = nullptr;
    QLabel  *m_nameValue = nullptr;
    QLabel  *m_locationValue = nullptr;
    QLabel  *m_sizeValue = nullptr;
    QLabel  *m_stateValue = nullptr;
    QProgressBar *m_checksumProgress = nullptr;
    QHash<QString, QLabel *> m_checksumValues;
    int m_checksumGeneration = 0;
    bool m_checksumStarted = false;
    bool m_panelFullyOpened = false;
    bool m_fileSectionCollapsed = false;
    bool m_checksumSectionCollapsed = false;
};

#endif // FILEPROPERTIES_H
