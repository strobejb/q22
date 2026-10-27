#include "filemap/filemapstrip.h"

#include "filemap/filemapengine.h"
#include "HexView/hexview.h"

#include <QEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QToolTip>

#include <algorithm>

namespace filemap
{
namespace {

constexpr int kStripWidth      = 28;
constexpr int kDragThreshold   = 3;
constexpr int kRebuildDelayMs  = 400;
constexpr int kMinViewportRect = 4;

bool isDark(const QPalette &palette)
{
    return palette.color(QPalette::Window).lightness() < 128;
}

QColor blend(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

QString hexOffset(uint64_t value)
{
    return QStringLiteral("0x%1").arg(value, 0, 16).toUpper().replace(QLatin1String("0X"), QLatin1String("0x"));
}

QString sourceDescription(RegionSource source)
{
    switch (source)
    {
    case RegionSource::Structure: return FileMapStrip::tr("described by the format definition");
    case RegionSource::Heuristic: return FileMapStrip::tr("inferred");
    case RegionSource::Gap:       return FileMapStrip::tr("not described by the format definition");
    case RegionSource::Overlay:   return FileMapStrip::tr("overlay: after the last described byte");
    }
    return QString();
}

} // namespace

FileMapStrip::FileMapStrip(HexView *hexView, QWidget *parent)
    : QWidget(parent)
    , m_hexView(hexView)
    , m_engine(new FileMapEngine(this))
    , m_rebuildTimer(new QTimer(this))
{
    setObjectName(QStringLiteral("FileMapStrip"));
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
    setAttribute(Qt::WA_OpaquePaintEvent);

    m_rebuildTimer->setSingleShot(true);
    m_rebuildTimer->setInterval(kRebuildDelayMs);
    connect(m_rebuildTimer, &QTimer::timeout, this, &FileMapStrip::startBuild);
    connect(m_engine, &FileMapEngine::finished, this, &FileMapStrip::applyMap);

    if (m_hexView)
    {
        connect(m_hexView, &HexView::fileOpened, this, [this](const QString &) { documentChanged(); });
        connect(m_hexView, &HexView::lengthChanged, this, [this](size_w) { documentChanged(); });
        connect(m_hexView, &HexView::contentChanged, this, [this](size_w, size_w, uint) { documentChanged(); });
        // Every scroll path repaints the viewport, so watching its paints
        // keeps the window rectangle in sync without HexView scroll signals.
        m_hexView->viewport()->installEventFilter(this);
    }
}

QSize FileMapStrip::sizeHint() const
{
    return QSize(kStripWidth, 100);
}

QSize FileMapStrip::minimumSizeHint() const
{
    return QSize(kStripWidth, 20);
}

QColor FileMapStrip::kindColor(RegionKind kind, const QPalette &palette)
{
    const bool dark = isDark(palette);
    switch (kind)
    {
    case RegionKind::Metadata:   return dark ? QColor(0xE8, 0xB0, 0x4A) : QColor(0xD9, 0x9A, 0x2B);
    case RegionKind::Table:      return dark ? QColor(0xD1, 0x8C, 0x45) : QColor(0xB8, 0x74, 0x2E);
    case RegionKind::Text:       return dark ? QColor(0x5C, 0xC0, 0x6C) : QColor(0x3E, 0x9E, 0x4F);
    case RegionKind::Code:       return dark ? QColor(0x5B, 0x92, 0xF0) : QColor(0x2F, 0x6F, 0xDB);
    case RegionKind::Data:       return dark ? QColor(0x6F, 0x7A, 0x8D) : QColor(0x8B, 0x95, 0xA7);
    case RegionKind::Padding:    return blend(palette.color(QPalette::Base), palette.color(QPalette::Text), 0.08);
    case RegionKind::Compressed: return dark ? QColor(0xA8, 0x74, 0xE6) : QColor(0x8E, 0x4F, 0xCF);
    case RegionKind::Embedded:   return dark ? QColor(0x36, 0xB8, 0xB8) : QColor(0x1E, 0x9A, 0x9A);
    case RegionKind::Unknown:    return blend(palette.color(QPalette::Base), palette.color(QPalette::Text), 0.25);
    }
    return palette.color(QPalette::Mid);
}

QString FileMapStrip::kindDisplayName(RegionKind kind)
{
    switch (kind)
    {
    case RegionKind::Unknown:    return tr("Unknown");
    case RegionKind::Metadata:   return tr("Header / metadata");
    case RegionKind::Table:      return tr("Table");
    case RegionKind::Text:       return tr("Text");
    case RegionKind::Code:       return tr("Code");
    case RegionKind::Data:       return tr("Data");
    case RegionKind::Padding:    return tr("Padding");
    case RegionKind::Compressed: return tr("Compressed / encrypted");
    case RegionKind::Embedded:   return tr("Embedded file");
    }
    return QString();
}

// ---- document tracking -----------------------------------------------------

void FileMapStrip::documentChanged()
{
    m_stale = true;
    if (isVisible())
        m_rebuildTimer->start();
    update();
}

void FileMapStrip::startBuild()
{
    if (!m_hexView || !isVisible())
        return;
    m_stale = false;
    m_engine->build(m_hexView);
    update();
}

void FileMapStrip::applyMap(const FileMap &map)
{
    m_map = map;
    m_hasMap = true;
    rebuildRowCache();
    update();
}

void FileMapStrip::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // Nothing is built while hidden; catch up now.
    if (m_stale)
        m_rebuildTimer->start(0);
}

bool FileMapStrip::eventFilter(QObject *watched, QEvent *event)
{
    if (m_hexView && watched == m_hexView->viewport() && event->type() == QEvent::Paint && isVisible())
    {
        const uint64_t top = m_hexView->topVisibleOffset();
        const uint64_t visible = m_hexView->visibleByteCount();
        const uint64_t cursor = m_hexView->cursorOffset();
        if (top != m_paintedTop || visible != m_paintedVisible || cursor != m_paintedCursor)
            update();
    }
    return QWidget::eventFilter(watched, event);
}

void FileMapStrip::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)
        update();
    QWidget::changeEvent(event);
}

