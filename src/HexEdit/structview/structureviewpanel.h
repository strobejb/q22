#ifndef STRUCTVIEW_STRUCTUREVIEWPANEL_H
#define STRUCTVIEW_STRUCTUREVIEWPANEL_H

#include "HexView/seqbase.h"
#include "filestats/sidepanel.h"
#include "structview/structuredisplayoptions.h"

#include <QList>
#include <QModelIndex>
#include <QPoint>
#include <QWidget>

class HexView;
class QAction;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QStackedWidget;
class QToolButton;
class QTreeView;
class MenuComboBox;
class StructureContentFrame;
class StructureDefinitionManager;
class StructureTreeModel;
struct ExportedStructureType;
struct StructureRow;
struct TypeDecl;

class StructureViewPanel : public QWidget
{
    Q_OBJECT
public:
    explicit StructureViewPanel(HexView *hv, QWidget *parent = nullptr);
    ~StructureViewPanel() override;

signals:
    void closeRequested();

public slots:
    void refresh();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override;

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
    void showHeaderContextMenu(int column, const QPoint &globalPos);
    void showOptionsContextMenu(int column, const QPoint &globalPos, bool includeAllColumns, const QModelIndex &rowIndex = QModelIndex());
    void expandSubtree(const QModelIndex &index);
    void collapseSubtree(const QModelIndex &index);
    void showGridPage();
    void showSourcePage(TypeDecl *typeDecl = nullptr);
    void showLogPage();
    void updateContentFramePage();
    void locateIndexInSource(const QModelIndex &index);
    bool loadSourceFile(const QString &path, int line);
    StructureRow *sourceRowForIndex(const QModelIndex &index) const;
    StructureDisplayOptions displayOptions() const;
    void applyDisplayOptions();
    void setUseDefinedTypeNames(bool enabled);
    void setUseHexadecimalValues(bool enabled);
    void setUseHexadecimalOffsets(bool enabled);
    void setUseRelativeOffsets(bool enabled);
    void updateHexViewSelection(const QModelIndex &current);
    void clearHexViewOverlay();
    void setHexViewSelectionFromStructure(size_w start, size_w end);
    bool explicitRootOffset(TypeDecl *rootType, uint64_t *offset) const;
    void selectAssociatedRootType(const QList<ExportedStructureType> &exportedTypes);
    TypeDecl *selectedRootType() const;
    QString displayNameForTypeDecl(TypeDecl *decl) const;

    HexView                    *m_hv = nullptr;
    StructureDefinitionManager *m_definitions = nullptr;
    StructureTreeModel         *m_model = nullptr;
    MenuComboBox               *m_rootCombo = nullptr;
    QLineEdit                  *m_offsetEdit = nullptr;
    QAction                    *m_pinAction = nullptr;
    StructureContentFrame      *m_contentFrame = nullptr;
    QToolButton                *m_logButton = nullptr;
    QStackedWidget             *m_viewStack = nullptr;
    QTreeView                  *m_tree = nullptr;
    QPlainTextEdit             *m_sourceView = nullptr;
    QPlainTextEdit             *m_logView = nullptr;
    QLabel                     *m_statusLabel = nullptr;
    int                         m_treeItemLeftPad = 6;
    bool                        m_pinned = false;
    bool                        m_useDefinedTypeNames = false;
    bool                        m_useHexadecimalValues = false;
    bool                        m_useHexadecimalOffsets = true;
    bool                        m_useRelativeOffsets = true;
    bool                        m_updatingHexViewFromStructure = false;
    bool                        m_rebuildingRows = false;
    uint64_t                    m_pinnedOffset = 0;
};

class StructureViewPanelHost : public SidePanelHostBase
{
    Q_OBJECT
public:
    explicit StructureViewPanelHost(HexView *hv, QWidget *parent = nullptr);

protected:
    QWidget *createPanelWidget() override;

private:
    HexView *m_hv = nullptr;
};

#endif // STRUCTVIEW_STRUCTUREVIEWPANEL_H
