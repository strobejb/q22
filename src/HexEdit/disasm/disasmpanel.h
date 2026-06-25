#ifndef DISASM_DISASMPANEL_H
#define DISASM_DISASMPANEL_H

#include "disasm/codediscovery.h"
#include "filestats/sidepanel.h"

#include <QTextEdit>
#include <QWidget>
#include <capstone/capstone.h>

#include <utility>
#include <vector>

class DataTypeComboBox;
class HexView;
class MenuComboBox;
class QAction;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
namespace filestats { class TabbedContentFrame; }

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
    // Pushed in by DisassemblerPanelHost (which owns the long-lived cache,
    // since this panel itself is destroyed/recreated each time it's
    // closed/reopened) whenever CodeDiscoveryEngine's results change.
    void setDiscoveredFunctions(QList<DiscoveredFunction> functions);
    void setFunctionsScanInProgress(bool inProgress);
    // Periodic in-flight snapshots from CodeDiscoveryEngine::partialResults()
    // -- repopulates the Functions tab/combo with whatever's been found so
    // far without marking the scan finished (unlike setDiscoveredFunctions()).
    void setPartialDiscoveredFunctions(QList<DiscoveredFunction> functions);
    // CodeDiscoveryEngine::scanProgress(): drives the progress bar below the
    // toolbar combos while a scan is running.
    void setScanProgress(int discoveredCount, int worklistProcessed, int worklistTotal);

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
    // "Functions" tab: lists recursive-descent-discovered functions (see
    // HexEdit/disasm/codediscovery.h). Lives in this same panel (rather than
    // a separate side panel) so a row click can jump the Disassembly tab
    // directly via goToOffset(), with no cross-panel wiring needed.
    void rebuildFunctionsList();
    void onFunctionItemActivated(QTreeWidgetItem *item, int column);
    // Rebuilds the "jump to function" combo's item list: a permanent
    // "Entrypoint" entry (enabled only once known) followed by, if any were
    // found, a separator and the discovered functions -- called whenever
    // m_discoveredFunctions or the entry point changes.
    void populateFunctionsCombo();
    // Shared by setDiscoveredFunctions() and setPartialDiscoveredFunctions()
    // -- replaces m_discoveredFunctions and refreshes everything driven by
    // it, without touching m_functionsScanInProgress (the caller decides
    // that, since a partial update mid-scan must leave it alone).
    void applyDiscoveredFunctions(QList<DiscoveredFunction> functions);
    // The footer status label is shared by both tabs (mirroring structview's
    // single status label) -- refreshes its text from whichever page is
    // currently showing, so a background disassemble()/rebuildFunctionsList()
    // while the *other* tab is active doesn't overwrite text the user isn't
    // looking at with text that doesn't match what they are looking at.
    void updateStatusLabelForCurrentTab();

    HexView        *m_hv               = nullptr;
    filestats::TabbedContentFrame *m_tabFrame   = nullptr;
    QStackedWidget *m_pageStack         = nullptr;
    QTreeWidget    *m_functionsList     = nullptr;
    QProgressBar   *m_scanProgressBar   = nullptr;
    MenuComboBox   *m_archCombo        = nullptr;
    DataTypeComboBox *m_functionsCombo = nullptr;
    QLineEdit      *m_offsetEdit       = nullptr;
    QAction        *m_pinAction        = nullptr;
    QPlainTextEdit *m_view             = nullptr;
    QLabel         *m_statusLabel      = nullptr;
    QString         m_disasmStatusText;
    QList<DiscoveredFunction> m_discoveredFunctions;
    bool            m_functionsScanInProgress = false;
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

    // The host outlives the panel widget (which is destroyed/recreated each
    // close/reopen), so it -- not HexView, not the panel -- is where
    // CodeDiscoveryEngine's results actually live; forwarded into the panel
    // live if it currently exists, and replayed into it on (re)creation.
    void setDiscoveredFunctions(QList<DiscoveredFunction> functions);
    void setFunctionsScanInProgress(bool inProgress);
    void setPartialDiscoveredFunctions(QList<DiscoveredFunction> functions);
    void setScanProgress(int discoveredCount, int worklistProcessed, int worklistTotal);

protected:
    QWidget *createPanelWidget() override;

private:
    HexView *m_hv = nullptr;
    QList<DiscoveredFunction> m_discoveredFunctions;
    bool m_functionsScanInProgress = false;
    int m_scanProgressDiscovered = 0;
    int m_scanProgressProcessed  = 0;
    int m_scanProgressTotal      = 0;
};

#endif // DISASM_DISASMPANEL_H