void FileMapStrip::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    rebuildRowCache();
}

// ---- geometry --------------------------------------------------------------

// Column 0 is the hairline against HexView's viewport.
QRect FileMapStrip::contentRect() const
{
    return QRect(1, 0, width() - 1, height());
}

uint64_t FileMapStrip::documentSize() const
{
    if (m_hasMap && m_map.fileSize > 0)
        return m_map.fileSize;
    return m_hexView ? uint64_t(m_hexView->size()) : 0;
}

// Doubles are exact to 2^53 bytes, far beyond what a pixel can resolve.
uint64_t FileMapStrip::offsetAtY(int y) const
{
    const uint64_t size = documentSize();
    if (size == 0 || height() <= 0)
        return 0;
    const double t = std::clamp(double(y) / double(height()), 0.0, 1.0);
    return std::min<uint64_t>(uint64_t(t * double(size)), size - 1);
}

int FileMapStrip::yForOffset(uint64_t offset) const
{
    const uint64_t size = documentSize();
    if (size == 0)
        return 0;
    return int(double(std::min(offset, size)) / double(size) * double(height()));
}

void FileMapStrip::rebuildRowCache()
{
    const int rows = std::max(0, height());
    m_rowRegion.fill(-1, rows);
    m_rowRegionCount.fill(0, rows);
    if (!m_hasMap || m_map.content.isEmpty() || rows == 0 || m_map.fileSize == 0)
        return;

    const QList<Region> &regions = m_map.content;
    const int count = int(regions.size());
    int first = 0;
    for (int y = 0; y < rows; ++y)
    {
        const uint64_t a = uint64_t(double(y) / rows * double(m_map.fileSize));
        uint64_t b = uint64_t(double(y + 1) / rows * double(m_map.fileSize));
        b = std::max(b, a + 1);

        while (first < count && regions[first].end <= a)
            ++first;

        int best = -1;
        uint64_t bestOverlap = 0;
        int touching = 0;
        for (int i = first; i < count && regions[i].start < b; ++i)
        {
            const uint64_t overlap = std::min(regions[i].end, b) - std::max(regions[i].start, a);
            ++touching;
            if (overlap > bestOverlap)
            {
                best = i;
                bestOverlap = overlap;
            }
        }
        m_rowRegion[y] = best;
        m_rowRegionCount[y] = touching;
    }
}

