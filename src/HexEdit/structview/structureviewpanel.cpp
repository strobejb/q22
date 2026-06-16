#include "structview/structureviewpanel.h"

#include "HexView/hexview.h"
#include "combos/menucombobox.h"
#include "filestats/widgets.h"
#include "structview/structuredefinitionmanager.h"
#include "structview/structuregriditemdelegate.h"
#include "structview/structuretreemodel.h"
#include "theme.h"

#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
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

namespace
{
static constexpr int kHeaderBottomGap = 3;

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
        painter->fillRect(rect, palette().base());

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
        opt.palette.setColor(QPalette::Button, palette().base().color());
        opt.palette.setColor(QPalette::Window, palette().base().color());
        style()->drawControl(QStyle::CE_Header, &opt, painter, this);

        const bool hovered = rect.contains(mapFromGlobal(QCursor::pos()));
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
};

class StructureGridView : public QTreeView
{
public:
    explicit StructureGridView(QWidget *parent = nullptr)
        : QTreeView(parent)
    {
    }

protected:
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
            this, [this](size_w) { updateOffsetDisplay(); });
    connect(m_hv, &HexView::contentChanged,
            this, [this](size_w, size_w, uint) { updateOffsetDisplay(); });
    connect(m_pinAction, &QAction::triggered,
            this, [this]() { setPinned(!m_pinned); });
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
    QList<TypeDecl *> exportedDecls;
    for (const ExportedStructureType &type : exportedTypes)
        exportedDecls.push_back(type.typeDecl);

    m_model->setTypeDecls(exportedDecls);

    m_rootCombo->clear();
    for (const ExportedStructureType &type : exportedTypes)
        m_rootCombo->addItem(displayNameForTypeDecl(type.typeDecl));

    if (!m_definitions->lastError().isEmpty())
    {
        m_statusLabel->setText(m_definitions->lastError());
        return;
    }

    const int fileCount = m_definitions->definitionFiles().size();
    m_statusLabel->setText(tr("%1 definition file(s), %2 exported type(s)").arg(fileCount).arg(exportedTypes.size()));
}

void StructureViewPanel::updateOffsetDisplay()
{
    if (!m_offsetEdit || !m_hv || m_pinned)
        return;

    m_offsetEdit->setText(QString::number(m_hv->cursorOffset(), 16).toUpper().rightJustified(8, QLatin1Char('0')));
}

void StructureViewPanel::setPinned(bool pinned)
{
    m_pinned = pinned;
    const QString iconName = pinned ? QStringLiteral("actions/pin0") : QStringLiteral("actions/pin1");
    const QColor iconColor = pinned
        ? filestats::subduedTextColor(m_offsetEdit->palette())
        : m_offsetEdit->palette().color(QPalette::WindowText);
    m_pinAction->setIcon(recoloredIcon(iconName, iconColor, 16));
    m_pinAction->setToolTip(pinned ? tr("Unpin offset") : tr("Pin offset"));
    if (!pinned)
        updateOffsetDisplay();
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
