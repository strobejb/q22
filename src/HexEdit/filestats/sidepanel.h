#ifndef FILESTATS_SIDEPANEL_H
#define FILESTATS_SIDEPANEL_H

#include <QDialog>
#include <QHash>
#include <QPoint>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

class HexView;
class QAction;
namespace filestats { class EntropyView; }
class QEvent;
class QProgressBar;
class QResizeEvent;
class QScrollArea;
class QSpacerItem;
class QComboBox;
class QCheckBox;
class QLabel;
class QPropertyAnimation;
class QPushButton;
class QToolButton;
class QTimer;
class QTreeWidget;
class StepSpinBox;
class QWidget;

namespace filestats
{
class SectionOperationStrip;
class SectionHeader;
class StringListFrame;

struct OperationPause
{
    void setPaused(bool value)
    {
        paused.store(value);
        if (!value)
            cv.notify_all();
    }

    void wake()
    {
        paused.store(false);
        cv.notify_all();
    }

    bool waitIfPaused(const std::shared_ptr<std::atomic_bool> &cancelFlag)
    {
        if (!paused.load())
            return !cancelFlag->load();
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock,
                [this, &cancelFlag]()
                {
                    return !paused.load() || cancelFlag->load();
                });
        return !cancelFlag->load();
    }

    std::atomic_bool        paused{false};
    std::mutex              mutex;
    std::condition_variable cv;
};
} // namespace filestats

class FilePropertiesPanel : public QDialog
{
    Q_OBJECT
  public:
    enum class SectionId
    {
        Properties,
        Checksums,
        Strings,
        Entropy
    };
    Q_ENUM(SectionId)

    enum class EntropyMode  { Shannon, Bigram };

    explicit FilePropertiesPanel(HexView *hexView, QWidget *parent = nullptr);
    ~FilePropertiesPanel() override;
    void showSection(SectionId section);
    void setPanelFullyOpened(bool opened);

  signals:
    void closeRequested();
    void sectionExpanded(SectionId section);
    void sectionReady(SectionId section);

  public slots:
    void refresh();
    void resetForCurrentDocument();
    void maybeStartChecksumCalculation();

  protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

  private:
    static QString formatSize(qulonglong bytes);
    void           refreshDocumentState(bool contentsChanged);
    void           markChecksumContentsChanged();
    void           resetChecksumForCurrentDocument();
    void           markStringsContentsChanged();
    void           resetStringsForCurrentDocument();
    void           markEntropyContentsChanged();
    void           resetEntropyForCurrentDocument();
    void           maybeStartEntropyAnalysis();
    void           startEntropyAnalysis();
    void           cancelEntropyAnalysis();
    void           resumeEntropyAnalysis();
    void           updateEntropyProgress(int generation, int value);
    void           applyEntropyResults(int generation, QVector<float> data, qulonglong fileSize, int windowSize);
    void           applyBigramResults(int generation, QVector<quint64> counts, qulonglong fileSize);
    void           setEntropyProgressTitle(int value);
    void           resetEntropyTitle();
    void           updateEntropyStatsLabel();
    void           startChecksumCalculation();
    void           maybeStartStringScan();
    void           startStringScan(qulonglong startOffset = 0, bool append = false, bool scanAll = false);
    void           cancelChecksumCalculation();
    void           resumeChecksumCalculation();
    void           cancelStringScan();
    void           resumeStringScan();
    void           exportStringResults();
    void           clearStringExportTemp();
    QString        createStringExportTemp();
    QStringList    selectedChecksumAlgorithms() const;
    void           markChecksumAlgorithmsChanged();
    void           setChecksumRowsPending();
    void           updateChecksumProgress(int generation, int value);
    void           applyChecksumResults(int generation, const QHash<QString, QString> &results);
    void           updateStringProgress(int generation, int value);
    void           appendStringResults(int generation, const QVector<QVariantMap> &results);
    void           sortStringResults(int column, Qt::SortOrder order);
    void           removeStringTruncationItem();
    void           addStringTruncationItem(const QString &message);
    int            visibleStringResultCount() const;
    void finishStringScan(int generation, const QVector<QVariantMap> &results, bool capped, qulonglong nextOffset,
                          int progress, qulonglong totalResults, const QString &exportTempPath,
                          bool exportTempComplete);
    void setChecksumProgressTitle(int value);
    void setStringsProgressTitle(int value);
    void resetChecksumTitle();
    void resetStringsTitle();
    void updateStringsOffsetColumnWidth();
    void animateSectionBody(QWidget *body, bool collapse, bool animate = true);
    void setSectionCollapsed(SectionId section, bool collapsed, bool animate = true);
    void emitSectionReadyIfPossible(SectionId section);
    void updateStickyHeader();
    void syncStickyHeader();
    bool shouldAutoStartOperations() const;
    bool isSectionCollapsed(SectionId section) const;
    void updateInterSectionGaps();
    void rebuildSectionLayout();
    void repairExpandedSectionGeometry(SectionId section);
    void resizeSection(SectionId section, int dy);
    bool applyResizableSectionHeight(SectionId section);
    void scheduleResizableSectionRepair(SectionId section);
    void onDragStarted(SectionId s, QPoint globalPos);
    void onDragMoved(QPoint globalPos);
    void onDragEnded(SectionId s, QPoint globalPos);
    void collapseSectionsForDrag();
    void updateDropIndicator(QPoint globalPos);
    void syncSectionHeaderHover();
    void scheduleSectionHeaderHoverSync();
    void requestSectionLayoutRefresh(SectionId section);
    void performSectionLayoutRefresh();
    void settleContentLayout();
    void toggleSectionFullExpand(SectionId sectionId);
    void clearSectionFullExpand(SectionId sectionId);

