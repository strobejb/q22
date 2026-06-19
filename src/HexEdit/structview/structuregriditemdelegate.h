#ifndef STRUCTVIEW_STRUCTUREGRIDITEMDELEGATE_H
#define STRUCTVIEW_STRUCTUREGRIDITEMDELEGATE_H

#include <QStyledItemDelegate>
#include <QColor>
#include <QPalette>
#include <QPersistentModelIndex>
#include <QRect>

class QAbstractItemModel;
class QEvent;

class StructureGridItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit StructureGridItemDelegate(QObject *parent = nullptr);

    static QColor gridColour(const QPalette &palette);

    void paint(QPainter *painter,
               const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QWidget *createEditor(QWidget *parent,
                          const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;
    bool editorEvent(QEvent *event,
                     QAbstractItemModel *model,
                     const QStyleOptionViewItem &option,
                     const QModelIndex &index) override;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override;
    void updateEditorGeometry(QWidget *editor,
                              const QStyleOptionViewItem &option,
                              const QModelIndex &index) const override;
    void destroyEditor(QWidget *editor, const QModelIndex &index) const override;

private:
    bool isEditingIndex(const QModelIndex &index) const;
    void drawGridLines(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const;
    bool paintAlignedName(QPainter *painter, QStyleOptionViewItem *option, const QModelIndex &index) const;
    QRect itemTextRect(const QStyleOptionViewItem &option, const QModelIndex &index) const;

    mutable QPersistentModelIndex m_editingIndex;
    mutable QPersistentModelIndex m_popupIndex;
};

#endif // STRUCTVIEW_STRUCTUREGRIDITEMDELEGATE_H
