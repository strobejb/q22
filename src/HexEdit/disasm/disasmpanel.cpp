#include "disasm/disasmpanel.h"

#include "HexView/hexview.h"
#include "combos/menucombobox.h"
#include "filestats/widgets.h"
#include "theme.h"

#include <QApplication>
#include <QComboBox>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QTextEdit>
#include <QToolButton>
#include <QStyle>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QVBoxLayout>


// ── arch table ────────────────────────────────────────────────────────────────

struct ArchEntry
{
    const char *label;
    cs_arch     arch;
    cs_mode     mode;
};

QColor blend(const QColor &a, const QColor &b, double r = 0.5);

static const ArchEntry kArchEntries[] = {
    { "x86 64-bit",  CS_ARCH_X86,     CS_MODE_64    },
    { "x86 32-bit",  CS_ARCH_X86,     CS_MODE_32    },
    { "ARM",         CS_ARCH_ARM,     CS_MODE_ARM   },
    { "ARM Thumb",   CS_ARCH_ARM,     CS_MODE_THUMB },
    { "AArch64",     CS_ARCH_ARM64,   (cs_mode)0    },
};
static constexpr int kArchCount = (int)(sizeof(kArchEntries) / sizeof(kArchEntries[0]));

static constexpr bool kHighlightCurrentLine = false; // tint the line under the cursor

static QFont disassemblyViewFont(const QFont &hexViewFont)
{
    QFont font = hexViewFont;
    const QFont defaultFont = QApplication::font();

    if (font.pointSizeF() > 0)
    {
        const qreal defaultSize = defaultFont.pointSizeF() > 0 ? defaultFont.pointSizeF() : font.pointSizeF();
        font.setPointSizeF(qMax(defaultSize, font.pointSizeF() - 2.0));
    }
    else if (font.pixelSize() > 0)
    {
        const int defaultSize = defaultFont.pixelSize() > 0 ? defaultFont.pixelSize() : QFontMetrics(defaultFont).height();
        font.setPixelSize(qMax(defaultSize, font.pixelSize() - 2));
    }

    return font;
}


// ── DisassemblerPanel ─────────────────────────────────────────────────────────

DisassemblerPanel::DisassemblerPanel(HexView *hv, QWidget *parent)
    : QWidget(parent)
    , m_hv(hv)
{
    buildUi();
    openCapstone();
    disassemble();
}

DisassemblerPanel::~DisassemblerPanel()
{
    closeCapstone();
}

void DisassemblerPanel::refresh()
{
    disassemble();
}

