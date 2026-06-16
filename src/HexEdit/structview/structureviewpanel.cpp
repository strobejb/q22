#include "structview/structureviewpanel.h"

#include "HexView/hexview.h"
#include "combos/menucombobox.h"
#include "filestats/widgets.h"
#include "structview/structuredefinitionmanager.h"
#include "structview/structuregriditemdelegate.h"
#include "structview/structuretreemodel.h"
#include "structview/structurevaluebuilder.h"
#include "theme.h"

#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionHeader>
#include <QTreeView>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>
#include <cstdint>

namespace
{
static constexpr int kHeaderBottomGap = 3;

enum class InitialStructureExpansion
{
    Collapsed,
    FirstLevel,
    FirstLevelAndFirstField,
    All,
};

static constexpr InitialStructureExpansion kInitialStructureExpansion =
    InitialStructureExpansion::FirstLevelAndFirstField;

qreal devicePixelSize(const QPainter *painter)
{
    const qreal dpr = painter && painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    return dpr > 0.0 ? 1.0 / dpr : 1.0;
}

qreal snapToDevicePixel(const QPainter *painter, qreal value)
{
    const qreal dpr = painter && painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    return dpr > 0.0 ? std::round(value * dpr) / dpr : value;
}

bool useClassicPlusMinusExpanders()
{
    return qEnvironmentVariableIntValue("QEXED_STRUCTURE_PLUS_MINUS") != 0;
}

class StructureGridHeader : public QHeaderView
{
public:
    explicit StructureGridHeader(Qt::Orientation orientation, QWidget *parent = nullptr)
        : QHeaderView(orientation, parent)
    {
        setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        setHighlightSections(false);
        setSectionsClickable(false);
        setMouseTracking(true);
    }

protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override
    {
        if (!painter || !rect.isValid())
            return;

        painter->save();
        const bool hovered = rect.contains(mapFromGlobal(QCursor::pos()));
        const QColor background = hovered ? palette().color(QPalette::Button)
                                          : palette().color(QPalette::Base);
        painter->fillRect(rect, background);

        QFont headerFont = font();
        if (headerFont.pointSizeF() > 0)
            headerFont.setPointSizeF(qMax(1.0, headerFont.pointSizeF() - 1.0));
        else if (headerFont.pixelSize() > 0)
            headerFont.setPixelSize(qMax(1, headerFont.pixelSize() - 1));
        headerFont.setWeight(QFont::DemiBold);

        QStyleOptionHeader opt;
        initStyleOption(&opt);
        initStyleOptionForIndex(&opt, logicalIndex);
        opt.rect = rect.adjusted(0, 0, 0, -kHeaderBottomGap);
        opt.fontMetrics = QFontMetrics(headerFont);
        opt.text.clear();
        opt.sortIndicator = QStyleOptionHeader::None;
        opt.palette.setColor(QPalette::Button, background);
        opt.palette.setColor(QPalette::Window, background);
        style()->drawControl(QStyle::CE_Header, &opt, painter, this);

        const QColor textColor = hovered ? palette().color(QPalette::WindowText)
                                         : filestats::stringsHeaderTextColor(palette());
        const int pad = filestats::stringsHeaderPadding(QFontMetrics(headerFont));
        const QRect textRect = opt.rect.adjusted(pad, pad, -pad, -pad);

        painter->setFont(headerFont);
        painter->setPen(textColor);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                          model() ? model()->headerData(logicalIndex, orientation(), Qt::DisplayRole).toString()
                                  : QString());
        painter->restore();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        QHeaderView::mouseMoveEvent(event);
        viewport()->update();
    }

    void leaveEvent(QEvent *event) override
    {
        QHeaderView::leaveEvent(event);
        viewport()->update();
    }
};

