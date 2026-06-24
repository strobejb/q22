#ifndef DISASM_DISASMPANEL_H
#define DISASM_DISASMPANEL_H

#include "filestats/sidepanel.h"

#include <QTextEdit>
#include <QWidget>
#include <capstone/capstone.h>

#include <utility>
#include <vector>

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

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi();
    void disassemble();
    void highlightCurrentLine();
    void updateOffsetDisplay();
    void setPinned(bool pinned);
    void openCapstone();
    void closeCapstone();
    // Snaps the view's text selection to whole instruction lines (a click
    // becomes a one-line selection, a drag expands to cover every touched
    // instruction) and mirrors the resulting byte range onto the hex view.
    void syncSelectionWithHexView();
    // Reverse direction: highlights whichever already-displayed instruction
    // line(s) the hex view's current selection falls within. Never triggers
    // a re-disassemble -- the listing's start offset only changes via an
    // explicit jump (goToOffset) or a line click, never passive navigation.
    void syncDisasmHighlightFromHexView();
    void applyLineSelection(int firstLine, int lastLine);
    // Merges the current line-selection highlight(s) with the hover-underline
    // (if any) into one ExtraSelection list -- the two are tracked separately
    // but QPlainTextEdit only accepts a single combined list per call.
    void updateExtraSelections();

    HexView        *m_hv               = nullptr;
    MenuComboBox   *m_archCombo        = nullptr;
    QToolButton    *m_entryPointButton = nullptr;
    QLineEdit      *m_offsetEdit       = nullptr;
    QAction        *m_pinAction        = nullptr;
    QPlainTextEdit *m_view             = nullptr;
    QLabel         *m_statusLabel      = nullptr;
    bool            m_pinned           = false;
    // Per-line (one per disassembled instruction) [start, end) file offsets,
    // rebuilt every disassemble() call; index == text block number.
    std::vector<std::pair<uint64_t, uint64_t>> m_instructionRanges;
    bool            m_syncingDisasmSelection   = false;
    bool            m_updatingHexViewFromDisasm = false;

    // A JMP/Jcc/CALL operand that resolves to a static in-file target address
    // -- [startPos, endPos) are QTextDocument-global character positions,
    // rebuilt every disassemble() call alongside m_instructionRanges.
    struct BranchSpan { int startPos; int endPos; uint64_t targetOffset; };
    std::vector<BranchSpan> m_branchSpans;
    int             m_hoveredSpanIndex  = -1;
    // Per-line [startPos, endPos) of just the address-column text, index ==
    // text block number -- rebuilt every disassemble() call alongside
    // m_instructionRanges, used to highlight only the address (not the whole
    // line) when previewing a hovered branch's target.
    std::vector<std::pair<int, int>> m_addressSpans;
    // Line index (into m_instructionRanges) that m_hoveredSpanIndex's target
    // address falls within, or -1 if the hovered branch's target isn't among
    // the currently-displayed instructions -- previewed with a temporary
    // highlight while hovering, separate from m_lineHighlights' persistent
    // selection highlight.
    int             m_hoveredTargetLine = -1;
    QList<QTextEdit::ExtraSelection> m_lineHighlights;

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