    struct SectionResizeState
    {
        QWidget *target        = nullptr;
        int      minHeight     = 0;
        int      currentHeight = 0;
    };

    // Contract for hosted sections:
    // - Register every section with its header/body/gap, and optional operation strip.
    // - Provide a resizableTarget/minResizableHeight when the section has a vertical
    //   resize grip; the sidepanel will preserve and reapply that height.
    // - Call requestSectionLayoutRefresh(id) whenever section content intentionally
    //   changes vertical size, e.g. status rows, operation strips, or dynamic results.
    // - Use callbacks for section-owned lifecycle; the sidepanel stays responsible
    //   only for hosting, ordering, collapse state, and geometry.
    struct PanelSectionSpec
    {
        SectionId                         id = SectionId::Properties;
        QString                           title;
        filestats::SectionHeader         *header             = nullptr;
        QWidget                          *body               = nullptr;
        QSpacerItem                      *headerGap          = nullptr;
        filestats::SectionOperationStrip *operation          = nullptr;
        QWidget                          *resizableTarget    = nullptr;
        int                               minResizableHeight = 0;
        std::function<void()>             onExpanded;
        std::function<void(bool)>         onCollapsedChanged;
        std::function<void(bool)>         onRefreshDocumentState;
        std::function<void()>             onResetForCurrentDocument;
    };

    struct PanelSection
    {
        SectionId                         id = SectionId::Properties;
        QString                           title;
        filestats::SectionHeader         *header           = nullptr;
        QWidget                          *body             = nullptr;
        QSpacerItem                      *headerGap        = nullptr;
        filestats::SectionOperationStrip *operation        = nullptr;
        bool                              collapsed        = false;
        bool                              preDragCollapsed = false;
        SectionResizeState                resize;
        std::function<void()>             onExpanded;
        std::function<void(bool)>         onCollapsedChanged;
        std::function<void(bool)>         onRefreshDocumentState;
        std::function<void()>             onResetForCurrentDocument;
    };

    void                registerPanelSection(const PanelSectionSpec &spec);
    PanelSection       *sectionFor(SectionId section);
    const PanelSection *sectionFor(SectionId section) const;

    struct ScanSectionState
    {
        std::shared_ptr<std::atomic_bool>          cancel;
        std::shared_ptr<filestats::OperationPause> pause;
        int                                        generation        = 0;
        int                                        progress          = 0;
        bool                                       started           = false;
        bool                                       pausedByCollapse  = false;
        bool                                       autoStartConsumed = false;
        bool                                       rescanRequired    = false;
        QString                                    rescanMessage;
    };