void DisassemblerPanel::buildUi()
{
    auto *rootLay = new QVBoxLayout(this);
    rootLay->setContentsMargins(0, filestats::kContentMargin, 0, 0);
    rootLay->setSpacing(0);

    const int scrollBarW = QApplication::style()->pixelMetric(QStyle::PM_ScrollBarExtent);

    // Section header – indented on the right by scrollBarW to match the filestats panel,
    // where the header sits inside a scroll-viewport that is scrollBarW narrower than the panel.
    auto *header = new filestats::SectionHeader(tr("Disassembler"), this);
    header->setCloseMode(true);
    header->setExpandCallback([this]() { emit closeRequested(); });
    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, scrollBarW, 0);
    headerRow->setSpacing(0);
    headerRow->addWidget(header);
    rootLay->addLayout(headerRow);

    // Content area
    auto *content = new QWidget(this);
    auto *contentLay = new QVBoxLayout(content);
    // Right margin = kContentMargin + 7 to compensate for the 7-px resize grip
    // on the left (kGripWidth in resizegrip.cpp), so content is visually centred.
    contentLay->setContentsMargins(filestats::kContentMargin, filestats::kContentMargin,
                                   filestats::kContentMargin + 7, 8);
    contentLay->setSpacing(4);

    // Options row
    auto *optLay = new QHBoxLayout;
    optLay->setContentsMargins(0, 0, 0, 0);
    optLay->setSpacing(6);

    m_archCombo = new MenuComboBox(content);
    m_archCombo->setToolTip(tr("Architecture"));
    for (int i = 0; i < kArchCount; ++i)
        m_archCombo->addItem(tr(kArchEntries[i].label));
    m_archCombo->setCurrentIndex(0);
    m_archCombo->setLeadingIcon(
        recoloredIcon(QStringLiteral("actions/chip"),
                      filestats::subduedTextColor(palette()), 16));
    const int comboH = qMax(24, static_cast<QComboBox *>(m_archCombo)->sizeHint().height() - 4);
    m_archCombo->setFixedHeight(comboH);
    m_archCombo->setMaximumWidth(qRound(static_cast<QComboBox *>(m_archCombo)->sizeHint().width() * 1.7));
    m_archCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // Read-only offset display with trailing pin toggle.
    m_offsetEdit = new QLineEdit(content);
    m_offsetEdit->setReadOnly(true);
    m_offsetEdit->setFixedHeight(comboH);
    m_offsetEdit->setPlaceholderText(tr("Offset"));
    const auto existingBtns = m_offsetEdit->findChildren<QToolButton *>();
    m_pinAction = m_offsetEdit->addAction(
        recoloredIcon(QStringLiteral("actions/pin1"),
                      palette().color(QPalette::WindowText), 16),
        QLineEdit::TrailingPosition);
    m_pinAction->setToolTip(tr("Pin offset"));
    for (auto *btn : m_offsetEdit->findChildren<QToolButton *>())
        if (!existingBtns.contains(btn))
            btn->setCursor(Qt::PointingHandCursor);
    // Width: 8 hex digits + generous left/right padding + trailing icon room
    const int offsetW = m_offsetEdit->fontMetrics().horizontalAdvance(QStringLiteral("00000000")) + 40
                        + 16 + 8; // icon + Qt's action button right margin
    m_offsetEdit->setFixedWidth(offsetW);

    optLay->addWidget(m_archCombo, 1);
    optLay->addSpacing(4);
    optLay->addWidget(m_offsetEdit);
    optLay->addStretch(1);
    contentLay->addLayout(optLay);
    contentLay->addSpacing(4);

    // Instruction view
    m_view = new QPlainTextEdit(content);
    m_view->setReadOnly(true);
    m_view->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->document()->setDocumentMargin(6.0);
    m_view->setFont(disassemblyViewFont(m_hv->font()));

    // Match the hex view's background and selection colours.
    // Background goes through QPalette::Base (stylesheet doesn't set background-color).
    // Selection must go through the stylesheet: when a QSS is active, Qt's style engine
    // renders text selection itself and ignores QPalette::Highlight/HighlightedText.
    const QColor bgColor     = QColor(m_hv->getHexColour(HVC_BACKGROUND));
    const QColor selBgColor  = QColor(m_hv->getHexColour(HVC_SELECTION));
    const QColor selFgColor  = QColor(m_hv->getHexColour(HVC_SELTEXT));

    QPalette vp = m_view->palette();
    vp.setColor(QPalette::Base, bgColor);
    m_view->setPalette(vp);

    m_view->setStyleSheet(
        QStringLiteral("QPlainTextEdit { border: 1px solid palette(mid); border-radius: 6px;"
                       " padding: 0;"
                       " selection-background-color: %1; selection-color: %2; }")
        .arg(filestats::cssColor(selBgColor), filestats::cssColor(selFgColor)));

    contentLay->addWidget(m_view, 1);

    // Status row
    m_statusLabel = new QLabel(content);
    m_statusLabel->setTextFormat(Qt::PlainText);
    contentLay->addWidget(m_statusLabel);

    rootLay->addWidget(content, 1);

    // Connections
    if constexpr (kHighlightCurrentLine)
        connect(m_view, &QPlainTextEdit::cursorPositionChanged,
                this, &DisassemblerPanel::highlightCurrentLine);

    connect(m_archCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { openCapstone(); disassemble(); });

    connect(m_hv, &HexView::cursorChanged,
            this, [this](size_w) { if (!m_pinned) disassemble(); });

    connect(m_hv, &HexView::contentChanged,
            this, [this](size_w, size_w, uint) { if (!m_pinned) disassemble(); });

    connect(m_pinAction, &QAction::triggered,
            this, [this]() { setPinned(!m_pinned); });
}