// ---- painting --------------------------------------------------------------

QBrush FileMapStrip::regionBrush(const Region &region) const
{
    const QPalette &pal = palette();
    QColor colour = kindColor(region.kind, pal);
    // Inferred kinds are paler than described ones so certainty reads at a glance.
    if (region.source == RegionSource::Heuristic)
        colour = blend(colour, pal.color(QPalette::Base), 0.35);
    return QBrush(colour);
}

void FileMapStrip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QPalette &pal = palette();
    const QColor base = pal.color(QPalette::Base);
    const QColor text = pal.color(QPalette::Text);
    p.fillRect(rect(), blend(base, text, 0.04));

    const QRect content = contentRect();

    if (m_hasMap && !m_map.content.isEmpty())
    {
        // Runs of rows with the same dominant region paint as one rect.
        const QList<Region> &regions = m_map.content;
        const QColor hatch = blend(base, text, 0.55);
        int runStart = 0;
        for (int y = 1; y <= m_rowRegion.size(); ++y)
        {
            if (y < m_rowRegion.size() && m_rowRegion[y] == m_rowRegion[runStart])
                continue;
            const int index = m_rowRegion[runStart];
            if (index >= 0)
            {
                const Region &region = regions[index];
                const QRect r(content.left(), runStart, content.width(), y - runStart);
                p.fillRect(r, regionBrush(region));
                // Bytes no definition describes are hatched: they are a guess
                // about content, and a pointer to gaps in the .strata file.
                if (region.source == RegionSource::Gap || region.source == RegionSource::Overlay)
                {
                    QBrush pattern(hatch, region.source == RegionSource::Overlay ? Qt::DiagCrossPattern
                                                                                 : Qt::BDiagPattern);
                    p.fillRect(r, pattern);
                }
            }
            runStart = y;
        }
    }

    // While (re)building, dim whatever is shown so stale results don't read as current.
    if (m_stale || m_engine->isBuilding())
    {
        QColor veil = base;
        veil.setAlpha(m_hasMap ? 110 : 200);
        p.fillRect(rect(), veil);
        if (!m_hasMap)
            p.fillRect(content, QBrush(blend(base, text, 0.25), Qt::BDiagPattern));
    }

    if (m_hexView && m_hexView->size() > 0)
    {
        m_paintedTop = m_hexView->topVisibleOffset();
        m_paintedVisible = m_hexView->visibleByteCount();
        m_paintedCursor = m_hexView->cursorOffset();

        int y1 = yForOffset(m_paintedTop);
        int y2 = yForOffset(m_paintedTop + m_paintedVisible);
        if (y2 - y1 < kMinViewportRect)
        {
            const int mid = (y1 + y2) / 2;
            y1 = mid - kMinViewportRect / 2;
            y2 = y1 + kMinViewportRect;
        }
        QColor fill = text;
        fill.setAlpha(isDark(pal) ? 60 : 45);
        QColor edge = text;
        edge.setAlpha(170);
        const QRect window(0, y1, width() - 1, y2 - y1);
        p.fillRect(window, fill);
        p.setPen(edge);
        p.setBrush(Qt::NoBrush);
        p.drawRect(window);

        const int cy = yForOffset(m_paintedCursor);
        p.setPen(QPen(pal.color(QPalette::Highlight), 1));
        p.drawLine(content.left(), cy, content.right(), cy);
    }

    // Hairline separating the strip from HexView.
    p.setPen(blend(base, text, 0.18));
    p.drawLine(0, 0, 0, height());
}

// ---- interaction -----------------------------------------------------------

void FileMapStrip::navigateTo(uint64_t offset, bool toTop)
{
    if (!m_hexView || m_hexView->size() == 0)
        return;
    offset = std::min<uint64_t>(offset, uint64_t(m_hexView->size()) - 1);
    m_hexView->setCurPos(size_w(offset));
    if (toTop)
        m_hexView->scrollTop(size_w(offset));
    else
        m_hexView->scrollCenter(size_w(offset));
    m_hexView->setFocus(Qt::MouseFocusReason);
    update();
}

