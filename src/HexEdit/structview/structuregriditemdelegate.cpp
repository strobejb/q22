#include "structview/structuregriditemdelegate.h"

#include "structview/structuretreemodel.h"

#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QIcon>
#include <QLineEdit>
#include <QPainter>
#include <QTreeView>

#include <algorithm>
#include <cmath>

namespace
{
static constexpr bool kAlignStructureNameIdentifiers = true;
static constexpr bool kEmphasizeAlignedNameIdentifiers = true;
static constexpr qreal kNameIdentifierColumnWidth = 44.0;
static constexpr qreal kArrayIndexColumnWidth = 24.0;
static constexpr QFont::Weight kStructureTypePrefixWeight = QFont::Weight(650);

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

}

StructureGridItemDelegate::StructureGridItemDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QColor StructureGridItemDelegate::gridColour(const QPalette &palette)
{
    const QColor mid = palette.color(QPalette::Mid);
    const QColor base = palette.color(QPalette::Base);
    constexpr qreal midWeight = 0.45;
    constexpr qreal baseWeight = 1.0 - midWeight;
    return QColor(qRound(mid.red() * midWeight + base.red() * baseWeight),
                  qRound(mid.green() * midWeight + base.green() * baseWeight),
                  qRound(mid.blue() * midWeight + base.blue() * baseWeight),
                  qRound(mid.alpha() * midWeight + base.alpha() * baseWeight));
}

void StructureGridItemDelegate::paint(QPainter *painter,
                                      const QStyleOptionViewItem &option,
                                      const QModelIndex &index) const
{
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);

    if (kAlignStructureNameIdentifiers && paintAlignedName(painter, &opt, index))
    {
        drawGridLines(painter, opt, index);
        return;
    }

    if (index.data(StructureTreeModel::EmphasizeNameRole).toBool())
        opt.font.setBold(true);

    QStyledItemDelegate::paint(painter, opt, index);

    if (isEditingIndex(index))
        return;

    drawGridLines(painter, opt, index);
}

void StructureGridItemDelegate::drawGridLines(QPainter *painter,
                                              const QStyleOptionViewItem &option,
                                              const QModelIndex &index) const
{
    if (isEditingIndex(index))
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    const QColor gridColour = StructureGridItemDelegate::gridColour(option.palette);

    const qreal px = devicePixelSize(painter);
    const qreal y = snapToDevicePixel(painter, option.rect.top());
    const qreal x = snapToDevicePixel(painter, option.rect.right() + 1.0) - px;
    painter->fillRect(QRectF(option.rect.left(), y, option.rect.width(), px), gridColour);
    if (const auto *view = qobject_cast<const QTreeView *>(option.widget))
    {
        if (!view->indexBelow(index).isValid())
        {
            const qreal bottomY = snapToDevicePixel(painter, option.rect.bottom() + 1.0) - px;
            painter->fillRect(QRectF(option.rect.left(), bottomY, option.rect.width(), px), gridColour);
        }
    }
    painter->fillRect(QRectF(x, option.rect.top(), px, option.rect.height()), gridColour);
    painter->restore();
}

QWidget *StructureGridItemDelegate::createEditor(QWidget *parent,
                                                 const QStyleOptionViewItem &option,
                                                 const QModelIndex &index) const
{
    m_editingIndex = QPersistentModelIndex(index);
    if (parent)
        parent->update(option.rect);

    auto *edit = new QLineEdit(parent);
    edit->setFrame(false);
    edit->setAutoFillBackground(true);
    edit->setTextMargins(0, 0, 0, 0);
    edit->setStyleSheet(QStringLiteral(
        "QLineEdit {"
        " border: none;"
        " border-radius: 0px;"
        " padding: 0px;"
        " margin: 0px;"
        " background: palette(base);"
        "}"));
    return edit;
}

void StructureGridItemDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    if (auto *edit = qobject_cast<QLineEdit *>(editor))
    {
        edit->setText(index.data(Qt::EditRole).toString());
        edit->selectAll();
        return;
    }

    QStyledItemDelegate::setEditorData(editor, index);
}

void StructureGridItemDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const
{
    if (auto *edit = qobject_cast<QLineEdit *>(editor))
    {
        model->setData(index, edit->text(), Qt::EditRole);
        return;
    }

    QStyledItemDelegate::setModelData(editor, model, index);
}

void StructureGridItemDelegate::updateEditorGeometry(QWidget *editor,
                                                     const QStyleOptionViewItem &option,
                                                     const QModelIndex &index) const
{
    auto *edit = qobject_cast<QLineEdit *>(editor);
    if (!edit)
    {
        QStyledItemDelegate::updateEditorGeometry(editor, option, index);
        return;
    }

    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);

    const QRect textRect = itemTextRect(option, index);
    const QRect cellRect = opt.rect;

    edit->setGeometry(cellRect);
    edit->setFont(opt.font);

    QPalette pal = edit->palette();
    pal.setColor(QPalette::Base, opt.palette.color(QPalette::Base));
    pal.setColor(QPalette::Text, opt.palette.color(QPalette::Text));
    pal.setColor(QPalette::Highlight, opt.palette.color(QPalette::Highlight));
    pal.setColor(QPalette::HighlightedText, opt.palette.color(QPalette::HighlightedText));
    edit->setPalette(pal);

    edit->setTextMargins(qMax(0, textRect.left() - cellRect.left()),
                         qMax(0, textRect.top() - cellRect.top()),
                         qMax(0, cellRect.right() - textRect.right()),
                         qMax(0, cellRect.bottom() - textRect.bottom()));
}

