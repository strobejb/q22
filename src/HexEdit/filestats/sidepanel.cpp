#include "filestats/sidepanel.h"

#include "HexView/hexview.h"
#include "chrome/dialog-chrome.h"
#include "combos/menucombobox.h"
#include "filestats/entropy.h"
#include "filestats/resizegrip.h"
#include "filestats/widgets.h"
#include "settings/scrollhintoverlay.h"
#include "settings/settings.h"
#include "settings/settingscard.h"
#include "theme.h"

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBrush>
#include <QByteArray>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCryptographicHash>
#include <QCursor>
#include <QDesktopServices>
#include <QEnterEvent>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QPointer>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionHeader>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariantMap>
#include <QtMath>

#include <algorithm>
#include <array>
#include <functional>
#include <utility>

using namespace filestats;

static constexpr int  kFileInfoPaneMinWidth         = 280;
static constexpr int  kFileInfoPaneMaxWidth         = 720;
static constexpr int  kFileInfoPaneAnimMs           = 220;
static constexpr bool kAutoStartPanelOperations     = true;
FilePropertiesPanel::FilePropertiesPanel(HexView *hexView, QWidget *parent) : QDialog(parent), m_hexView(hexView)
{
    m_sectionOrder = {SectionId::Properties, SectionId::Checksums, SectionId::Strings, SectionId::Entropy};

    setWindowTitle(tr("File Information"));
    setSizeGripEnabled(false);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, pal.color(QPalette::Window));
    setPalette(pal);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_scrollArea->verticalScrollBar()->setFocusPolicy(Qt::NoFocus);
    m_scrollArea->viewport()->setAutoFillBackground(false);
    m_scrollArea->setStyleSheet(QStringLiteral(R"(
        QScrollArea {
            background: transparent;
            border: none;
        }
    )"));
    ScrollHintOverlay::install(m_scrollArea);
    m_scrollArea->verticalScrollBar()->setProperty("filePropertiesScrollBar", true);
    m_scrollArea->verticalScrollBar()->style()->unpolish(m_scrollArea->verticalScrollBar());
    m_scrollArea->verticalScrollBar()->style()->polish(m_scrollArea->verticalScrollBar());

    m_content = new QWidget(m_scrollArea);
    m_content->setMinimumWidth(0);
    m_content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(kSectionHeaderOuterMargin, kContentMargin, kSectionHeaderOuterMargin, 0);
    contentLayout->setSpacing(0);

    buildPropertiesSection(m_content, contentLayout);

    m_interSectionGaps.append(new QSpacerItem(0, kGroupTopGap, QSizePolicy::Minimum, QSizePolicy::Fixed));
    contentLayout->addSpacerItem(m_interSectionGaps.last());
    buildChecksumSection(m_content, contentLayout);

    m_interSectionGaps.append(new QSpacerItem(0, kGroupTopGap, QSizePolicy::Minimum, QSizePolicy::Fixed));
    contentLayout->addSpacerItem(m_interSectionGaps.last());
    buildStringsSection(m_content, contentLayout);

    m_interSectionGaps.append(new QSpacerItem(0, kGroupTopGap, QSizePolicy::Minimum, QSizePolicy::Fixed));
    contentLayout->addSpacerItem(m_interSectionGaps.last());
    buildEntropySection(m_content, contentLayout);

    contentLayout->addStretch();

    m_scrollArea->setWidget(m_content);
    root->addWidget(m_scrollArea, 1);

    m_stickyHeader = new SectionHeader(QString(), m_scrollArea->viewport());
    m_stickyHeader->hide();
    m_stickyHeader->raise();

    m_dropIndicator = new QWidget(m_scrollArea->viewport());
    m_dropIndicator->setFixedHeight(4);
    m_dropIndicator->setAutoFillBackground(true);
    {
        QPalette ip = m_dropIndicator->palette();
        ip.setColor(QPalette::Window, palette().mid().color());
        m_dropIndicator->setPalette(ip);
    }
    m_dropIndicator->hide();

    for (SectionId s : m_sectionOrder)
    {
        PanelSection *ps = sectionFor(s);
        if (ps && ps->header)
        {
            if (ps->resize.target)
            {
                ps->header->setDoubleClickCallback(
                    [this, s]()
                    {
                        if (AppSettings::prefSectionHeaderDoubleClick())
                            toggleSectionFullExpand(s);
                    });
            }
            else
            {
                ps->header->setDoubleClickCallback(
                    [this, s]()
                    {
                        if (!AppSettings::prefSectionHeaderDoubleClick())
                            return;
                        QTimer::singleShot(0, this,
                                           [this, s]()
                                           {
                                               PanelSection *sec = sectionFor(s);
                                               if (!sec || !sec->header || !m_scrollArea || !m_content
                                                   || sec->collapsed)
                                                   return;
                                               const int y = sec->header->mapTo(m_content, QPoint(0, 0)).y();
                                               ++m_programmaticScrollDepth;
                                               m_scrollArea->verticalScrollBar()->setValue(
                                                   qMax(0, y - kContentMargin));
                                               QTimer::singleShot(
                                                   0, this, [this] { --m_programmaticScrollDepth; });
                                           });
                    });
            }
        }
        sectionFor(s)->header->setDragCallbacks(
            [this, s](QPoint p)
            {
                onDragStarted(s, p);
            },
            [this](QPoint p)
            {
                onDragMoved(p);
            },
            [this, s](QPoint p)
            {
                onDragEnded(s, p);
            });
    }
    connect(m_scrollArea->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this]()
            {
                updateStickyHeader();
                if (!m_programmaticScrollDepth && m_hasExpandedSection)
                {
                    m_hasExpandedSection = false;
                    if (PanelSection *s = sectionFor(m_expandedSectionId))
                        if (s->header)
                            s->header->setSectionExpanded(false);
                }
            });
    connect(m_scrollArea->verticalScrollBar(), &QScrollBar::rangeChanged, this,
            [this]()
            {
                updateStickyHeader();
            });
    QTimer::singleShot(0, this, &FilePropertiesPanel::updateStickyHeader);

    setMinimumWidth(260);
    resetForCurrentDocument();
    // Collapse without animation: the panel hasn't been shown yet, so
    // animateSectionBody's isVisible() guard would bail out early and leave all
    // bodies visible when the panel first appears.
    setSectionCollapsed(SectionId::Properties, true, false);
    setSectionCollapsed(SectionId::Checksums, true, false);
    setSectionCollapsed(SectionId::Strings, true, false);
}