void DisassemblerPanel::openCapstone()
{
    closeCapstone();
    const int idx = m_archCombo ? qBound(0, m_archCombo->currentIndex(), kArchCount - 1) : 0;
    m_csArch = kArchEntries[idx].arch;
    m_csMode = kArchEntries[idx].mode;
    m_csOpen = (cs_open(m_csArch, m_csMode, &m_csHandle) == CS_ERR_OK);
    if (m_csOpen)
        cs_option(m_csHandle, CS_OPT_SYNTAX, CS_OPT_SYNTAX_INTEL);
}

void DisassemblerPanel::highlightCurrentLine()
{
    const QColor bg  = QColor(m_hv->getHexColour(HVC_BACKGROUND));
    const QColor sel = QColor(m_hv->getHexColour(HVC_SELECTION));
    // Subtle tint: 15 % selection colour blended over the background
    const QColor hlColor(qRound(bg.red()   * 0.85 + sel.red()   * 0.15),
                         qRound(bg.green() * 0.85 + sel.green() * 0.15),
                         qRound(bg.blue()  * 0.85 + sel.blue()  * 0.15));

    QTextEdit::ExtraSelection hl;
    hl.format.setBackground(hlColor);
    hl.format.setProperty(QTextFormat::FullWidthSelection, true);
    hl.cursor = m_view->textCursor();
    hl.cursor.clearSelection();
    m_view->setExtraSelections({hl});
}

void DisassemblerPanel::closeCapstone()
{
    if (m_csOpen)
    {
        cs_close(&m_csHandle);
        m_csOpen = false;
    }
}

