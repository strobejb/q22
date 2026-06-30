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
class QVBoxLayout;
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
        DataInterpreter,
        Checksums,
        Strings,
        Entropy
    };
    Q_ENUM(SectionId)

    enum class EntropyMode  { Shannon, Bigram, ByteClass, Hilbert, Gilbert };

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
    void           applyEntropyResults(int generation, QVector<float> data, qulonglong scopeSize, int windowSize, qulonglong scopeStart);
    void           applyBigramResults(int generation, QVector<quint64> counts, qulonglong fileSize);
    void           applyByteClassResults(int generation, QVector<float> data, qulonglong scopeSize, int windowSize, qulonglong scopeStart);
    void           updateZoomButton();
    void           applyHilbertResults(int generation, QVector<quint8> bytes, qulonglong scopeSize, int sampleCount, int gridSide, qulonglong scopeStart);
    void           applyGilbertResults(int generation, QVector<quint8> bytes, qulonglong scopeSize, int sampleCount, qulonglong scopeStart);
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
    void           updateDataInterpreter();
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
    QWidget                          *m_dataInterpreterSectionBody = nullptr;
    QWidget                          *m_checksumSectionBody = nullptr;
    QWidget                          *m_stringsSectionBody  = nullptr;
    QSpacerItem                      *m_fileHeaderGap       = nullptr;
    QSpacerItem                      *m_dataInterpreterHeaderGap = nullptr;
    QSpacerItem                      *m_checksumHeaderGap   = nullptr;
    QSpacerItem                      *m_stringsHeaderGap    = nullptr;
    QVector<QSpacerItem *>            m_interSectionGaps;
    filestats::SectionHeader         *m_fileHeader        = nullptr;
    filestats::SectionHeader         *m_dataInterpreterHeader = nullptr;
    filestats::SectionHeader         *m_checksumHeader    = nullptr;
    filestats::SectionHeader         *m_stringsHeader     = nullptr;
    filestats::SectionHeader         *m_stickyHeader      = nullptr;
    QLabel                           *m_nameValue         = nullptr;
    QLabel                           *m_locationValue     = nullptr;
    QLabel                           *m_sizeValue         = nullptr;
    QHash<QString, QWidget *>         m_dataInterpreterIntegerRows;
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
    QLabel                           *m_hilbertGridLabel    = nullptr;
    QComboBox                        *m_hilbertGridCombo    = nullptr;
    QToolButton                      *m_hilbertColorButton  = nullptr;
    QToolButton                      *m_hilbertZoomButton   = nullptr;
    QMenu                            *m_hilbertColorMenu         = nullptr;
    QMenu                            *m_byteClassSchemeMenu      = nullptr;
    QAction                          *m_colorByteClassAction     = nullptr;
    QAction                          *m_colorMagnitudeAction     = nullptr;
    QAction                          *m_colorEntropyAction       = nullptr;
    QAction                          *m_colorDetailAction        = nullptr;
    QAction                          *m_bcSchemeSemanticAction   = nullptr;
    QAction                          *m_bcSchemeAsciiAction      = nullptr;
    QAction                          *m_bcSchemeBitDensAction    = nullptr;
    QAction                          *m_bcSchemeNibbleAction     = nullptr;
    QToolButton                      *m_entropyRotateButton = nullptr;
    void   buildPropertiesSection(QWidget *parent, QVBoxLayout *contentLayout);
    void   buildDataInterpreterSection(QWidget *parent, QVBoxLayout *contentLayout);
    void   buildChecksumSection(QWidget *parent, QVBoxLayout *contentLayout);
    void   buildStringsSection(QWidget *parent, QVBoxLayout *contentLayout);
    void   buildEntropySection(QWidget *parent, QVBoxLayout *contentLayout);
    void   triggerParamRescan(const QString &message);
    qint64 m_entropyScanStartMs = 0;
    qint64 m_entropyLastScanMs  = -1;
    int                               m_entropyWindowSize   = 256;
    int                               m_hilbertGridSide     = 256;
    qulonglong                        m_entropyScopeStart   = 0;
    qulonglong                        m_entropyScopeLength  = 0;
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