FilePropertiesPanel::~FilePropertiesPanel()
{
    ++m_checksumState.generation;
    ++m_stringsState.generation;
    if (m_checksumState.cancel)
        m_checksumState.cancel->store(true);
    if (m_checksumState.pause)
        m_checksumState.pause->wake();
    if (m_stringsState.cancel)
        m_stringsState.cancel->store(true);
    if (m_stringsState.pause)
        m_stringsState.pause->wake();
    clearStringExportTemp();
}

bool FilePropertiesPanel::shouldAutoStartOperations() const
{
    return kAutoStartPanelOperations;
}

void FilePropertiesPanel::exportStringResults()
{
    if (!m_stringsList || m_stringsList->topLevelItemCount() == 0)
        return;

    const bool  useNativeFileDialogs = AppSettings::prefNativeFileDialogs();
    QWidget    *dialogParent         = window();
    QFileDialog dlg(dialogParent ? dialogParent : this, tr("Export Strings"));
    dlg.setOption(QFileDialog::DontUseNativeDialog, !useNativeFileDialogs);
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    dlg.setNameFilter(tr("Text files (*.txt);;All files (*)"));
    dlg.selectFile(QStringLiteral("strings.txt"));
    dlg.setDefaultSuffix(QStringLiteral("txt"));
    if (!useNativeFileDialogs)
    {
        installThemedFileDialogComboPopups(&dlg);
        installDialogChrome(&dlg);
    }

    if ((useNativeFileDialogs ? dlg.exec() : execCentered(&dlg)) != QDialog::Accepted)
        return;

    const QString path = dlg.selectedFiles().value(0);
    if (path.isEmpty())
        return;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    if (m_stringsExportTempComplete && !m_stringsExportTempPath.isEmpty())
    {
        QFile temp(m_stringsExportTempPath);
        if (!temp.open(QIODevice::ReadOnly))
        {
            file.cancelWriting();
            return;
        }
        while (!temp.atEnd())
        {
            const QByteArray chunk = temp.read(1024 * 1024);
            if (chunk.isEmpty())
                break;
            file.write(chunk);
        }
    }
    else
    {
        QTextStream out(&file);
        const bool  prefixHexOffset = m_prefixHexOffsetAction && m_prefixHexOffsetAction->isChecked();
        for (int i = 0; i < m_stringsList->topLevelItemCount(); ++i)
        {
            if (QTreeWidgetItem *item = m_stringsList->topLevelItem(i))
            {
                if (item->data(0, Qt::UserRole + 2).toBool())
                    continue;
                if (prefixHexOffset)
                {
                    const qulonglong offset = item->data(0, Qt::UserRole).toULongLong();
                    out << QStringLiteral("%1 ").arg(offset, 8, 16, QLatin1Char('0')).toUpper();
                }
                out << item->text(0) << '\n';
            }
        }
    }
    file.commit();
}

void FilePropertiesPanel::setChecksumProgressTitle(int value)
{
    m_checksumState.progress = value;
    if (!m_checksumHeader)
        return;
    const int pct = qBound(0, value, 1000) / 10;
    m_checksumHeader->setTitle((isSectionCollapsed(SectionId::Checksums) || m_checksumState.pausedByCollapse)
                                   ? tr("Checksums (%1% - paused)").arg(pct)
                                   : tr("Checksums (%1%)").arg(pct));
    syncStickyHeader();
}

void FilePropertiesPanel::setStringsProgressTitle(int value)
{
    if (!m_stringsHeader)
        return;
    const int pct = qBound(0, value, 1000) / 10;
    m_stringsHeader->setTitle((isSectionCollapsed(SectionId::Strings) || m_stringsState.pausedByCollapse)
                                  ? tr("Strings (%1% - paused)").arg(pct)
                                  : tr("Strings (%1%)").arg(pct));
    syncStickyHeader();
}

void FilePropertiesPanel::resetChecksumTitle()
{
    if (m_checksumHeader)
        m_checksumHeader->setTitle(tr("Checksums"));
    syncStickyHeader();
}

void FilePropertiesPanel::resetStringsTitle()
{
    if (m_stringsHeader)
        m_stringsHeader->setTitle(tr("Strings"));
    syncStickyHeader();
}

void FilePropertiesPanel::updateStringsOffsetColumnWidth()
{
    if (!m_stringsList)
        return;

    const qulonglong   fileSize  = m_hexView ? static_cast<qulonglong>(m_hexView->size()) : 0;
    const qulonglong   maxOffset = fileSize > 0 ? fileSize - 1 : 0;
    const QString      sample    = QStringLiteral("%1").arg(maxOffset, 8, 16, QLatin1Char('0')).toUpper();
    const QFontMetrics fm(m_stringsList->font());
    const int          textWidth              = qMax(fm.horizontalAdvance(sample), fm.horizontalAdvance(tr("Offset")));
    constexpr int      kItemHorizontalPadding = 12; // QTreeWidget::item padding: 3px 6px
    constexpr int      kStyleReserve          = 28; // delegate/header focus margins + sort indicator
    const int          offsetColumnWidth      = textWidth + kItemHorizontalPadding + kStyleReserve;
    if (m_stringsList->columnWidth(1) != offsetColumnWidth)
        m_stringsList->setColumnWidth(1, offsetColumnWidth);
}