    HexView                          *m_hexView             = nullptr;
    QScrollArea                      *m_scrollArea          = nullptr;
    QWidget                          *m_content             = nullptr;
    QWidget                          *m_fileSectionBody     = nullptr;
    QWidget                          *m_checksumSectionBody = nullptr;
    QWidget                          *m_stringsSectionBody  = nullptr;
    QSpacerItem                      *m_fileHeaderGap       = nullptr;
    QSpacerItem                      *m_checksumHeaderGap   = nullptr;
    QSpacerItem                      *m_stringsHeaderGap    = nullptr;
    QVector<QSpacerItem *>            m_interSectionGaps;
    filestats::SectionHeader         *m_fileHeader        = nullptr;
    filestats::SectionHeader         *m_checksumHeader    = nullptr;
    filestats::SectionHeader         *m_stringsHeader     = nullptr;
    filestats::SectionHeader         *m_stickyHeader      = nullptr;
    QLabel                           *m_nameValue         = nullptr;
    QLabel                           *m_locationValue     = nullptr;
    QLabel                           *m_sizeValue         = nullptr;
    filestats::SectionOperationStrip *m_checksumOperation = nullptr;
    QHash<QString, QLabel *>          m_checksumValues;
    QHash<QString, QCheckBox *>       m_checksumChecks;
    filestats::SectionOperationStrip *m_stringsOperation        = nullptr;
    QToolButton                      *m_stringOptionsButton     = nullptr;
    QAction                          *m_includeWhitespaceAction = nullptr;
    QAction                          *m_prefixHexOffsetAction   = nullptr;
    StepSpinBox                      *m_minStringLength         = nullptr;
    QComboBox                        *m_stringEncoding          = nullptr;
    QTreeWidget                      *m_stringsList             = nullptr;
    QWidget                          *m_stringsStatusRow        = nullptr;
    QLabel                           *m_stringsStatusLabel      = nullptr;
    QLabel                           *m_stringsProgressLabel    = nullptr;
    QPushButton                      *m_stringsNextButton       = nullptr;
    QPushButton                      *m_stringsAllButton        = nullptr;
    QPushButton                      *m_stringsExportButton     = nullptr;
    filestats::StringListFrame       *m_stringsListFrame        = nullptr;
    QWidget                          *m_stringsResizeHandle     = nullptr;
    ScanSectionState                  m_checksumState;
    ScanSectionState                  m_stringsState;
    ScanSectionState                  m_entropyState;
    QWidget                          *m_entropySectionBody  = nullptr;
    QSpacerItem                      *m_entropyHeaderGap    = nullptr;
    filestats::SectionHeader         *m_entropyHeader       = nullptr;
    filestats::SectionOperationStrip *m_entropyOperation    = nullptr;
    filestats::EntropyView           *m_entropyView          = nullptr;
    QLabel                           *m_entropyStatsLabel   = nullptr;
    QComboBox                        *m_entropyWindowCombo  = nullptr;
    QComboBox                        *m_entropyModeCombo    = nullptr;
    QComboBox                        *m_bigramScaleCombo    = nullptr;
    StepSpinBox                      *m_bigramStrideSpinner = nullptr;
    QLabel                           *m_entropyWindowLabel  = nullptr;
    QToolButton                      *m_entropyRotateButton = nullptr;
    int                               m_entropyWindowSize   = 256;
    EntropyMode                       m_entropyMode         = EntropyMode::Shannon;
    int                               m_bigramStride        = 1;
    QTimer                           *m_bigramRescanTimer   = nullptr;
    bool                              m_panelFullyOpened   = false;
    bool                              m_hasExpandedSection      = false;
    SectionId                         m_expandedSectionId       = SectionId::Properties;
    int                               m_preExpandHeight         = 0;
    int                               m_programmaticScrollDepth = 0;
    QVector<QPair<SectionId, bool>>   m_preExpandCollapsedStates;
    bool                              m_stringMoreAvailable         = false;
    bool                              m_sectionLayoutRefreshPending = false;
    QString                           m_stringsExportTempPath;
    QVector<PanelSection>             m_sections;
    QVector<SectionId>                m_sectionOrder;
    SectionId                         m_draggedSection            = SectionId::Properties;
    QWidget                          *m_dropIndicator             = nullptr;
    bool                              m_draggingSection           = false;
    bool                              m_dragSectionsCollapsed     = false;
    qulonglong                        m_stringNextOffset          = 0;
    qulonglong                        m_stringResultCount         = 0;
    bool                              m_stringsExportTempComplete = false;
    QPoint                            m_stringOptionsMenuClosePos{-1, -1};
};

class SidePanelHost : public QWidget
{
    Q_OBJECT
  public:
    explicit SidePanelHost(HexView *hexView, QWidget *parent = nullptr);

    bool isOpen() const;
    void toggle();
    void openSection(FilePropertiesPanel::SectionId section);
    void closePanel();
    void refreshPanel();
    void resetPanelForCurrentDocument();

  signals:
    void openChanged(bool open);

  protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

  private:
    void setExpanded(bool expanded);
    void setPaneWidth(int width);

    HexView                      *m_hexView      = nullptr;
    QWidget                      *m_resizeHandle = nullptr;
    QPropertyAnimation           *m_widthAnim    = nullptr;
    QPointer<FilePropertiesPanel> m_panel;
    bool                          m_resizing         = false;
    int                           m_paneWidth        = 400;
    int                           m_resizeStartWidth = 0;
    qreal                         m_resizeStartX     = 0.0;
};

#endif // FILESTATS_SIDEPANEL_H
