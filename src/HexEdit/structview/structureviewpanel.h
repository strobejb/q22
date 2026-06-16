#ifndef STRUCTVIEW_STRUCTUREVIEWPANEL_H
#define STRUCTVIEW_STRUCTUREVIEWPANEL_H

#include "filestats/sidepanel.h"

#include <QWidget>

class HexView;
class QLabel;
class QLineEdit;
class QTreeView;
class MenuComboBox;
class StructureDefinitionManager;
class StructureTreeModel;
struct TypeDecl;

class StructureViewPanel : public QWidget
{
    Q_OBJECT
public:
    explicit StructureViewPanel(HexView *hv, QWidget *parent = nullptr);

signals:
    void closeRequested();

public slots:
    void refresh();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void buildUi();
    void updateDefinitionsUi();
    void updateOffsetDisplay();
    QString displayNameForTypeDecl(TypeDecl *decl) const;

    HexView                    *m_hv = nullptr;
    StructureDefinitionManager *m_definitions = nullptr;
    StructureTreeModel         *m_model = nullptr;
    MenuComboBox               *m_rootCombo = nullptr;
    QLineEdit                  *m_offsetEdit = nullptr;
    QTreeView                  *m_tree = nullptr;
    QLabel                     *m_statusLabel = nullptr;
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