void FilePropertiesPanel::showSection(SectionId section)
{
    setSectionCollapsed(section, false);
}

void FilePropertiesPanel::setPanelFullyOpened(bool opened)
{
    m_panelFullyOpened = opened;
    if (opened)
        for (SectionId section : std::as_const(m_sectionOrder))
            emitSectionReadyIfPossible(section);
}

void FilePropertiesPanel::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        recolorToolButtons(this);
}

void FilePropertiesPanel::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    updateStickyHeader();
}

void FilePropertiesPanel::animateSectionBody(QWidget *body, bool collapse, bool animate)
{
    if (!body)
        return;

    const QString animName = QStringLiteral("sectionBodyAnim");
    if (auto *existing = body->findChild<QPropertyAnimation *>(animName))
        existing->stop();

    if (!animate)
    {
        // Skip animation: snap directly to the target state.  Used when restoring
        // sections after a reorder so the freshly-rebuilt layout isn't asked to
        // drive a maximumHeight animation before it has settled.
        body->setMaximumHeight(QWIDGETSIZE_MAX);
        if (collapse)
        {
            body->hide();
        }
        else
        {
            body->show();
            if (QWidget *p = body->parentWidget())
                if (QLayout *l = p->layout())
                    l->activate();
        }
        return;
    }

    // These bodies contain dynamic content: operation strips appear/disappear,
    // labels update while background work finishes, and sections can be
    // re-ordered mid-interaction.  Animating maximumHeight makes that live
    // layout state part of the animation state, which can strand an expanded
    // body at height 0.  Keep the visual state deterministic: collapsed means
    // hidden, expanded means shown and unconstrained.
    if (collapse)
    {
        body->setMaximumHeight(QWIDGETSIZE_MAX);
        body->hide();
    }
    else
    {
        body->setMaximumHeight(QWIDGETSIZE_MAX);
        body->show();
        body->updateGeometry();
        if (QWidget *p = body->parentWidget())
            if (QLayout *l = p->layout())
            {
                l->invalidate();
                l->activate();
            }
    }
}

void FilePropertiesPanel::setSectionCollapsed(SectionId sectionId, bool collapsed, bool animate)
{
    if (collapsed && m_hasExpandedSection && m_expandedSectionId == sectionId)
        clearSectionFullExpand(sectionId);

    PanelSection *section = sectionFor(sectionId);
    if (!section)
        return;

    const bool wasCollapsed = section->collapsed;
    section->collapsed      = collapsed;
    animateSectionBody(section->body, collapsed, animate);
    if (section->operation)
        section->operation->setCollapsed(collapsed);
    if (section->headerGap)
        section->headerGap->changeSize(0, collapsed ? 0 : kHeaderControlGap, QSizePolicy::Minimum, QSizePolicy::Fixed);
    if (section->header)
        section->header->setCollapsed(collapsed);
    if (section->onCollapsedChanged)
        section->onCollapsedChanged(collapsed);
    updateInterSectionGaps();
    if (!collapsed)
    {
        repairExpandedSectionGeometry(sectionId);
        settleContentLayout();
        requestSectionLayoutRefresh(sectionId);
    }
    if (wasCollapsed && !collapsed)
    {
        emit sectionExpanded(sectionId);
        emitSectionReadyIfPossible(sectionId);
        scheduleResizableSectionRepair(sectionId);
        QTimer::singleShot(170, this,
                           [this, sectionId]()
                           {
                               repairExpandedSectionGeometry(sectionId);
                               settleContentLayout();
                           });
        QTimer::singleShot(0, this,
                           [this, sectionId]()
                           {
                               // Skip when a full-expand is in progress — it owns scrolling.
                               if (m_hasExpandedSection || !m_scrollArea || !m_content)
                                   return;
                               PanelSection *s = sectionFor(sectionId);
                               if (!s || !s->header || !s->body || s->collapsed)
                                   return;

                               const int vpH       = m_scrollArea->viewport()->height();
                               const int curScroll = m_scrollArea->verticalScrollBar()->value();

                               // Use content coords — reliable after settleContentLayout.
                               const int headerCY    = s->header->mapTo(m_content, QPoint(0, 0)).y();
                               const int headerVpY   = headerCY - curScroll;

                               // Only act when the header is in the lower half of the viewport.
                               if (headerVpY <= vpH / 2)
                                   return;

                               const int bodyBottomCY  = s->body->mapTo(m_content,
                                                                          QPoint(0, s->body->height())).y();
                               // Only scroll if the expanded body extends below the viewport.
                               if (bodyBottomCY - curScroll <= vpH)
                                   return;

                               // Scroll so the section header sits just below the top,
                               // leaving exactly one header-height of room for the
                               // section above to remain visible.
                               const int targetScroll = qMax(0, headerCY - kSectionHeaderHeight);
                               if (targetScroll > curScroll)
                                   m_scrollArea->verticalScrollBar()->setValue(targetScroll);
                           });
    }
    updateStickyHeader();
    QTimer::singleShot(0, this, &FilePropertiesPanel::updateStickyHeader);
}

void FilePropertiesPanel::emitSectionReadyIfPossible(SectionId section)
{
    if (!m_panelFullyOpened)
        return;

    if (!isSectionCollapsed(section))
    {
        emit sectionReady(section);
        if (PanelSection *panelSection = sectionFor(section))
            if (panelSection->onExpanded)
                panelSection->onExpanded();
    }
}