void DisassemblerPanel::disassemble()
{
    if (!m_csOpen || !m_hv)
    {
        m_view->clear();
        m_statusLabel->clear();
        return;
    }

    const size_w fileSize = m_hv->size();
    if (fileSize == 0)
    {
        m_view->clear();
        m_statusLabel->clear();
        return;
    }

    const size_w offset      = m_hv->cursorOffset();
    const size_w remaining   = fileSize - offset;
    const size_t bytesToRead = (size_t)qMin((size_w)512, remaining);

    QByteArray buf(static_cast<int>(bytesToRead), '\0');
    const size_t got = m_hv->getData(offset,
                                     reinterpret_cast<uint8_t *>(buf.data()),
                                     bytesToRead);
    if (got == 0)
    {
        m_view->setPlainText(tr("(no data at cursor)"));
        m_statusLabel->clear();
        return;
    }

    cs_insn *insn   = nullptr;
    const size_t count = cs_disasm(m_csHandle,
                                   reinterpret_cast<const uint8_t *>(buf.constData()),
                                   got,
                                   static_cast<uint64_t>(offset),
                                   0,
                                   &insn);

    if (count == 0)
    {
        m_view->setPlainText(tr("(no instructions decoded)"));
        m_statusLabel->clear();
        return;
    }

    // Compute address column width from the last instruction address
    const quint64 lastAddr  = insn[count - 1].address;
    const int     addrWidth = lastAddr <= 0xFFFF       ? 4
                            : lastAddr <= 0xFFFFFFFF   ? 8
                                                       : 12;

    // ── column tuning (edit these to taste) ───────────────────────────────────
    static constexpr int  kMaxShowBytes        = 6;    // cap on bytes shown per insn (".." if longer)
    static constexpr int  kBytesGap            = 2;    // min spaces between bytes column and mnemonic
    static constexpr int  kMnemonicGap         = 3;    // spaces between mnemonic column and operands

    // ── character formats ──────────────────────────────────────────────────────
    const QPalette &pal = m_view->palette();
    const QColor    wt  = pal.windowText().color();

    // Semantic colours from the hex view's own palette
    const QColor addrColor  = QColor(m_hv->getHexColour(HVC_ADDRESS));
    const QColor bytesColor = blend(QColor(m_hv->getHexColour(HVC_ADDRESS)), m_hv->getHexColour(HVC_BACKGROUND));
    const QColor instrColor(0xcf, 0x23, 0x38);
    const QColor opColor(0x06, 0x51, 0xae);
    const QColor numColor = QColor(m_hv->getHexColour(HVC_HEXODD));

    // Bytes column: midpoint between instrColor and opColor
    /*const QColor bytesColor(qRound((instrColor.red()   + opColor.red())   / 2.0),
                            qRound((instrColor.green() + opColor.green()) / 2.0),
                            qRound((instrColor.blue()  + opColor.blue())  / 2.0));*/

    QTextCharFormat fmtGrey, fmtBytes, fmtInstr, fmtOp, fmtMod, fmtNum, fmtPunct;
    fmtGrey.setForeground(addrColor);
    fmtBytes.setForeground(bytesColor);
    fmtInstr.setForeground(instrColor);       // mnemonic – odd column, bold
    fmtInstr.setFontWeight(QFont::Medium);
    fmtOp.setForeground(opColor);             // registers/identifiers – even column, bold
    fmtOp.setFontWeight(QFont::Medium);
    fmtMod.setForeground(wt);                 // byte ptr / dword ptr – neutral, not bold
    fmtNum.setForeground(numColor);           // numeric literals – modified colour, bold
    fmtNum.setFontWeight(QFont::Medium);
    fmtPunct.setForeground(wt);               // [ ] , + - * : – plain foreground
    fmtPunct.setFontWeight(QFont::Bold);               // [ ] , + - * : – plain foreground

    // Single pass regex: modifier phrases > hex numbers > decimal > punctuation
    static const QRegularExpression reTok(QStringLiteral(
        R"((?<mod>\b(?:byte|word|dword|qword|xmmword|ymmword|zmmword|tbyte|tword|oword) ptr\b))"
        R"(|(?<num>0x[0-9a-fA-F]+|\b[0-9]+\b))"
        R"(|(?<punct>[[\],+\-*:{}]))"));

    // Helper: insert operand string with numbers, punctuation and modifiers coloured
    auto insertOperands = [&](QTextCursor &cur, const QString &ops) {
        QRegularExpressionMatchIterator it = reTok.globalMatch(ops);
        int lastEnd = 0;
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            if (m.capturedStart() > lastEnd)
                cur.insertText(ops.mid(lastEnd, m.capturedStart() - lastEnd), fmtOp);
            if (!m.captured(QStringLiteral("mod")).isEmpty())
                cur.insertText(m.captured(), fmtMod);
            else if (!m.captured(QStringLiteral("num")).isEmpty())
                cur.insertText(m.captured(), fmtNum);
            else
                cur.insertText(m.captured(), fmtPunct);
            lastEnd = m.capturedEnd();
        }
        if (lastEnd < ops.size())
            cur.insertText(ops.mid(lastEnd), fmtOp);
    };

    // ── pre-pass: measure columns ──────────────────────────────────────────────
    int maxMnemonicLen = 0;
    int maxBytesShown  = 0;
    for (size_t i = 0; i < count; ++i) {
        maxMnemonicLen = qMax(maxMnemonicLen, (int)qstrlen(insn[i].mnemonic));
        maxBytesShown  = qMax(maxBytesShown,  qMin((int)insn[i].size, kMaxShowBytes));
    }
    // bytes column: wide enough for the longest instruction in this batch, plus gap
    const int bytesColWidth = maxBytesShown * 3 - 1 + kBytesGap;

    // ── build document ─────────────────────────────────────────────────────────
    m_view->clear();
    QTextCursor cur(m_view->document());
    cur.beginEditBlock();

    for (size_t i = 0; i < count; ++i)
    {
        if (i > 0)
            cur.insertText(QStringLiteral("\n"), fmtGrey);

        const cs_insn &in = insn[i];

        // Address (no 0x prefix)
        cur.insertText(
            QString::number(in.address, 16).rightJustified(addrWidth, QLatin1Char('0')) +
            QLatin1Char(' '),
            fmtGrey);

        // Hex bytes (up to kMaxShowBytes, ".." if truncated) – dark grey
        const int showBytes = qMin((int)in.size, kMaxShowBytes);
        QString hexBytes;
        hexBytes.reserve(showBytes * 3 + 2);
        for (int b = 0; b < showBytes; ++b) {
            if (b > 0) hexBytes += QLatin1Char(' ');
            hexBytes += QString::number(in.bytes[b], 16).rightJustified(2, QLatin1Char('0'));
        }
        if ((int)in.size > kMaxShowBytes)
            hexBytes += QLatin1String("..");
        cur.insertText(hexBytes.leftJustified(bytesColWidth, QLatin1Char(' ')), fmtBytes);

        // Mnemonic – right-padded so all operand columns align
        const QString mnemonic = QLatin1String(in.mnemonic);
        cur.insertText(in.op_str[0] ? mnemonic.leftJustified(maxMnemonicLen + kMnemonicGap,
                                                              QLatin1Char(' '))
                                    : mnemonic,
                       fmtInstr);

        // Operands (numbers coloured red within them)
        if (in.op_str[0])
            insertOperands(cur, QLatin1String(in.op_str));
    }

    cur.endEditBlock();
    if constexpr (kHighlightCurrentLine)
        highlightCurrentLine();

    // Status line
    const size_t coveredBytes = static_cast<size_t>(
        insn[count - 1].address + insn[count - 1].size - offset);
    m_statusLabel->setText(
        tr("%1 instructions · %2 bytes")
            .arg(count)
            .arg(coveredBytes));

    cs_free(insn, count);
    updateOffsetDisplay();
}

