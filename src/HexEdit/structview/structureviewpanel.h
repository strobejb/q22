#ifndef STRUCTVIEW_STRUCTUREVIEWPANEL_H
#define STRUCTVIEW_STRUCTUREVIEWPANEL_H

#include "HexView/seqbase.h"
#include "filestats/sidepanel.h"
#include "structview/structuredisplayoptions.h"

#include <QList>
#include <QModelIndex>
#include <QPoint>
#include <QWidget>

#include <memory>

class HexView;
class QAction;
class QDialog;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QStackedWidget;
class QToolButton;
class QTreeView;
class QVBoxLayout;
class MenuComboBox;
class SourceViewButton;
class StructureContentFrame;
class StructureDefinitionManager;
class StructureTreeModel;
class StructureRenderEngine;
class sequence;
struct ExportedStructureType;
struct StructureMagicSignature;
struct StructureRow;
struct TypeDecl;

namespace filestats
{
class ActionBanner;
}

// Detects which exported Strata type matches hv's currently open file (by
// assoc extension, then magic signature) and locates that type's first
// [entrypoint]-tagged field. Self-contained -- does not require a
// StructureViewPanel to exist, so the disassembler's entry point stays
// correct even when the structure view panel is closed or was never opened
// for this file (HexView's cached entry point otherwise only gets updated as
// a side effect of that panel rebuilding).
bool detectStructureEntryPoint(HexView *hv, uint64_t *fileOffset);

class StructureViewPanel : public QWidget
{
    Q_OBJECT
public:
    explicit StructureViewPanel(HexView *hv, QWidget *parent = nullptr);
    ~StructureViewPanel() override;

signals:
    void closeRequested();
    void openDisassemblerRequested(uint64_t offset, uint64_t length,
                                   const QString &architecture, const QString &name);
    void selectionIdentityChanged(const QString &name, uint64_t offset);

public slots:
    void refresh();
    // Re-selects the row matching (name, offset) once rows are available.
    // Used by the host to restore the selection after the panel is recreated
    // (it's destroyed/rebuilt on close, like the other side panels), so e.g.
    // switching to the disassembler and back doesn't drop the selected row.
    void restoreSelection(const QString &name, uint64_t offset);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi();
    void updateTreeSelectionPalette();
    void updateDefinitionsUi();
    void updateOffsetDisplay();
    void updatePinAction();
    void setPinned(bool pinned);
    void rebuildRows();
    void applyInitialExpansion();
    void showGridContextMenu(const QPoint &pos);
    void showRootComboContextMenu(const QPoint &pos);
    void showHeaderContextMenu(int column, const QPoint &globalPos);
    void showOptionsContextMenu(int column, const QPoint &globalPos, bool includeAllColumns, const QModelIndex &rowIndex = QModelIndex());
    void focusSubtree(const QModelIndex &index);
    void expandSubtree(const QModelIndex &index);
    void showGridPage();
    void showSourcePage(TypeDecl *typeDecl = nullptr);
    void showLogPage();
    void updateContentFramePage();
    void locateIndexInSource(const QModelIndex &index);
    bool locateLogDiagnosticAt(const QPoint &viewportPos);
    bool logDiagnosticAt(const QPoint &viewportPos, QString *path, int *lineNo) const;
    bool loadSourceFile(const QString &path, int line, int selStart = -1, int selEnd = -1);
    bool saveSourceDefinition();
    QString sourceSaveTargetPath() const;
    bool confirmOverwriteSourceFile(const QString &path) const;
    StructureRow *sourceRowForIndex(const QModelIndex &index) const;
    void setStatusLabelError(bool error);
    StructureDisplayOptions displayOptions() const;
    void applyDisplayOptions();
    void setUseDefinedTypeNames(bool enabled);
    void setUseHexadecimalValues(bool enabled);
    void setUseHexadecimalOffsets(bool enabled);
    void setUseRelativeOffsets(bool enabled);
    void updateHexViewSelection(const QModelIndex &current);
    StructureRow *openAsRowForIndex(const QModelIndex &index) const;
    TypeDecl *resolvedOpenAsRootType(const StructureRow *row) const;
    TypeDecl *resolvedOpenAsRootType(const StructureRow *row, sequence *source, uint64_t byteLength) const;
    void openIndexAsStructure(const QModelIndex &index);
    bool createTransformedOpenAsSource(const StructureRow *row,
                                       QString *tempPath,
                                       std::shared_ptr<sequence> *source,
                                       uint64_t *byteLength,
                                       QString *errorMessage) const;
    uint64_t currentRootBaseOffset(TypeDecl *rootType) const;
    void ensureSourceStackRootFrame(TypeDecl *rootType);
    void updateSourceStackWidget();
    void setSourceStackActiveHighlightVisible(bool visible);
    void navigateToSourceFrame(int index);
    void exitSourceStack();
    void clearSourceStack();
    void removeSourceFrameAt(int index);
    void applyPendingRestore();
    QModelIndex findIndexByIdentity(const QModelIndex &parent, const QString &name, uint64_t offset) const;
    void clearHexViewOverlay();
    void setHexViewSelectionFromStructure(size_w start, size_w end);
    bool explicitRootOffset(TypeDecl *rootType, uint64_t *offset) const;
    bool magicSignatureMatches(const StructureMagicSignature &signature) const;
    int associatedRootTypeIndex(const QList<ExportedStructureType> &exportedTypes) const;
    bool selectAssociatedRootType(const QList<ExportedStructureType> &exportedTypes);
    QList<ExportedStructureType> sortedExportedTypes(const QList<ExportedStructureType> &exportedTypes) const;
    int rootComboIndexForType(TypeDecl *typeDecl) const;
    void refreshForCurrentFileAssociation();
    TypeDecl *selectedRootType() const;
    QString displayNameForTypeDecl(TypeDecl *decl) const;
    bool selectedRootHasLoadError(QString *filePath, QString *message) const;
    void updateLoadErrorView();