void FilePropertiesPanel::syncStickyHeader()
{
    if (!m_scrollArea || !m_stickyHeader || m_sectionOrder.isEmpty())
        return;

    const int headerWidth = qMax(1, m_scrollArea->viewport()->width() - 2 * kSectionHeaderOuterMargin);

    // Hide if the first header is still visible above the fold
    const PanelSection *first = sectionFor(m_sectionOrder.first());
    if (!first || !first->header)
        return;
    const int firstY = first->header->mapTo(m_scrollArea->viewport(), QPoint(0, 0)).y();
    if (firstY > 0)
    {
        m_stickyHeader->hide();
        return;
    }

    // Find the bottommost section that has scrolled above the viewport.
    // When a section is fully expanded, skip collapsed sections above it —
    // they are navigation headers only and must not appear as sticky headers.
    SectionId activeSection = m_sectionOrder.first();
    int       nextHeaderY   = kSectionHeaderHeight;
    for (int i = 0; i < m_sectionOrder.size(); ++i)
    {
        const PanelSection *section = sectionFor(m_sectionOrder[i]);
        if (!section || !section->header)
            continue;
        if (m_hasExpandedSection && m_sectionOrder[i] != m_expandedSectionId && section->collapsed)
            continue;
        SectionHeader *h = section->header;
        const int      y = h->mapTo(m_scrollArea->viewport(), QPoint(0, 0)).y();
        if (y <= 0)
        {
            activeSection = m_sectionOrder[i];
            if (i + 1 < m_sectionOrder.size())
            {
                if (const PanelSection *next = sectionFor(m_sectionOrder[i + 1]))
                    if (next->header)
                        nextHeaderY = next->header->mapTo(m_scrollArea->viewport(), QPoint(0, 0)).y();
            }
            else
            {
                nextHeaderY = kSectionHeaderHeight;
            }
        }
    }
    // If only the expanded section is relevant but it hasn't scrolled off the
    // top yet, there is nothing to show as a sticky header.
    if (m_hasExpandedSection && activeSection != m_expandedSectionId)
    {
        m_stickyHeader->hide();
        return;
    }

    const PanelSection *active = sectionFor(activeSection);
    if (!active || !active->header)
        return;
    m_stickyHeader->setTitle(active->header->title());
    m_stickyHeader->setCollapsed(active->collapsed);
    m_stickyHeader->setClickedCallback(
        [this, activeSection]()
        {
            setSectionCollapsed(activeSection, !isSectionCollapsed(activeSection));
        });
    const bool hasResizeTarget = active->resize.target != nullptr;
    m_stickyHeader->setExpandable(hasResizeTarget);
    if (hasResizeTarget)
    {
        m_stickyHeader->setExpandCallback([this, activeSection]() { toggleSectionFullExpand(activeSection); });
        m_stickyHeader->setSectionExpanded(m_hasExpandedSection && m_expandedSectionId == activeSection);
    }
    else
    {
        m_stickyHeader->setSectionExpanded(false);
    }

    const int y = qMin(0, nextHeaderY - kSectionHeaderHeight);
    m_stickyHeader->setGeometry(kSectionHeaderOuterMargin, y, headerWidth, kSectionHeaderHeight);
    m_stickyHeader->show();
    m_stickyHeader->raise();
}

void FilePropertiesPanel::updateStickyHeader()
{
    syncStickyHeader();
}

void FilePropertiesPanel::registerPanelSection(const PanelSectionSpec &spec)
{
    PanelSection section;
    section.id                        = spec.id;
    section.title                     = spec.title;
    section.header                    = spec.header;
    section.body                      = spec.body;
    section.headerGap                 = spec.headerGap;
    section.operation                 = spec.operation;
    section.onExpanded                = spec.onExpanded;
    section.onCollapsedChanged        = spec.onCollapsedChanged;
    section.onRefreshDocumentState    = spec.onRefreshDocumentState;
    section.onResetForCurrentDocument = spec.onResetForCurrentDocument;
    if (spec.resizableTarget)
    {
        section.resize.target        = spec.resizableTarget;
        section.resize.minHeight     = spec.minResizableHeight;
        section.resize.currentHeight = spec.minResizableHeight;
        spec.resizableTarget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        spec.resizableTarget->setFixedHeight(spec.minResizableHeight);
        spec.header->setExpandable(true);
        spec.header->setExpandCallback(
            [this, id = spec.id]()
            {
                toggleSectionFullExpand(id);
            });
    }
    m_sections.append(section);
}

FilePropertiesPanel::PanelSection *FilePropertiesPanel::sectionFor(SectionId section)
{
    for (PanelSection &candidate : m_sections)
        if (candidate.id == section)
            return &candidate;
    return nullptr;
}

const FilePropertiesPanel::PanelSection *FilePropertiesPanel::sectionFor(SectionId section) const
{
    for (const PanelSection &candidate : m_sections)
        if (candidate.id == section)
            return &candidate;
    return nullptr;
}

bool FilePropertiesPanel::isSectionCollapsed(SectionId section) const
{
    if (const PanelSection *panelSection = sectionFor(section))
        return panelSection->collapsed;
    return true;
}

void FilePropertiesPanel::updateInterSectionGaps()
{
    for (int i = 0; i + 1 < m_sectionOrder.size(); ++i)
    {
        const bool prevCollapsed = isSectionCollapsed(m_sectionOrder[i]);
        const int  gapSize       = prevCollapsed ? kHeaderControlGap : kGroupTopGap;
        m_interSectionGaps[i]->changeSize(0, gapSize, QSizePolicy::Minimum, QSizePolicy::Fixed);
    }
    if (m_content && m_content->layout())
        m_content->layout()->invalidate();
}