void StructureGridItemDelegate::destroyEditor(QWidget *editor, const QModelIndex &index) const
{
    QWidget *viewport = editor ? editor->parentWidget() : nullptr;
    const QRect repaintRect = editor ? editor->geometry().adjusted(-2, -2, 2, 2) : QRect();

    if (m_editingIndex == index)
        m_editingIndex = QPersistentModelIndex();

    QStyledItemDelegate::destroyEditor(editor, index);

    if (viewport && repaintRect.isValid())
        viewport->update(repaintRect);
}

bool StructureGridItemDelegate::isEditingIndex(const QModelIndex &index) const
{
    return m_editingIndex.isValid() && m_editingIndex == index;
}

bool StructureGridItemDelegate::paintAlignedName(QPainter *painter,
                                                 QStyleOptionViewItem *option,
                                                 const QModelIndex &index) const
{
    if (!painter || !option || index.column() != StructureTreeModel::NameColumn || isEditingIndex(index))
        return false;

    const QString prefix = index.data(StructureTreeModel::NameTypePrefixRole).toString();
    const QString identifier = index.data(StructureTreeModel::NameIdentifierRole).toString();
    const QString suffix = index.data(StructureTreeModel::NameSuffixRole).toString();
    const bool hasBranchIcon = !index.data(StructureTreeModel::BranchIconPathRole).toString().isEmpty();
    const bool arrayIndexPrefix = prefix.startsWith(QLatin1Char('['));
    const bool emphasizeName = index.data(StructureTreeModel::EmphasizeNameRole).toBool();
    if (prefix.isEmpty() && !hasBranchIcon)
        return false;
    if (!prefix.isEmpty() && !arrayIndexPrefix && identifier.isEmpty())
        return false;

    QStyleOptionViewItem backgroundOption(*option);
    backgroundOption.text.clear();
    backgroundOption.icon = QIcon();
    backgroundOption.features &= ~(QStyleOptionViewItem::HasDisplay | QStyleOptionViewItem::HasDecoration);

    const QWidget *widget = option->widget;
    QStyle *style = widget ? widget->style() : QApplication::style();
    style->drawPrimitive(QStyle::PE_PanelItemViewItem, &backgroundOption, painter, widget);

    const QRect textRect = itemTextRect(*option, index);
    const QFontMetricsF metrics(option->font);
    const qreal spaceWidth = metrics.horizontalAdvance(QLatin1Char(' '));
    const qreal baseline = textRect.top() + (textRect.height() - metrics.height()) / 2.0 + metrics.ascent();
    const QColor textColour = option->state & QStyle::State_Selected
        ? option->palette.color(QPalette::HighlightedText)
        : option->palette.color(QPalette::Text);

    QFont prefixFont = option->font;
    QFont identifierFont = option->font;
    if (!arrayIndexPrefix)
    {
        const QFont::Weight prefixWeight = emphasizeName ? kStructureTypePrefixWeight : option->font.weight();
        if (prefixFont.weight() < prefixWeight)
            prefixFont.setWeight(prefixWeight);

        const QFont::Weight identifierWeight = emphasizeName ? QFont::Bold : QFont::DemiBold;
        if (identifierFont.weight() < identifierWeight)
            identifierFont.setWeight(identifierWeight);
    }

    painter->save();
    painter->setClipRect(option->rect);
    painter->setPen(textColour);

    if (prefix.isEmpty())
    {
        painter->setFont(option->font);
        painter->drawText(QPointF(textRect.left(), baseline), option->text);
        painter->restore();
        return true;
    }

    const QFontMetricsF prefixMetrics(prefixFont);
    const qreal prefixWidth = prefixMetrics.horizontalAdvance(prefix);
    const qreal identifierColumnWidth = arrayIndexPrefix ? kArrayIndexColumnWidth : kNameIdentifierColumnWidth;
    const qreal nameX = textRect.left() + std::max(identifierColumnWidth, prefixWidth + spaceWidth);

    painter->setFont(prefixFont);
    painter->drawText(QPointF(textRect.left(), baseline), prefix);

    if (kEmphasizeAlignedNameIdentifiers && !arrayIndexPrefix)
        painter->setFont(identifierFont);
    painter->drawText(QPointF(nameX, baseline), identifier + suffix);
    painter->restore();

    return true;
}

QRect StructureGridItemDelegate::itemTextRect(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);

    const QWidget *widget = opt.widget;
    QStyle *style = widget ? widget->style() : QApplication::style();
    return style->subElementRect(QStyle::SE_ItemViewItemText, &opt, widget);
}