class SidePanelSlot;

// Generic slide-in panel host. Handles the animation, resize grip, and panel
// widget lifecycle. Subclasses implement createPanelWidget() and optionally
// override onPanelCreated() and onFullyOpenedChanged().
class SidePanelHostBase : public QWidget
{
    Q_OBJECT
  public:
    // gripOnLeft: true  → grip on left edge (right-side panel; drag left to expand)
    //             false → grip on right edge (left-side panel; drag right to expand)
    explicit SidePanelHostBase(int defaultWidth, int minWidth, int maxWidth,
                               bool gripOnLeft, QWidget *parent = nullptr);

    bool isOpen() const;
    int  paneWidth() const { return m_paneWidth; }
    virtual void toggle();
    void closePanel();

  signals:
    void openChanged(bool open);

  protected:
    virtual QWidget *createPanelWidget()                = 0;
    virtual void    onPanelCreated(QWidget * /*panel*/) {}
    virtual void    onFullyOpenedChanged(bool /*open*/) {}

    void resizeEvent(QResizeEvent *) override;

    void     openPanel();
    QWidget *panelWidget() const;
    void     setExpanded(bool expanded);
    void     setPaneWidth(int width);

    // Slot-based slide animation (used when installed in a SidePanelSlot)
    void slideIn(std::function<void()> onDone = {});
    void slideOut(std::function<void()> onDone = {});
    void notifyFullyOpened(bool open);
    void destroyPanel();
    void positionPanel();

  private:
    bool eventFilter(QObject *obj, QEvent *event) override;

    SidePanelSlot      *m_slot             = nullptr;
    QWidget            *m_resizeHandle     = nullptr;
    QPropertyAnimation *m_widthAnim        = nullptr;
    QPropertyAnimation *m_slideAnim        = nullptr;
    QPointer<QWidget>   m_panel;
    bool                m_resizing         = false;
    bool                m_gripOnLeft       = true;
    int                 m_minWidth         = 0;
    int                 m_maxWidth         = 0;
    int                 m_paneWidth        = 0;
    int                 m_resizeStartWidth = 0;
    qreal               m_resizeStartX     = 0.0;

    friend class SidePanelSlot;
};

// Shared container that holds multiple SidePanelHostBase instances as
// absolutely-positioned, overlapping children. The slot owns the single
// layout-level width animation; each host uses pos-only slide animation
// within the fixed-width slot — so panel content never reflowss during
// transitions, and switching panels produces an overlay rather than
// an extension of the total panel area.
class SidePanelSlot : public QWidget
{
    Q_OBJECT
public:
    explicit SidePanelSlot(QWidget *parent = nullptr);
    void addHost(SidePanelHostBase *host);

    // Called by SidePanelHostBase
    void hostOpening(SidePanelHostBase *host);
    void hostClosing(SidePanelHostBase *host);
    void hostPaneWidthChanged(SidePanelHostBase *host, int newWidth);

protected:
    void resizeEvent(QResizeEvent *) override;

private:
    void beginExpand(int toWidth);
    void beginCollapse();

    QList<SidePanelHostBase *> m_hosts;
    SidePanelHostBase         *m_activeHost = nullptr;
    QPropertyAnimation        *m_slotAnim   = nullptr;
};

class SidePanelHost : public SidePanelHostBase
{
    Q_OBJECT
  public:
    explicit SidePanelHost(HexView *hexView, QWidget *parent = nullptr);

    void toggle() override;
    void openSection(FilePropertiesPanel::SectionId section);
    void refreshPanel();
    void resetPanelForCurrentDocument();

  protected:
    QWidget *createPanelWidget() override;
    void     onPanelCreated(QWidget *panel) override;
    void     onFullyOpenedChanged(bool open) override;

  private:
    HexView *m_hexView = nullptr;
};

#endif // FILESTATS_SIDEPANEL_H