void FilePropertiesPanel::rebuildSectionLayout()
{
    // Stop any in-flight body animations and snap to a clean hidden state so the
    // subsequent re-expand always starts from a known baseline (fixes missing
    // content after reorder and the related narrow-width disappearance bug).
    const QString animName = QStringLiteral("sectionBodyAnim");
    for (PanelSection &section : m_sections)
    {
        if (QWidget *body = section.body)
        {
            if (auto *anim = body->findChild<QPropertyAnimation *>(animName))
                anim->stop();
            body->setMaximumHeight(QWIDGETSIZE_MAX);
            body->setVisible(!section.collapsed);
            if (!section.collapsed)
                applyResizableSectionHeight(section.id);
            body->updateGeometry();
        }
    }

    auto *layout = static_cast<QVBoxLayout *>(m_content->layout());
    while (layout->count())
        layout->takeAt(0);

    for (int i = 0; i < m_sectionOrder.size(); ++i)
    {
        PanelSection *section = sectionFor(m_sectionOrder[i]);
        if (!section)
            continue;
        if (i > 0)
            layout->addSpacerItem(m_interSectionGaps[i - 1]);
        layout->addWidget(section->header);
        layout->addSpacerItem(section->headerGap);
        if (auto *op = section->operation)
            layout->addWidget(op->widget());
        layout->addWidget(section->body);
    }
    layout->addStretch();
    updateInterSectionGaps();
    layout->activate(); // headers must be at final positions before releaseMouse() fires hover events
    settleContentLayout();
}

bool FilePropertiesPanel::applyResizableSectionHeight(SectionId sectionId)
{
    PanelSection *section = sectionFor(sectionId);
    if (!section)
        return false;
    SectionResizeState &state = section->resize;
    if (!state.target)
        return false;

    const int height = state.currentHeight > 0 ? state.currentHeight : state.minHeight;
    state.target->setFixedHeight(height);
    state.target->resize(state.target->width(), height);
    state.target->updateGeometry();

    if (QWidget *stack = state.target->parentWidget())
    {
        stack->updateGeometry();
        if (QLayout *layout = stack->layout())
        {
            layout->invalidate();
            layout->activate();
        }
    }

    QWidget *body = section->body;
    if (!body)
        return true;
    body->updateGeometry();
    if (QLayout *layout = body->layout())
    {
        body->setMinimumHeight(0);
        body->setMaximumHeight(QWIDGETSIZE_MAX);
        layout->invalidate();
        layout->activate();
        const int bodyHeight = layout->sizeHint().height();
        body->setFixedHeight(bodyHeight);
        body->resize(body->width(), bodyHeight);
        body->updateGeometry();
    }
    return true;
}

void FilePropertiesPanel::scheduleResizableSectionRepair(SectionId sectionId)
{
    PanelSection *section = sectionFor(sectionId);
    if (!section || !section->resize.target)
        return;

    auto repair = [this, sectionId]()
    {
        if (isSectionCollapsed(sectionId))
            return;
        applyResizableSectionHeight(sectionId);
        settleContentLayout();
        updateStickyHeader();
    };
    QTimer::singleShot(0, this, repair);
    QTimer::singleShot(16, this, repair);
}

void FilePropertiesPanel::resizeSection(SectionId sectionId, int dy)
{
    PanelSection *section = sectionFor(sectionId);
    if (!section)
        return;
    SectionResizeState &state = section->resize;
    if (!state.target)
        return;

    const int current   = state.currentHeight > 0 ? state.currentHeight : state.target->height();
    const int maxHeight = qMax(state.minHeight, height() - kSectionHeaderHeight * 2);
    const int newHeight = qBound(state.minHeight, current + dy, maxHeight);
    if (newHeight == current)
        return;

    state.currentHeight = newHeight;

    if (m_hasExpandedSection && m_expandedSectionId == sectionId)
    {
        m_hasExpandedSection = false;
        if (section->header)
            section->header->setSectionExpanded(false);
    }

    applyResizableSectionHeight(sectionId);
    rebuildSectionLayout();

    settleContentLayout();
    updateStickyHeader();
}

void FilePropertiesPanel::repairExpandedSectionGeometry(SectionId sectionId)
{
    if (isSectionCollapsed(sectionId))
        return;

    PanelSection *section = sectionFor(sectionId);
    QWidget      *body    = section ? section->body : nullptr;
    if (!body)
        return;

    const QString animName = QStringLiteral("sectionBodyAnim");
    if (auto *anim = body->findChild<QPropertyAnimation *>(animName))
        if (anim->state() == QAbstractAnimation::Running)
            return;

    body->show();
    if (!applyResizableSectionHeight(sectionId))
    {
        body->setMaximumHeight(QWIDGETSIZE_MAX);
        body->updateGeometry();
    }

    if (QWidget *parent = body->parentWidget())
    {
        if (QLayout *layout = parent->layout())
        {
            layout->invalidate();
            layout->activate();
        }
    }

    settleContentLayout();

    if (body->height() == 0)
        QTimer::singleShot(0, this,
                           [this, sectionId]()
                           {
                               if (isSectionCollapsed(sectionId))
                                   return;
                               if (PanelSection *section = sectionFor(sectionId))
                               {
                                   QWidget *body = section->body;
                                   if (!body)
                                       return;
                                   body->show();
                                   if (!applyResizableSectionHeight(sectionId))
                                   {
                                       body->setMaximumHeight(QWIDGETSIZE_MAX);
                                       body->updateGeometry();
                                   }
                                   if (QWidget *parent = body->parentWidget())
                                       if (QLayout *layout = parent->layout())
                                       {
                                           layout->invalidate();
                                           layout->activate();
                                       }
                                   settleContentLayout();
                               }
                           });
}

