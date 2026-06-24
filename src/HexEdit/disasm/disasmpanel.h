#ifndef DISASM_DISASMPANEL_H
#define DISASM_DISASMPANEL_H

#include "filestats/sidepanel.h"

#include <QWidget>
#include <capstone/capstone.h>

class HexView;
class MenuComboBox;
class QAction;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QToolButton;

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
    void goToOffset(uint64_t offset);

private:
    void buildUi();
    void disassemble();
    void highlightCurrentLine();
    void updateOffsetDisplay();
    void setPinned(bool pinned);
    void openCapstone();
    void closeCapstone();

    HexView        *m_hv               = nullptr;
    MenuComboBox   *m_archCombo        = nullptr;
    QToolButton    *m_entryPointButton = nullptr;
    QLineEdit      *m_offsetEdit       = nullptr;
    QAction        *m_pinAction        = nullptr;
    QPlainTextEdit *m_view             = nullptr;
    QLabel         *m_statusLabel      = nullptr;
    bool            m_pinned           = false;

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

    // Ensures the panel is open (without closing it if already open, unlike
    // toggle()) and jumps it to offset, bypassing "pin" since this is an
    // explicit navigation request rather than incidental cursor movement.
    void openAtOffset(uint64_t offset);

protected:
    QWidget *createPanelWidget() override;

private:
    HexView *m_hv = nullptr;
};

#endif // DISASM_DISASMPANEL_H