class StructureGridView : public QTreeView
{
public:
    explicit StructureGridView(QWidget *parent = nullptr)
        : QTreeView(parent)
    {
    }

protected:
    bool viewportEvent(QEvent *event) override
    {
        switch (event->type())
        {
        case QEvent::MouseButtonPress:
        {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton)
            {
                m_resizeSection = resizeSectionAt(mouse->pos());
                if (m_resizeSection >= 0)
                {
                    m_resizeStartX = mouse->position().x();
                    m_resizeStartWidth = header()->sectionSize(m_resizeSection);
                    return true;
                }
            }
            break;
        }
        case QEvent::MouseButtonDblClick:
        {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton && toggleAggregateRow(indexAt(mouse->pos())))
                return true;
            break;
        }
        case QEvent::MouseMove:
        {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (m_resizeSection >= 0)
            {
                const int dx = qRound(mouse->position().x() - m_resizeStartX);
                header()->resizeSection(m_resizeSection,
                                        qMax(header()->minimumSectionSize(), m_resizeStartWidth + dx));
                return true;
            }
            viewport()->setCursor(resizeSectionAt(mouse->pos()) >= 0 ? Qt::SplitHCursor : Qt::ArrowCursor);
            break;
        }
        case QEvent::MouseButtonRelease:
        {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (m_resizeSection >= 0)
            {
                m_resizeSection = -1;
                viewport()->setCursor(resizeSectionAt(mouse->pos()) >= 0 ? Qt::SplitHCursor : Qt::ArrowCursor);
                return true;
            }
            break;
        }
        case QEvent::Leave:
            if (m_resizeSection < 0)
                viewport()->unsetCursor();
            break;
        default:
            break;
        }

        return QTreeView::viewportEvent(event);
    }

    void drawBranches(QPainter *painter, const QRect &rect, const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        const QColor grid = StructureGridItemDelegate::gridColour(palette());
        const qreal px = devicePixelSize(painter);
        const qreal topY = snapToDevicePixel(painter, rect.top());
        painter->fillRect(QRectF(rect.left(), topY, rect.width(), px), grid);
        if (!indexBelow(index).isValid())
        {
            const qreal bottomY = snapToDevicePixel(painter, rect.bottom() + 1.0) - px;
            painter->fillRect(QRectF(rect.left(), bottomY, rect.width(), px), grid);
        }
        painter->restore();

        if (!useClassicPlusMinusExpanders())
        {
            QTreeView::drawBranches(painter, rect, index);
            return;
        }

        if (!model() || !model()->hasChildren(index))
            return;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);

        constexpr int boxSize = 11;
        const QRect box(rect.left() + qMax(0, (rect.width() - boxSize) / 2),
                        rect.top() + qMax(0, (rect.height() - boxSize) / 2),
                        boxSize,
                        boxSize);

        const QColor border = palette().color(QPalette::Mid);
        const QColor fill = palette().color(QPalette::Base);
        const QColor mark = palette().color(QPalette::WindowText);

        painter->fillRect(box, fill);
        painter->setPen(border);
        painter->drawRect(box.adjusted(0, 0, -1, -1));

        painter->setPen(mark);
        const int midX = box.center().x();
        const int midY = box.center().y();
        painter->drawLine(box.left() + 3, midY, box.right() - 3, midY);
        if (!isExpanded(index))
            painter->drawLine(midX, box.top() + 3, midX, box.bottom() - 3);

        painter->restore();
    }

private:
    bool toggleAggregateRow(const QModelIndex &index)
    {
        if (!index.isValid()
            || (index.column() != StructureTreeModel::ValueColumn
                && index.column() != StructureTreeModel::OffsetColumn))
            return false;

        const QModelIndex rowIndex = index.sibling(index.row(), StructureTreeModel::NameColumn);
        const QModelIndex valueIndex = index.sibling(index.row(), StructureTreeModel::ValueColumn);
        const QString value = valueIndex.data(Qt::DisplayRole).toString().trimmed();
        if (!model()->hasChildren(rowIndex) || !value.startsWith(QLatin1Char('{')))
            return false;

        setExpanded(rowIndex, !isExpanded(rowIndex));
        return true;
    }

    int resizeSectionAt(const QPoint &pos) const
    {
        constexpr int kResizeSlop = 4;
        const int count = header()->count();
        for (int visual = 0; visual < count - 1; ++visual)
        {
            const int logical = header()->logicalIndex(visual);
            if (header()->isSectionHidden(logical))
                continue;

            const int edgeX = header()->sectionViewportPosition(logical) + header()->sectionSize(logical);
            if (qAbs(pos.x() - edgeX) <= kResizeSlop)
                return logical;
        }

        return -1;
    }

    int m_resizeSection = -1;
    qreal m_resizeStartX = 0.0;
    int m_resizeStartWidth = 0;
};
}

StructureViewPanel::StructureViewPanel(HexView *hv, QWidget *parent)
    : QWidget(parent)
    , m_hv(hv)
    , m_definitions(new StructureDefinitionManager(this))
    , m_model(new StructureTreeModel(this))
{
    buildUi();
}

StructureViewPanel::~StructureViewPanel()
{
    clearHexViewOverlay();
}

void StructureViewPanel::refresh()
{
    m_definitions->reload();
}

void StructureViewPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_definitions->ensureLoaded();
    updateOffsetDisplay();
}

void StructureViewPanel::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    clearHexViewOverlay();
}

void StructureViewPanel::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        updateTreeSelectionPalette();
}

void StructureViewPanel::buildUi()
{
    auto *rootLay = new QVBoxLayout(this);
    rootLay->setContentsMargins(0, filestats::kContentMargin, 0, 0);
    rootLay->setSpacing(0);

    const int scrollBarW = QApplication::style()->pixelMetric(QStyle::PM_ScrollBarExtent);

    auto *header = new filestats::SectionHeader(tr("Structure View"), this);
    header->setCloseMode(true);
    header->setExpandCallback([this]() { emit closeRequested(); });

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, scrollBarW, 0);
    headerRow->setSpacing(0);
    headerRow->addWidget(header);
    rootLay->addLayout(headerRow);

    auto *content = new QWidget(this);
    auto *contentLay = new QVBoxLayout(content);
    contentLay->setContentsMargins(filestats::kContentMargin, filestats::kContentMargin,
                                   filestats::kContentMargin + 7, 8);
    contentLay->setSpacing(4);

    auto *optLay = new QHBoxLayout;
    optLay->setContentsMargins(0, 0, 0, 0);
    optLay->setSpacing(6);

    m_rootCombo = new MenuComboBox(content);
    m_rootCombo->setToolTip(tr("Root structure"));
    m_rootCombo->setLeadingIcon(
        recoloredIcon(QStringLiteral("actions/hierarchy1"),
                      filestats::subduedTextColor(palette()), 16));
    const int comboH = qMax(24, static_cast<QComboBox *>(m_rootCombo)->sizeHint().height() - 4);
    m_rootCombo->setFixedHeight(comboH);

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
    const int offsetW = m_offsetEdit->fontMetrics().horizontalAdvance(QStringLiteral("00000000")) + 40
                        + 16 + 8; // icon + Qt's action button right margin
    m_offsetEdit->setFixedWidth(offsetW);

    optLay->addWidget(m_rootCombo, 1);
    optLay->addWidget(m_offsetEdit);
    contentLay->addLayout(optLay);
    contentLay->addSpacing(4);

    auto *gridFrame = new QFrame(content);
    gridFrame->setObjectName(QStringLiteral("structureGridFrame"));
    gridFrame->setStyleSheet(QStringLiteral(R"(
        QFrame#structureGridFrame {
            background: palette(base);
            border: 1px solid palette(mid);
            border-radius: 6px;
        }
    )"));

    auto *gridLay = new QVBoxLayout(gridFrame);
    gridLay->setContentsMargins(1, 1, 1, 1);
    gridLay->setSpacing(0);

    m_tree = new StructureGridView(gridFrame);
    m_tree->setObjectName(QStringLiteral("structureGrid"));
    m_tree->setModel(m_model);
    m_tree->setItemDelegate(new StructureGridItemDelegate(m_tree));
    m_tree->setHeader(new StructureGridHeader(Qt::Horizontal, m_tree));
    m_tree->setRootIsDecorated(true);
    m_tree->setAlternatingRowColors(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_tree->setMouseTracking(true);
    m_tree->setIndentation(18);
    m_tree->header()->setStretchLastSection(true);
    m_tree->header()->setMinimumSectionSize(48);
    m_tree->header()->setSectionResizeMode(StructureTreeModel::NameColumn, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(StructureTreeModel::ValueColumn, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(StructureTreeModel::OffsetColumn, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(StructureTreeModel::CommentColumn, QHeaderView::Interactive);
    m_tree->header()->resizeSection(StructureTreeModel::NameColumn, 190);
    m_tree->header()->resizeSection(StructureTreeModel::ValueColumn, 90);
    m_tree->header()->resizeSection(StructureTreeModel::OffsetColumn, 84);
    m_tree->header()->resizeSection(StructureTreeModel::CommentColumn, 140);
    connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &StructureViewPanel::updateHexViewSelection);
    updateTreeSelectionPalette();
    {
        QFont headerFont = m_tree->header()->font();
        if (headerFont.pointSizeF() > 0)
            headerFont.setPointSizeF(qMax(1.0, headerFont.pointSizeF() - 1.0));
        else if (headerFont.pixelSize() > 0)
            headerFont.setPixelSize(qMax(1, headerFont.pixelSize() - 1));
        headerFont.setWeight(QFont::DemiBold);
        const QFontMetrics metrics(headerFont);
        const int headerPad = filestats::stringsHeaderPadding(metrics);
        constexpr int itemCellInset = 3;
        m_treeItemLeftPad = qMax(0, headerPad - itemCellInset);
        m_tree->header()->setFixedHeight(metrics.height() + 2 * headerPad + kHeaderBottomGap);
        updateTreeSelectionPalette();
    }

    gridLay->addWidget(m_tree);
    contentLay->addWidget(gridFrame, 1);

    m_statusLabel = new QLabel(content);
    m_statusLabel->setTextFormat(Qt::PlainText);
    m_statusLabel->setWordWrap(true);
    contentLay->addWidget(m_statusLabel);

    rootLay->addWidget(content, 1);

    connect(m_definitions, &StructureDefinitionManager::definitionsReloaded,
            this, &StructureViewPanel::updateDefinitionsUi);
    connect(m_definitions, &StructureDefinitionManager::reloadFailed,
            this, [this](const QString &message) {
                m_statusLabel->setText(message);
                updateDefinitionsUi();
            });
    connect(m_hv, &HexView::cursorChanged,
            this, [this](size_w) {
                if (m_updatingHexViewFromStructure)
                    return;
                updateOffsetDisplay();
                uint64_t explicitOffset = 0;
                if (!m_pinned && !explicitRootOffset(selectedRootType(), &explicitOffset))
                    rebuildRows();
            });
    connect(m_hv, &HexView::contentChanged,
            this, [this](size_w, size_w, uint) {
                updateOffsetDisplay();
                if (!m_pinned)
                    rebuildRows();
            });
    connect(m_pinAction, &QAction::triggered,
            this, [this]() { setPinned(!m_pinned); });
    connect(m_rootCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { rebuildRows(); });
}

void StructureViewPanel::updateTreeSelectionPalette()
{
    if (!m_tree || !m_hv)
        return;

    const QColor activeSelection = QColor(m_hv->getHexColour(HVC_SELECTION));
    const QColor activeText = QColor(m_hv->getHexColour(HVC_SELTEXT));
    const QColor inactiveSelection = QColor(m_hv->getHexColour(HVC_SELECTION_INACTIVE));
    const QColor inactiveText = QColor(m_hv->getHexColour(HVC_SELTEXT_INACTIVE));

    QPalette pal = m_tree->palette();
    pal.setColor(QPalette::Active, QPalette::Highlight, activeSelection);
    pal.setColor(QPalette::Active, QPalette::HighlightedText, activeText);
    pal.setColor(QPalette::Inactive, QPalette::Highlight, inactiveSelection);
    pal.setColor(QPalette::Inactive, QPalette::HighlightedText, inactiveText);
    m_tree->setPalette(pal);
    if (m_tree->viewport())
        m_tree->viewport()->setPalette(pal);

    m_tree->setStyleSheet(
        QStringLiteral(R"(
        QTreeView#structureGrid {
            background: palette(base);
            border: none;
            border-radius: 5px;
            padding: 0px;
            outline: none;
        }
        QTreeView#structureGrid::viewport {
            background: palette(base);
            border-radius: 5px;
        }
        QTreeView#structureGrid QHeaderView::section {
            background: palette(base);
            border: none;
            padding: 4px 6px;
        }
        QTreeView#structureGrid QHeaderView::section:hover {
            background: palette(button);
        }
        QTreeView#structureGrid::item {
            padding: 3px 6px 3px %1px;
        }
        QTreeView#structureGrid::item:hover {
            background: palette(button);
        }
        QTreeView#structureGrid::item:selected {
            background: %2;
            color: %3;
        }
        QTreeView#structureGrid::item:selected:!active {
            background: %4;
            color: %5;
        }
    )").arg(m_treeItemLeftPad)
            .arg(filestats::cssColor(activeSelection),
                 filestats::cssColor(activeText),
                 filestats::cssColor(inactiveSelection),
                 filestats::cssColor(inactiveText)));
}