void FilePropertiesPanel::onDragStarted(SectionId s, QPoint /*globalPos*/)
{
    m_draggedSection  = s;
    m_draggingSection = true;

    for (PanelSection &section : m_sections)
        section.preDragCollapsed = section.collapsed;
    m_dragSectionsCollapsed = false;

    updateDropIndicator(QCursor::pos());
}

void FilePropertiesPanel::onDragMoved(QPoint globalPos)
{
    if (m_draggingSection)
    {
        collapseSectionsForDrag();
        updateDropIndicator(globalPos);
    }
}

void FilePropertiesPanel::onDragEnded(SectionId /*s*/, QPoint globalPos)
{
    const bool sectionsWereCollapsedForDrag = m_dragSectionsCollapsed;
    m_draggingSection                       = false;
    m_dragSectionsCollapsed                 = false;
    for (SectionId s : m_sectionOrder)
        if (PanelSection *section = sectionFor(s))
            section->header->setDragTarget(false);
    if (m_dropIndicator)
        m_dropIndicator->hide();

    // Find the section whose header the mouse is over, using the same logic as
    // updateDropIndicator.  Swap the dragged section with that target so the
    // dragged panel lands exactly where the user pointed, with the displaced
    // panel filling the gap — simpler and more predictable than an insert-index
    // approach, which has a large no-op zone around the middle panel.
    SectionId targetSection = m_sectionOrder.last();
    if (m_content && !m_sectionOrder.isEmpty())
    {
        const int contentY = m_content->mapFromGlobal(globalPos).y();
        for (SectionId s : m_sectionOrder)
        {
            PanelSection *section = sectionFor(s);
            if (!section || !section->header)
                continue;
            const int bottom = section->header->mapTo(m_content, QPoint(0, 0)).y() + kSectionHeaderHeight;
            if (contentY < bottom)
            {
                targetSection = s;
                break;
            }
        }
    }

    const bool isNoOp = (targetSection == m_draggedSection);

    if (!isNoOp)
    {
        const int di = m_sectionOrder.indexOf(m_draggedSection);
        const int ti = m_sectionOrder.indexOf(targetSection);
        // Remove the dragged section then insert it at the target's original index.
        // When di < ti the removal shifts the array down by one, so inserting at the
        // original ti places the dragged item at exactly the target's old position,
        // with every other item sliding to fill the gap — no explicit adjustment needed.
        m_sectionOrder.removeAt(di);
        m_sectionOrder.insert(ti, m_draggedSection);
        rebuildSectionLayout();
    }

    if (!isNoOp)
    {
        // After a reorder: leave everything collapsed except the dragged panel,
        // and only re-expand that one if it was open before the drag started.
        // This avoids restoring animation state across a freshly-rebuilt layout.
        for (SectionId s : m_sectionOrder)
        {
            PanelSection *section = sectionFor(s);
            if (section)
                setSectionCollapsed(s, s == m_draggedSection ? section->preDragCollapsed : true, false);
        }
    }
    else if (sectionsWereCollapsedForDrag)
    {
        // No reorder: restore all panels to their pre-drag state with animation.
        for (SectionId s : m_sectionOrder)
        {
            if (PanelSection *section = sectionFor(s))
                if (!section->preDragCollapsed)
                    setSectionCollapsed(s, false);
        }
    }

    scheduleSectionHeaderHoverSync();
    updateStickyHeader();
}

void FilePropertiesPanel::collapseSectionsForDrag()
{
    if (m_dragSectionsCollapsed)
        return;

    m_dragSectionsCollapsed = true;
    bool anyWasExpanded     = false;
    for (const PanelSection &section : m_sections)
        anyWasExpanded = anyWasExpanded || !section.preDragCollapsed;

    for (SectionId s : m_sectionOrder)
        setSectionCollapsed(s, true);

    QTimer::singleShot(anyWasExpanded ? 130 : 0, this,
                       [this]()
                       {
                           if (m_draggingSection)
                               updateDropIndicator(QCursor::pos());
                       });
}

void FilePropertiesPanel::updateDropIndicator(QPoint globalPos)
{
    if (!m_draggingSection)
        return;

    for (SectionId s : m_sectionOrder)
        if (PanelSection *section = sectionFor(s))
            section->header->setDragTarget(false);

    if (!m_content || m_sectionOrder.isEmpty())
        return;

    const int contentY = m_content->mapFromGlobal(globalPos).y();
    SectionId target   = m_sectionOrder.last();
    for (SectionId s : m_sectionOrder)
    {
        PanelSection *section = sectionFor(s);
        if (!section || !section->header)
            continue;
        SectionHeader *h      = section->header;
        const int      bottom = h->mapTo(m_content, QPoint(0, 0)).y() + kSectionHeaderHeight;
        if (contentY < bottom)
        {
            target = s;
            break;
        }
    }
    if (PanelSection *section = sectionFor(target))
        section->header->setDragTarget(true);
}

void FilePropertiesPanel::syncSectionHeaderHover()
{
    for (SectionId s : m_sectionOrder)
        if (PanelSection *section = sectionFor(s))
            section->header->syncHoverFromCursor();
}

void FilePropertiesPanel::scheduleSectionHeaderHoverSync()
{
    QTimer::singleShot(0, this, &FilePropertiesPanel::syncSectionHeaderHover);
    QTimer::singleShot(16, this, &FilePropertiesPanel::syncSectionHeaderHover);
    QTimer::singleShot(50, this, &FilePropertiesPanel::syncSectionHeaderHover);
}

