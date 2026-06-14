#ifndef DISASM_DISASMPANEL_H
#define DISASM_DISASMPANEL_H

#include "filestats/sidepanel.h"

#include <QWidget>
#include <capstone/capstone.h>

class HexView;
class MenuComboBox;
class QCheckBox;
class QLabel;
class QPlainTextEdit;

class DisassemblerPanel : public QWidget
{
    Q_OBJECT
public:
    explicit DisassemblerPanel(HexView *hv, QWidget *parent = nullptr);
    ~DisassemblerPanel() override;

signals:
    void closeRequested();

public slots:
    void refresh();

private:
    void buildUi();
    void disassemble();
    void highlightCurrentLine();
    void openCapstone();
    void closeCapstone();

    HexView        *m_hv           = nullptr;
    MenuComboBox   *m_archCombo    = nullptr;
    QCheckBox      *m_followCursor = nullptr;
    QPlainTextEdit *m_view         = nullptr;
    QLabel         *m_statusLabel  = nullptr;

    cs_arch m_csArch   = CS_ARCH_X86;
    cs_mode m_csMode   = (cs_mode)CS_MODE_64;
    csh     m_csHandle = 0;
    bool    m_csOpen   = false;
};

class DisassemblerPanelHost : public SidePanelHostBase
{
    Q_OBJECT
public:
    explicit DisassemblerPanelHost(HexView *hv, QWidget *parent = nullptr);

protected:
    QWidget *createPanelWidget() override;

private:
    HexView *m_hv = nullptr;
};

#endif // DISASM_DISASMPANEL_H