void StructureViewPanel::updateDefinitionsUi()
{
    const QList<ExportedStructureType> exportedTypes = m_definitions->exportedTypes();

    m_rootCombo->clear();
    for (const ExportedStructureType &type : exportedTypes)
    {
        m_rootCombo->addItem(displayNameForTypeDecl(type.typeDecl),
                             QVariant::fromValue<qulonglong>(reinterpret_cast<qulonglong>(type.typeDecl)));
    }
    selectAssociatedRootType(exportedTypes);

    if (!m_definitions->lastError().isEmpty())
    {
        m_statusLabel->setText(m_definitions->lastError());
        return;
    }

    const int fileCount = m_definitions->definitionFiles().size();
    m_statusLabel->setText(tr("%1 definition file(s), %2 exported type(s)").arg(fileCount).arg(exportedTypes.size()));
    rebuildRows();
}

void StructureViewPanel::updateOffsetDisplay()
{
    if (!m_offsetEdit || !m_hv || m_pinned)
        return;

    uint64_t explicitOffset = 0;
    const uint64_t offset = explicitRootOffset(selectedRootType(), &explicitOffset)
        ? explicitOffset
        : m_hv->cursorOffset();
    m_offsetEdit->setText(QString::number(offset, 16).toUpper().rightJustified(8, QLatin1Char('0')));
}