void FileMapStrip::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return QWidget::mousePressEvent(event);
    m_pressed = true;
    m_dragging = false;
    m_pressY = event->position().toPoint().y();
    QToolTip::hideText();
}

void FileMapStrip::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    if (m_pressed)
    {
        if (!m_dragging && std::abs(pos.y() - m_pressY) >= kDragThreshold)
            m_dragging = true;
        if (m_dragging)
            navigateTo(offsetAtY(pos.y()), false);
        return;
    }
    showTooltipAt(pos, event->globalPosition().toPoint());
}

void FileMapStrip::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_pressed)
        return QWidget::mouseReleaseEvent(event);
    m_pressed = false;
    if (m_dragging)
    {
        m_dragging = false;
        return;
    }

    // A click goes to the start of the region under the pointer, so the map
    // works as structural navigation ("take me to .text"), not just a scrollbar.
    const int y = std::clamp(event->position().toPoint().y(), 0, std::max(0, height() - 1));
    const int index = y < m_rowRegion.size() ? m_rowRegion[y] : -1;
    if (index >= 0)
        navigateTo(m_map.content[index].start, true);
    else
        navigateTo(offsetAtY(y), false);
}

void FileMapStrip::leaveEvent(QEvent *event)
{
    QToolTip::hideText();
    QWidget::leaveEvent(event);
}

void FileMapStrip::showTooltipAt(const QPoint &pos, const QPoint &globalPos)
{
    if (!m_hasMap)
    {
        QToolTip::showText(globalPos, m_stale || m_engine->isBuilding() ? tr("Analysing file…") : QString(), this);
        return;
    }

    const int y = std::clamp(pos.y(), 0, std::max(0, height() - 1));
    const int index = y < m_rowRegion.size() ? m_rowRegion[y] : -1;
    if (index < 0)
    {
        QToolTip::hideText();
        return;
    }

    const Region &region = m_map.content[index];
    const QLocale locale;
    const QColor swatch = regionBrush(region).color();

    QString html = QStringLiteral("<table cellspacing=0 cellpadding=0><tr>"
                                  "<td><span style='background:%1'>&nbsp;&nbsp;&nbsp;</span>&nbsp;</td>"
                                  "<td><b>%2</b></td></tr></table>")
                       .arg(swatch.name(), kindDisplayName(region.kind).toHtmlEscaped());

    if (!region.label.isEmpty())
        html += QStringLiteral("%1<br>").arg(region.label.toHtmlEscaped());
    html += QStringLiteral("%1 – %2 &nbsp;(%3)<br>")
                .arg(hexOffset(region.start), hexOffset(region.end), locale.formattedDataSize(qint64(region.end - region.start)));
    html += QStringLiteral("<i>%1</i>").arg(sourceDescription(region.source).toHtmlEscaped());

    // Name the enclosing structural section when it adds something.
    const uint64_t offset = std::max(region.start, offsetAtY(y));
    for (auto it = m_map.structure.crbegin(); it != m_map.structure.crend(); ++it)
        if (!it->dynamic && it->start <= offset && offset < it->end)
        {
            if (it->label != region.label)
                html += QStringLiteral("<br>%1 %2").arg(tr("in"), it->label.toHtmlEscaped());
            break;
        }

    if (!region.path.isEmpty() && region.path != region.label)
        html += QStringLiteral("<br><span style='color:%1'>%2</span>")
                    .arg(palette().color(QPalette::Disabled, QPalette::Text).name(), region.path.toHtmlEscaped());

    const int others = y < m_rowRegionCount.size() ? m_rowRegionCount[y] - 1 : 0;
    if (others > 0)
        html += QStringLiteral("<br><span style='color:%1'>%2</span>")
                    .arg(palette().color(QPalette::Disabled, QPalette::Text).name(),
                         tr("+%n smaller region(s) at this zoom", nullptr, others));

    QToolTip::showText(globalPos, html, this);
}

} // namespace filemap