void DisassemblerPanel::updateOffsetDisplay()
{
    if (!m_offsetEdit || !m_hv || m_pinned)
        return;
    const size_w offset = m_hv->cursorOffset();
    m_offsetEdit->setText(QString::number(offset, 16).toUpper().rightJustified(8, QLatin1Char('0')));
}

void DisassemblerPanel::setPinned(bool pinned)
{
    m_pinned = pinned;
    const QString iconName  = pinned ? QStringLiteral("actions/pin0") : QStringLiteral("actions/pin1");
    const QColor  iconColor = pinned
        ? filestats::subduedTextColor(m_offsetEdit->palette())
        : m_offsetEdit->palette().color(QPalette::WindowText);
    m_pinAction->setIcon(recoloredIcon(iconName, iconColor, 16));
    m_pinAction->setToolTip(pinned ? tr("Unpin offset") : tr("Pin offset"));
    if (!pinned)
        disassemble();
}

// ── DisassemblerPanelHost ─────────────────────────────────────────────────────

DisassemblerPanelHost::DisassemblerPanelHost(HexView *hv, QWidget *parent)
    : SidePanelHostBase(450, 300, 800, /*gripOnLeft=*/true, parent)
    , m_hv(hv)
{}

QWidget *DisassemblerPanelHost::createPanelWidget()
{
    auto *panel = new DisassemblerPanel(m_hv, this);
    connect(panel, &DisassemblerPanel::closeRequested,
            this, &DisassemblerPanelHost::closePanel);
    return panel;
}