void StructureViewPanel::updatePinAction()
{
    if (!m_pinAction || !m_offsetEdit)
        return;

    const QString iconName = m_pinned ? QStringLiteral("actions/pin0") : QStringLiteral("actions/pin1");
    const QColor iconColor = m_pinned
        ? filestats::subduedTextColor(m_offsetEdit->palette())
        : m_offsetEdit->palette().color(QPalette::WindowText);
    m_pinAction->setIcon(recoloredIcon(iconName, iconColor, 16));
    m_pinAction->setToolTip(m_pinned ? tr("Unpin offset") : tr("Pin offset"));
}

void StructureViewPanel::setPinned(bool pinned)
{
    m_pinned = pinned;
    if (pinned && m_hv)
        m_pinnedOffset = m_hv->cursorOffset();
    updatePinAction();
    if (!pinned)
        updateOffsetDisplay();
    rebuildRows();
}

void StructureViewPanel::rebuildRows()
{
    if (!m_model || !m_hv || !m_definitions || !m_definitions->library())
        return;

    TypeDecl *rootType = selectedRootType();
    if (!rootType)
    {
        m_model->clear();
        clearHexViewOverlay();
        return;
    }

    uint64_t explicitOffset = 0;
    const bool hasExplicitOffset = explicitRootOffset(rootType, &explicitOffset);
    if (hasExplicitOffset && m_pinned)
    {
        m_pinned = false;
        updatePinAction();
    }
    const uint64_t baseOffset = hasExplicitOffset ? explicitOffset
        : (m_pinned ? m_pinnedOffset : m_hv->cursorOffset());
    m_offsetEdit->setText(QString::number(baseOffset, 16).toUpper().rightJustified(8, QLatin1Char('0')));
    StructureValueBuilder builder;
    m_rebuildingRows = true;
    m_model->setRows(builder.build(m_definitions->library(),
                                   rootType,
                                   baseOffset,
                                   [this](uint64_t offset, uint8_t *buffer, size_t length) -> size_t {
                                       return m_hv ? m_hv->getData(static_cast<size_w>(offset), buffer, length) : 0;
                                   }));
    applyInitialExpansion();
    m_rebuildingRows = false;
    clearHexViewOverlay();
}

void StructureViewPanel::applyInitialExpansion()
{
    if (!m_tree || !m_model)
        return;

    m_tree->collapseAll();

    switch (kInitialStructureExpansion)
    {
    case InitialStructureExpansion::Collapsed:
        return;
    case InitialStructureExpansion::All:
        m_tree->expandAll();
        return;
    case InitialStructureExpansion::FirstLevel:
    case InitialStructureExpansion::FirstLevelAndFirstField:
        break;
    }

    const int rootRows = m_model->rowCount();
    for (int row = 0; row < rootRows; ++row)
    {
        const QModelIndex rootIndex = m_model->index(row, StructureTreeModel::NameColumn);
        if (!rootIndex.isValid())
            continue;

        m_tree->expand(rootIndex);

        if (kInitialStructureExpansion == InitialStructureExpansion::FirstLevelAndFirstField)
        {
            const QModelIndex firstField = m_model->index(0, StructureTreeModel::NameColumn, rootIndex);
            if (firstField.isValid())
                m_tree->expand(firstField);
        }
    }
}

