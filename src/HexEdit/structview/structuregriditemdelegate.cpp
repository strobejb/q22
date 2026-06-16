#include "structview/structuregriditemdelegate.h"

#include <QApplication>
#include <QLineEdit>
#include <QPainter>
#include <QTreeView>

#include <cmath>

namespace
{
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

    QStyledItemDelegate::paint(painter, opt, index);

    if (isEditingIndex(index))
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    const QColor gridColour = StructureGridItemDelegate::gridColour(opt.palette);

    const qreal px = devicePixelSize(painter);
    const qreal y = snapToDevicePixel(painter, opt.rect.top());
    const qreal x = snapToDevicePixel(painter, opt.rect.right() + 1.0) - px;
    painter->fillRect(QRectF(opt.rect.left(), y, opt.rect.width(), px), gridColour);
    if (const auto *view = qobject_cast<const QTreeView *>(opt.widget))
    {
        if (!view->indexBelow(index).isValid())
        {
            const qreal bottomY = snapToDevicePixel(painter, opt.rect.bottom() + 1.0) - px;
            painter->fillRect(QRectF(opt.rect.left(), bottomY, opt.rect.width(), px), gridColour);
        }
    }
    painter->fillRect(QRectF(x, opt.rect.top(), px, opt.rect.height()), gridColour);
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

QRect StructureGridItemDelegate::itemTextRect(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);

    const QWidget *widget = opt.widget;
    QStyle *style = widget ? widget->style() : QApplication::style();
    return style->subElementRect(QStyle::SE_ItemViewItemText, &opt, widget);
}
