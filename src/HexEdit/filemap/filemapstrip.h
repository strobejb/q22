#ifndef FILEMAP_FILEMAPSTRIP_H
#define FILEMAP_FILEMAPSTRIP_H

#include "filemap/filemapbuilder.h"

#include <QPointer>
#include <QVector>
#include <QWidget>

class HexView;
class QTimer;

namespace filemap
{

class FileMapEngine;

// Vertical whole-file overview docked in HexView's scrollbar gutter. Each
// pixel row is coloured by the dominant kind of content in that byte range.
// Offsets map linearly to height so the visible-window rectangle lines up
// with HexView's scroll position. Click jumps to the start of the region
// under the pointer; drag scrubs.
class FileMapStrip : public QWidget
{
    Q_OBJECT
public:
    explicit FileMapStrip(HexView *hexView, QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    static QColor kindColor(RegionKind kind, const QPalette &palette);
    static QString kindDisplayName(RegionKind kind);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void documentChanged();
    void startBuild();
    void applyMap(const FileMap &map);
    void rebuildRowCache();

    QRect contentRect() const;
    uint64_t documentSize() const;
    uint64_t offsetAtY(int y) const;
    int yForOffset(uint64_t offset) const;
    QBrush regionBrush(const Region &region) const;
    void showTooltipAt(const QPoint &pos, const QPoint &globalPos);
    void navigateTo(uint64_t offset, bool toTop);

    QPointer<HexView> m_hexView;
    FileMapEngine *m_engine = nullptr;
    QTimer *m_rebuildTimer = nullptr;

    FileMap m_map;
    bool m_hasMap = false;
    bool m_stale = true;

    // Per pixel row of contentRect(): the region covering most of that row's
    // bytes, and how many regions the row touches.
    QVector<int> m_rowRegion;
    QVector<int> m_rowRegionCount;

    uint64_t m_paintedTop = 0;
    uint64_t m_paintedVisible = 0;
    uint64_t m_paintedCursor = 0;

    bool m_pressed = false;
    bool m_dragging = false;
    int m_pressY = 0;
};

} // namespace filemap

#endif // FILEMAP_FILEMAPSTRIP_H