void StructureViewPanel::updateHexViewSelection(const QModelIndex &current)
{
    if (!m_hv || !m_model || m_rebuildingRows)
        return;

    StructureRow *row = m_model->rowForIndex(current);
    if (!row || row->parent == nullptr)
    {
        clearHexViewOverlay();
        return;
    }

    if (row->byteLength > 0 && row->absoluteOffset < m_hv->size())
    {
        const uint64_t requestedEnd = row->absoluteOffset > UINT64_MAX - row->byteLength
            ? UINT64_MAX
            : row->absoluteOffset + row->byteLength;
        const size_w start = static_cast<size_w>(row->absoluteOffset);
        const size_w end = static_cast<size_w>(qMin<uint64_t>(requestedEnd, m_hv->size()));
        if (end > start)
            setHexViewSelectionFromStructure(start, end);
    }

    StructureRow *scope = row->parent;
    while (scope && scope->parent && scope->byteLength == 0)
        scope = scope->parent;

    if (!scope || !scope->parent || scope->byteLength == 0 || scope->absoluteOffset >= m_hv->size())
    {
        clearHexViewOverlay();
        return;
    }

    HexView::OverlayRange range;
    range.offset = static_cast<size_w>(scope->absoluteOffset);
    range.length = static_cast<size_w>(qMin<uint64_t>(scope->byteLength, m_hv->size() - range.offset));
    if (range.length == 0)
    {
        clearHexViewOverlay();
        return;
    }
    range.bgSlot = HVC_RANGE_OVERLAY;
    range.priority = 1;
    m_hv->setOverlayRanges(HexView::OverlayLayer::StructureView, {range});
}

void StructureViewPanel::clearHexViewOverlay()
{
    if (m_hv)
        m_hv->clearOverlayRanges(HexView::OverlayLayer::StructureView);
}

void StructureViewPanel::setHexViewSelectionFromStructure(size_w start, size_w end)
{
    if (!m_hv)
        return;

    m_updatingHexViewFromStructure = true;
    m_hv->setCurSel(start, end, true);
    m_hv->scrollCenterIfOffScreen(start, end - start);
    m_updatingHexViewFromStructure = false;
}

bool StructureViewPanel::explicitRootOffset(TypeDecl *rootType, uint64_t *offset) const
{
    if (!rootType || !offset)
        return false;

    ExprNode *expr = nullptr;
    if (!FindTag(rootType->tagList, TOK_OFFSET, &expr) || !expr)
        return false;

    // Root-level offsets are a file-placement hint for exported definitions.
    // They must be constant here; data-dependent field offsets are handled by
    // StructureRenderEngine while it walks already-rendered rows.
    switch (expr->type)
    {
    case EXPR_NUMBER:
    case EXPR_UNARY:
    case EXPR_BINARY:
    case EXPR_TERTIARY:
        *offset = static_cast<uint64_t>(Evaluate(expr));
        return true;
    default:
        return false;
    }
}

void StructureViewPanel::selectAssociatedRootType(const QList<ExportedStructureType> &exportedTypes)
{
    if (!m_rootCombo || !m_hv || exportedTypes.isEmpty())
        return;

    const QString suffix = QFileInfo(m_hv->filePath()).suffix().toLower();
    if (suffix.isEmpty())
        return;

    const QString dottedSuffix = QLatin1Char('.') + suffix;
    for (int i = 0; i < exportedTypes.size(); ++i)
    {
        if (exportedTypes[i].assocExtensions.contains(dottedSuffix, Qt::CaseInsensitive))
        {
            m_rootCombo->setCurrentIndex(i);
            return;
        }
    }
}

TypeDecl *StructureViewPanel::selectedRootType() const
{
    if (!m_rootCombo || m_rootCombo->currentIndex() < 0)
        return nullptr;

    const qulonglong ptr = m_rootCombo->currentData().toULongLong();
    return reinterpret_cast<TypeDecl *>(ptr);
}

QString StructureViewPanel::displayNameForTypeDecl(TypeDecl *decl) const
{
    if (!decl)
        return {};

    for (Type *type : decl->declList)
    {
        if (type && type->sym)
            return QString::fromLocal8Bit(type->sym->name);
    }

    if (decl->baseType && decl->baseType->ty == typeSTRUCT && decl->baseType->sptr && decl->baseType->sptr->symbol)
        return tr("struct %1").arg(QString::fromLocal8Bit(decl->baseType->sptr->symbol->name));

    return tr("(anonymous type)");
}

StructureViewPanelHost::StructureViewPanelHost(HexView *hv, QWidget *parent)
    : SidePanelHostBase(450, 300, 800, true, parent)
    , m_hv(hv)
{
}

QWidget *StructureViewPanelHost::createPanelWidget()
{
    auto *panel = new StructureViewPanel(m_hv);
    connect(panel, &StructureViewPanel::closeRequested,
            this, &StructureViewPanelHost::closePanel);
    return panel;
}