void FilePropertiesPanel::requestSectionLayoutRefresh(SectionId sectionId)
{
    if (m_sectionLayoutRefreshPending || isSectionCollapsed(sectionId))
        return;

    m_sectionLayoutRefreshPending = true;
    QTimer::singleShot(0, this, &FilePropertiesPanel::performSectionLayoutRefresh);
    QTimer::singleShot(16, this,
                       [this]()
                       {
                           performSectionLayoutRefresh();
                           m_sectionLayoutRefreshPending = false;
                       });
}

void FilePropertiesPanel::performSectionLayoutRefresh()
{
    if (!m_content || !m_content->layout())
        return;

    for (SectionId sectionId : std::as_const(m_sectionOrder))
    {
        if (isSectionCollapsed(sectionId))
            continue;
        if (PanelSection *section = sectionFor(sectionId))
        {
            if (section->body)
            {
                section->body->setMaximumHeight(QWIDGETSIZE_MAX);
                section->body->updateGeometry();
            }
            if (section->resize.target)
                applyResizableSectionHeight(sectionId);
        }
    }

    rebuildSectionLayout();
    settleContentLayout();
    updateStickyHeader();

    // If a section is pinned to full-expand, re-apply the scroll after every
    // layout refresh so transient churn (e.g. progress bar show/hide on first
    // open) cannot leave the panel at the wrong scroll position.
    if (m_hasExpandedSection && m_scrollArea && m_content)
    {
        if (PanelSection *s = sectionFor(m_expandedSectionId))
        {
            if (s->header)
            {
                const int y = s->header->mapTo(m_content, QPoint(0, 0)).y();
                ++m_programmaticScrollDepth;
                m_scrollArea->verticalScrollBar()->setValue(qMax(0, y - kContentMargin));
                QTimer::singleShot(0, this, [this] { --m_programmaticScrollDepth; });
            }
        }
    }
}

void FilePropertiesPanel::settleContentLayout()
{
    if (!m_content || !m_content->layout())
        return;

    QLayout *contentLayout = m_content->layout();
    m_content->setUpdatesEnabled(false);
    contentLayout->invalidate();
    contentLayout->activate();
    const int targetHeight =
        qMax(m_scrollArea ? m_scrollArea->viewport()->height() : 0, contentLayout->sizeHint().height());
    m_content->resize(m_content->width(), targetHeight);
    contentLayout->setGeometry(m_content->rect());
    m_content->setUpdatesEnabled(true);
    m_content->updateGeometry();
    m_content->update();
}

void FilePropertiesPanel::toggleSectionFullExpand(SectionId sectionId)
{
    PanelSection *section = sectionFor(sectionId);
    if (!section || !section->resize.target)
        return;

    if (section->collapsed)
        setSectionCollapsed(sectionId, false, false);

    if (m_hasExpandedSection && m_expandedSectionId == sectionId)
    {
        // Restore previous size and other sections' collapsed state.
        m_hasExpandedSection          = false;
        section->resize.currentHeight = m_preExpandHeight;
        if (section->header)
            section->header->setSectionExpanded(false);

        const auto states = std::exchange(m_preExpandCollapsedStates, {});
        for (const auto &pair : states)
            setSectionCollapsed(pair.first, pair.second, false);

        applyResizableSectionHeight(sectionId);
        rebuildSectionLayout();
        settleContentLayout();
    }
    else
    {
        // If another section was expanded, restore it first.
        if (m_hasExpandedSection)
        {
            if (PanelSection *prev = sectionFor(m_expandedSectionId))
            {
                prev->resize.currentHeight = m_preExpandHeight;
                if (prev->header)
                    prev->header->setSectionExpanded(false);
            }
            const auto states = std::exchange(m_preExpandCollapsedStates, {});
            for (const auto &pair : states)
                setSectionCollapsed(pair.first, pair.second, false);
            m_hasExpandedSection = false;
        }

        m_hasExpandedSection = true;
        m_expandedSectionId  = sectionId;
        m_preExpandHeight =
            section->resize.currentHeight > 0 ? section->resize.currentHeight : section->resize.minHeight;

        for (SectionId s : m_sectionOrder)
        {
            if (s == sectionId)
                continue;
            m_preExpandCollapsedStates.append({s, isSectionCollapsed(s)});
            if (!isSectionCollapsed(s))
                setSectionCollapsed(s, true, false);
        }

        // Size the list so the whole section fits within the viewport, with
        // kContentMargin of breathing room at the top.
        const int viewportHeight   = m_scrollArea ? m_scrollArea->viewport()->height() : 400;
        const int opH              = (section->operation && section->operation->widget()->isVisible())
                                         ? section->operation->widget()->sizeHint().height()
                                         : 0;
        const int sectionOverheadH = kSectionHeaderHeight + kHeaderControlGap + opH;
        const int bodyNonListH     = (section->body && section->resize.target)
                                         ? qMax(0, section->body->height() - section->resize.target->height())
                                         : 0;
        const int availH           = viewportHeight - kContentMargin - sectionOverheadH - bodyNonListH;
        section->resize.currentHeight = qMax(section->resize.minHeight, availH);
        applyResizableSectionHeight(sectionId);
        rebuildSectionLayout();
        settleContentLayout();

        if (section->header)
            section->header->setSectionExpanded(true);

        // Scroll so the section header sits kContentMargin below the viewport top.
        if (m_scrollArea && section->header)
        {
            QTimer::singleShot(0, this,
                               [this, sectionId]()
                               {
                                   PanelSection *s = sectionFor(sectionId);
                                   if (!s || !s->header || !m_scrollArea)
                                       return;
                                   const int y = s->header->mapTo(m_content, QPoint(0, 0)).y();
                                   ++m_programmaticScrollDepth;
                                   m_scrollArea->verticalScrollBar()->setValue(qMax(0, y - kContentMargin));
                                   QTimer::singleShot(0, this, [this] { --m_programmaticScrollDepth; });
                               });
        }
    }
    updateStickyHeader();
}