    void repositionSourceViewButtons();

    HexView                    *m_hv = nullptr;
    StructureDefinitionManager *m_definitions = nullptr;
    StructureTreeModel         *m_model = nullptr;
    filestats::ActionBanner    *m_reloadBanner = nullptr;
    MenuComboBox               *m_rootCombo = nullptr;
    QLineEdit                  *m_offsetEdit = nullptr;
    QAction                    *m_pinAction = nullptr;
    StructureContentFrame      *m_contentFrame = nullptr;
    QToolButton                *m_logButton = nullptr;
    QStackedWidget             *m_viewStack = nullptr;
    QTreeView                  *m_tree = nullptr;
    QPlainTextEdit             *m_sourceView = nullptr;
    QPlainTextEdit             *m_logView = nullptr;
    QLabel                     *m_loadErrorView = nullptr;
    QLabel                     *m_statusLabel = nullptr;
    QWidget                    *m_sourceStackWidget = nullptr;
    QVBoxLayout                *m_sourceStackLayout = nullptr;
    QToolButton                *m_sourceStackCloseButton = nullptr;
    SourceViewButton           *m_sourceSaveButton = nullptr;
    SourceViewButton           *m_sourceHelpButton = nullptr;
    QString                     m_currentSourceFilePath;
    QDialog                    *m_helpWindow = nullptr;
    int                         m_treeItemLeftPad = 6;
    bool                        m_pinned = false;
    bool                        m_useDefinedTypeNames = false;
    bool                        m_useHexadecimalValues = false;
    bool                        m_useHexadecimalOffsets = true;
    bool                        m_useRelativeOffsets = true;
    bool                        m_sortRootComboByDetail = false;
    bool                        m_preserveRootComboSelectionOnce = false;
    bool                        m_updatingHexViewFromStructure = false;
    bool                        m_rebuildingRows = false;
    bool                        m_openAsPinnedBase = false;
    uint64_t                    m_renderGeneration = 0;
    std::shared_ptr<StructureRenderEngine> m_deferredSemanticEngine;
    uint64_t                    m_pinnedOffset = 0;
    QString                     m_pendingRestoreName;
    uint64_t                    m_pendingRestoreOffset = 0;
    bool                        m_hasPendingRestore = false;

    struct SourceFrame
    {
        TypeDecl *rootType = nullptr;
        QString rootDisplayName;
        QString sourceName;
        uint64_t baseOffset = 0;
        uint64_t byteLength = 0;
        QString returnRowName;
        uint64_t returnRowOffset = 0;
        uint64_t returnRowLength = 0;
        bool hasReturnRow = false;
        bool sliceRoot = false;
        QString transform;
        QString tempFilePath;
        std::shared_ptr<sequence> transformedSource;
    };
    QList<SourceFrame>          m_sourceStack;
    int                         m_activeSourceFrame = -1;
};

class StructureViewPanelHost : public SidePanelHostBase
{
    Q_OBJECT
public:
    explicit StructureViewPanelHost(HexView *hv, QWidget *parent = nullptr);

signals:
    void openDisassemblerRequested(uint64_t offset, uint64_t length,
                                   const QString &architecture, const QString &name);

protected:
    QWidget *createPanelWidget() override;
    void onPaneWidthCommitted(int width) override;

private:
    HexView *m_hv = nullptr;
    QString  m_pendingSelectionName;
    uint64_t m_pendingSelectionOffset = 0;
    bool     m_hasPendingSelection = false;
};

#endif // STRUCTVIEW_STRUCTUREVIEWPANEL_H