void FilePropertiesPanel::clearSectionFullExpand(SectionId sectionId)
{
    if (!m_hasExpandedSection || m_expandedSectionId != sectionId)
        return;

    m_hasExpandedSection  = false;
    PanelSection *section = sectionFor(sectionId);
    if (section)
    {
        section->resize.currentHeight = m_preExpandHeight;
        if (section->header)
            section->header->setSectionExpanded(false);
    }

    const auto states = std::exchange(m_preExpandCollapsedStates, {});
    for (const auto &pair : states)
        setSectionCollapsed(pair.first, pair.second, false);
}

SidePanelHost::SidePanelHost(HexView *hexView, QWidget *parent) : QWidget(parent), m_hexView(hexView)
{
    setAcceptDrops(true);
    setMinimumWidth(0);
    setMaximumWidth(0);
    hide();

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_resizeHandle = new SidePanelResizeGrip(this);
    m_resizeHandle->installEventFilter(this);
    layout->addWidget(m_resizeHandle);

    m_widthAnim = new QPropertyAnimation(this, "maximumWidth", this);
    m_widthAnim->setDuration(kFileInfoPaneAnimMs);
    m_widthAnim->setEasingCurve(QEasingCurve::OutCubic);
}

bool SidePanelHost::isOpen() const
{
    return m_panel != nullptr;
}

void SidePanelHost::toggle()
{
    if (m_panel)
    {
        closePanel();
        return;
    }

    openSection(FilePropertiesPanel::SectionId::Properties);
}

void SidePanelHost::openSection(FilePropertiesPanel::SectionId section)
{
    if (m_panel)
    {
        m_panel->showSection(section);
        if (!isVisible() || maximumWidth() < m_paneWidth)
            setExpanded(true);
        emit openChanged(true);
        return;
    }

    auto *panel = new FilePropertiesPanel(m_hexView, this);
    panel->setWindowFlags(Qt::Widget);
    panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_panel = panel;
    if (auto *hostLayout = qobject_cast<QHBoxLayout *>(layout()))
        hostLayout->addWidget(panel);
    panel->show();

    connect(panel, &FilePropertiesPanel::closeRequested, this, &SidePanelHost::closePanel);

    panel->showSection(section);
    emit openChanged(true);
    setExpanded(true);
}

void SidePanelHost::closePanel()
{
    emit openChanged(false);
    setExpanded(false);
}

void SidePanelHost::refreshPanel()
{
    if (m_panel)
        m_panel->refresh();
}

void SidePanelHost::resetPanelForCurrentDocument()
{
    if (m_panel)
        m_panel->resetForCurrentDocument();
}

void SidePanelHost::setExpanded(bool expanded)
{
    if (!m_widthAnim)
        return;

    m_widthAnim->stop();
    m_widthAnim->disconnect();

    if (m_panel)
        m_panel->setPanelFullyOpened(false);

    setMinimumWidth(0);
    if (expanded)
        show();

    m_widthAnim->setEasingCurve(expanded ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
    m_widthAnim->setStartValue(maximumWidth());
    m_widthAnim->setEndValue(expanded ? m_paneWidth : 0);

    if (expanded)
    {
        connect(
            m_widthAnim, &QPropertyAnimation::finished, this,
            [this]()
            {
                if (maximumWidth() == m_paneWidth)
                {
                    setMinimumWidth(m_paneWidth);
                    if (m_panel)
                        m_panel->setPanelFullyOpened(true);
                }
            },
            Qt::SingleShotConnection);
    }
    else
    {
        connect(
            m_widthAnim, &QPropertyAnimation::finished, this,
            [this]()
            {
                if (maximumWidth() == 0)
                {
                    if (m_panel)
                    {
                        m_panel->deleteLater();
                        m_panel = nullptr;
                    }
                    hide();
                }
            },
            Qt::SingleShotConnection);
    }

    m_widthAnim->start();
}

void SidePanelHost::setPaneWidth(int width)
{
    m_paneWidth = qBound(kFileInfoPaneMinWidth, width, kFileInfoPaneMaxWidth);
    setMinimumWidth(m_paneWidth);
    setMaximumWidth(m_paneWidth);
}

bool SidePanelHost::eventFilter(QObject *obj, QEvent *event)
{
    if (obj != m_resizeHandle)
        return QWidget::eventFilter(obj, event);

    const auto type = event->type();
    if (type == QEvent::MouseButtonPress)
    {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton && isVisible())
        {
            m_widthAnim->stop();
            m_resizing         = true;
            m_resizeStartX     = me->globalPosition().x();
            m_resizeStartWidth = width();
            if (m_resizeHandle)
                static_cast<SidePanelResizeGrip *>(m_resizeHandle)->setActive(true);
            m_resizeHandle->grabMouse();
            return true;
        }
    }

    if (type == QEvent::MouseMove && m_resizing)
    {
        auto     *me    = static_cast<QMouseEvent *>(event);
        const int delta = qRound(me->globalPosition().x() - m_resizeStartX);
        setPaneWidth(m_resizeStartWidth - delta);
        return true;
    }

    if ((type == QEvent::MouseButtonRelease || type == QEvent::UngrabMouse) && m_resizing)
    {
        if (m_resizeHandle)
            m_resizeHandle->releaseMouse();
        if (m_resizeHandle)
            static_cast<SidePanelResizeGrip *>(m_resizeHandle)->setActive(false);
        m_resizing = false;
        return type == QEvent::MouseButtonRelease;
    }

    return QWidget::eventFilter(obj, event);
}
