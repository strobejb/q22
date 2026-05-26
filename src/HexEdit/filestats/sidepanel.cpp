#include "filestats/sidepanel.h"

#include "HexView/hexview.h"
#include "combos/menucombobox.h"
#include "filestats/widgets.h"
#include "settings/scrollhintoverlay.h"
#include "settings/settingscard.h"
#include "theme.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QBrush>
#include <QClipboard>
#include <QComboBox>
#include <QCryptographicHash>
#include <QCursor>
#include <QDesktopServices>
#include <QEnterEvent>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QLocale>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPalette>
#include <QPainter>
#include <QPointer>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QHeaderView>
#include <QStyle>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariantMap>

#include <array>
#include <algorithm>
#include <functional>
#include <utility>

using namespace filestats;

FilePropertiesPanel::FilePropertiesPanel(HexView *hexView, QWidget *parent)
    : QDialog(parent), m_hexView(hexView)
{
    setWindowTitle(tr("File Properties"));
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
    contentLayout->setContentsMargins(kSectionHeaderOuterMargin, kContentMargin,
                                      kSectionHeaderOuterMargin, kContentMargin);
    contentLayout->setSpacing(0);

    m_fileHeader = new SectionHeader(tr("File Properties"), m_content);
    m_fileHeader->setClickedCallback([this]() {
        setFileSectionCollapsed(!m_fileSectionCollapsed);
    });
    contentLayout->addWidget(m_fileHeader);
    contentLayout->setAlignment(m_fileHeader, Qt::AlignLeft);
    m_fileHeader->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_fileHeaderGap = new QSpacerItem(0, kHeaderControlGap, QSizePolicy::Minimum, QSizePolicy::Fixed);
    contentLayout->addSpacerItem(m_fileHeaderGap);

    m_fileSectionBody = new QWidget(m_content);
    m_fileSectionBody->setMinimumWidth(0);
    auto *fileBodyLayout = new QVBoxLayout(m_fileSectionBody);
    fileBodyLayout->setContentsMargins(kSectionHeaderOuterMargin + kCardLeftInset, 0,
                                       kSectionHeaderOuterMargin + kCardScrollbarInset, 0);
    fileBodyLayout->setSpacing(0);

    auto *card = new SettingsCard({
        new PropertyRow(tr("Name"), &m_nameValue, m_fileSectionBody),
        new PropertyRow(tr("Location"), &m_locationValue, m_fileSectionBody,
                        PropertyRow::Action::OpenExternal,
                        [this]() {
                            if (!m_hexView)
                                return;
                            const QString path = m_hexView->filePath();
                            if (!path.isEmpty())
                                QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
                        }),
        new PropertyRow(tr("Size"), &m_sizeValue, m_fileSectionBody),
        new PropertyRow(tr("State"), &m_stateValue, m_fileSectionBody),
    }, SettingsCard::Style::Spaced, m_fileSectionBody);
    card->setMinimumWidth(0);
    fileBodyLayout->addWidget(card);
    contentLayout->addWidget(m_fileSectionBody);

    m_betweenSectionsGap = new QSpacerItem(0, kGroupTopGap, QSizePolicy::Minimum, QSizePolicy::Fixed);
    contentLayout->addSpacerItem(m_betweenSectionsGap);
    m_checksumHeader = new SectionHeader(tr("Checksums"), m_content);
    m_checksumHeader->setClickedCallback([this]() {
        setChecksumSectionCollapsed(!m_checksumSectionCollapsed);
    });
    contentLayout->addWidget(m_checksumHeader);
    contentLayout->setAlignment(m_checksumHeader, Qt::AlignLeft);
    m_checksumHeader->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_checksumHeaderGap = new QSpacerItem(0, kHeaderControlGap, QSizePolicy::Minimum, QSizePolicy::Fixed);
    contentLayout->addSpacerItem(m_checksumHeaderGap);

    m_checksumSectionBody = new QWidget(m_content);
    m_checksumSectionBody->setMinimumWidth(0);
    auto *checksumBodyLayout = new QVBoxLayout(m_checksumSectionBody);
    checksumBodyLayout->setContentsMargins(kSectionHeaderOuterMargin + kCardLeftInset, 0,
                                           kSectionHeaderOuterMargin + kCardScrollbarInset, 0);
    checksumBodyLayout->setSpacing(0);

    m_checksumOperation = new SectionOperationStrip(m_content,
        [this]() { cancelChecksumCalculation(); },
        [this]() {
            m_checksumStarted = false;
            maybeStartChecksumCalculation();
        });
    contentLayout->addWidget(m_checksumOperation->widget());

    auto checksumRow = [this](const QString &name) {
        QLabel *value = nullptr;
        auto *row = new PropertyRow(name, &value, m_checksumSectionBody);
        m_checksumValues.insert(name, value);
        return row;
    };

    auto *checksumCard = new SettingsCard({
        checksumRow(QStringLiteral("SHA1")),
        checksumRow(QStringLiteral("SHA256")),
        checksumRow(QStringLiteral("MD2")),
        checksumRow(QStringLiteral("MD4")),
        checksumRow(QStringLiteral("MD5")),
        checksumRow(QStringLiteral("CRC16")),
        checksumRow(QStringLiteral("CRC32")),
    }, SettingsCard::Style::Spaced, m_checksumSectionBody);
    checksumCard->setMinimumWidth(0);
    checksumBodyLayout->addWidget(checksumCard);
    contentLayout->addWidget(m_checksumSectionBody);

    m_betweenChecksumStringsGap = new QSpacerItem(0, kGroupTopGap, QSizePolicy::Minimum, QSizePolicy::Fixed);
    contentLayout->addSpacerItem(m_betweenChecksumStringsGap);
    m_stringsHeader = new SectionHeader(tr("Strings"), m_content);
    m_stringsHeader->setClickedCallback([this]() {
        setStringsSectionCollapsed(!m_stringsSectionCollapsed);
    });
    contentLayout->addWidget(m_stringsHeader);
    contentLayout->setAlignment(m_stringsHeader, Qt::AlignLeft);
    m_stringsHeader->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_stringsHeaderGap = new QSpacerItem(0, kHeaderControlGap, QSizePolicy::Minimum, QSizePolicy::Fixed);
    contentLayout->addSpacerItem(m_stringsHeaderGap);

    m_stringsSectionBody = new QWidget(m_content);
    m_stringsSectionBody->setMinimumWidth(0);
    auto *stringsBodyLayout = new QVBoxLayout(m_stringsSectionBody);
    stringsBodyLayout->setContentsMargins(kSectionHeaderOuterMargin + kCardLeftInset, 0,
                                          kSectionHeaderOuterMargin + kCardScrollbarInset, 0);
    stringsBodyLayout->setSpacing(0);

    m_stringsOperation = new SectionOperationStrip(m_content,
        [this]() { cancelStringScan(); },
        [this]() {
            m_stringsStarted = false;
            if (m_stringsList)
                m_stringsList->clear();
            maybeStartStringScan();
        });
    contentLayout->addWidget(m_stringsOperation->widget());

    auto *stringsControlsStack = new QWidget(m_stringsSectionBody);
    auto *stringsControlsStackLayout = new QVBoxLayout(stringsControlsStack);
    stringsControlsStackLayout->setContentsMargins(kSettingsCardShadowInset, 0,
                                                   kSettingsCardShadowInset, 0);
    stringsControlsStackLayout->setSpacing(0);

    m_stringEncoding = new MenuComboBox(m_stringsSectionBody);
    m_stringEncoding->addItems({tr("Ascii"), tr("Unicode"), tr("Ascii and Unicode")});
    m_stringEncoding->setFocusPolicy(Qt::StrongFocus);
    m_stringEncoding->setFixedHeight(qMax(24, m_stringEncoding->sizeHint().height() - 4));

    m_minStringLength = new StepSpinBox(tr("Characters"), 3, 128, 1, m_stringsSectionBody);
    m_minStringLength->setValue(3);
    m_minStringLength->setLabelAlignment(Qt::AlignRight);

    auto *stringsControls = new QWidget(m_stringsSectionBody);
    auto *stringsControlsLayout = new QHBoxLayout(stringsControls);
    stringsControlsLayout->setContentsMargins(0, 0, 0, 0);
    stringsControlsLayout->setSpacing(kContentMargin + 6);
    stringsControlsLayout->addWidget(m_stringEncoding, 1, Qt::AlignVCenter);
    stringsControlsLayout->addWidget(m_minStringLength, 0);
    stringsControlsStackLayout->addWidget(stringsControls);
    stringsControlsStackLayout->addSpacing(kHeaderControlGap + 4);

    auto *stringsListFrame = new StringListFrame(m_stringsSectionBody);
    stringsListFrame->setObjectName(QStringLiteral("stringsListFrame"));
    stringsListFrame->setFixedHeight(kStringsListMinHeight);
    stringsListFrame->setStyleSheet(QStringLiteral(R"(
        QFrame#stringsListFrame {
            background: palette(base);
            border: 1px solid palette(mid);
            border-radius: 6px;
        }
    )"));

    m_stringsList = new QTreeWidget(stringsListFrame);
    m_stringsList->setMinimumSize(0, 0);
    m_stringsList->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_stringsList->setColumnCount(2);
    m_stringsList->setHeaderHidden(true);
    m_stringsList->setRootIsDecorated(false);
    m_stringsList->setAlternatingRowColors(false);
    m_stringsList->setUniformRowHeights(true);
    m_stringsList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_stringsList->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_stringsList->header()->setStretchLastSection(false);
    m_stringsList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_stringsList->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_stringsList->setStyleSheet(QStringLiteral(R"(
        QTreeWidget {
            background: palette(base);
            border: none;
            padding: 0px;
        }
        QTreeWidget::item {
            padding: 3px 6px;
        }
        QTreeWidget::item:hover {
            background: palette(button);
        }
        QTreeWidget::item:selected {
            background: palette(highlight);
            color: palette(highlighted-text);
        }
    )"));
    stringsListFrame->setListWidget(m_stringsList);
    stringsControlsStackLayout->addWidget(stringsListFrame);

    auto *stringsResizeWrap = new QWidget(m_stringsSectionBody);
    auto *stringsResizeLayout = new QVBoxLayout(stringsResizeWrap);
    stringsResizeLayout->setContentsMargins(0, 0, 0, 0);
    stringsResizeLayout->setSpacing(0);
    m_stringsResizeHandle = new VerticalResizeHandle([this](int dy) {
        resizeStringsList(dy);
    }, stringsResizeWrap);
    stringsResizeLayout->addWidget(m_stringsResizeHandle);
    stringsResizeLayout->setAlignment(m_stringsResizeHandle, Qt::AlignTop);
    stringsControlsStackLayout->addWidget(stringsResizeWrap);
    stringsBodyLayout->addWidget(stringsControlsStack);
    contentLayout->addWidget(m_stringsSectionBody);
    m_stringsResizeSlack = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Fixed);
    contentLayout->addSpacerItem(m_stringsResizeSlack);
    contentLayout->addStretch();

    m_scrollArea->setWidget(m_content);
    root->addWidget(m_scrollArea, 1);

    m_stickyHeader = new SectionHeader(QString(), m_scrollArea->viewport());
    m_stickyHeader->hide();
    m_stickyHeader->raise();
    m_stickyHeader->setClickedCallback([this]() {
        if (!m_scrollArea)
            return;
        const int fileY = m_fileHeader->mapTo(m_scrollArea->viewport(), QPoint(0, 0)).y();
        const int checksumY = m_checksumHeader->mapTo(m_scrollArea->viewport(), QPoint(0, 0)).y();
        const int stringsY = m_stringsHeader->mapTo(m_scrollArea->viewport(), QPoint(0, 0)).y();
        if (stringsY <= 0 || (checksumY > 0 && stringsY < checksumY && fileY > 0))
            setStringsSectionCollapsed(!m_stringsSectionCollapsed);
        else if (checksumY <= 0 || (fileY > 0 && checksumY < fileY))
            setChecksumSectionCollapsed(!m_checksumSectionCollapsed);
        else
            setFileSectionCollapsed(!m_fileSectionCollapsed);
    });
    connect(this, &FilePropertiesPanel::sectionReady,
            this, [this](Section section) {
                if (section == Section::Checksums)
                    maybeStartChecksumCalculation();
                else if (section == Section::Strings)
                    maybeStartStringScan();
            });
    connect(m_minStringLength, &StepSpinBox::valueChanged, this, [this](int) {
        m_stringsStarted = false;
        if (m_stringsList)
            m_stringsList->clear();
        maybeStartStringScan();
    });
    connect(m_stringEncoding, &QComboBox::currentIndexChanged, this, [this](int) {
        m_stringsStarted = false;
        if (m_stringsList)
            m_stringsList->clear();
        maybeStartStringScan();
    });
    auto navigateToStringItem = [this](QTreeWidgetItem *item, int) {
        if (!item || !m_hexView)
            return;
        const size_w offset = static_cast<size_w>(item->data(0, Qt::UserRole).toULongLong());
        const size_w length = static_cast<size_w>(item->data(0, Qt::UserRole + 1).toULongLong());
        m_hexView->setCurSel(offset, qMin(offset + length, m_hexView->size()));
        m_hexView->scrollCenterIfOffScreen(offset);
        m_hexView->setFocus();
    };
    connect(m_stringsList, &QTreeWidget::itemClicked, this, navigateToStringItem);
    connect(m_stringsList, &QTreeWidget::itemActivated, this, navigateToStringItem);
    connect(m_scrollArea->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &FilePropertiesPanel::updateStickyHeader);
    connect(m_scrollArea->verticalScrollBar(), &QScrollBar::rangeChanged,
            this, [this]() { updateStickyHeader(); });
    QTimer::singleShot(0, this, &FilePropertiesPanel::updateStickyHeader);

    setMinimumWidth(260);
    refresh();
    setFileSectionCollapsed(true);
    setChecksumSectionCollapsed(true);
    setStringsSectionCollapsed(true);
}

FilePropertiesPanel::~FilePropertiesPanel()
{
    ++m_checksumGeneration;
    ++m_stringGeneration;
    if (m_checksumCancel)
        m_checksumCancel->store(true);
    if (m_stringCancel)
        m_stringCancel->store(true);
}

void FilePropertiesPanel::showSection(Section section)
{
    if (section == Section::Properties)
        setFileSectionCollapsed(false);
    else if (section == Section::Checksums)
        setChecksumSectionCollapsed(false);
    else
        setStringsSectionCollapsed(false);
}

void FilePropertiesPanel::setPanelFullyOpened(bool opened)
{
    m_panelFullyOpened = opened;
    if (opened) {
        if (!m_fileSectionCollapsed)
            emitSectionReadyIfPossible(Section::Properties);
        if (!m_checksumSectionCollapsed)
            emitSectionReadyIfPossible(Section::Checksums);
        if (!m_stringsSectionCollapsed)
            emitSectionReadyIfPossible(Section::Strings);
    }
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

void FilePropertiesPanel::setFileSectionCollapsed(bool collapsed)
{
    const bool wasCollapsed = m_fileSectionCollapsed;
    m_fileSectionCollapsed = collapsed;
    if (m_fileSectionBody)
        m_fileSectionBody->setVisible(!collapsed);
    if (m_fileHeaderGap)
        m_fileHeaderGap->changeSize(0, collapsed ? 0 : kHeaderControlGap,
                                    QSizePolicy::Minimum, QSizePolicy::Fixed);
    if (m_betweenSectionsGap)
        m_betweenSectionsGap->changeSize(0, collapsed ? kHeaderControlGap : kGroupTopGap,
                                         QSizePolicy::Minimum, QSizePolicy::Fixed);
    if (m_fileHeader)
        m_fileHeader->setCollapsed(collapsed);
    if (m_content && m_content->layout())
        m_content->layout()->invalidate();
    if (wasCollapsed && !collapsed) {
        emit sectionExpanded(Section::Properties);
        emitSectionReadyIfPossible(Section::Properties);
    }
    updateStickyHeader();
    QTimer::singleShot(0, this, &FilePropertiesPanel::updateStickyHeader);
}

void FilePropertiesPanel::setChecksumSectionCollapsed(bool collapsed)
{
    const bool wasCollapsed = m_checksumSectionCollapsed;
    m_checksumSectionCollapsed = collapsed;
    if (m_checksumSectionBody)
        m_checksumSectionBody->setVisible(!collapsed);
    if (m_checksumOperation)
        m_checksumOperation->setCollapsed(collapsed);
    if (m_checksumHeaderGap)
        m_checksumHeaderGap->changeSize(0, collapsed ? 0 : kHeaderControlGap,
                                        QSizePolicy::Minimum, QSizePolicy::Fixed);
    if (m_betweenChecksumStringsGap)
        m_betweenChecksumStringsGap->changeSize(0, collapsed ? kHeaderControlGap : kGroupTopGap,
                                                QSizePolicy::Minimum, QSizePolicy::Fixed);
    if (m_checksumHeader)
        m_checksumHeader->setCollapsed(collapsed);
    if (m_content && m_content->layout())
        m_content->layout()->invalidate();
    if (wasCollapsed && !collapsed) {
        emit sectionExpanded(Section::Checksums);
        emitSectionReadyIfPossible(Section::Checksums);
    }
    updateStickyHeader();
    QTimer::singleShot(0, this, &FilePropertiesPanel::updateStickyHeader);
}

void FilePropertiesPanel::setStringsSectionCollapsed(bool collapsed)
{
    const bool wasCollapsed = m_stringsSectionCollapsed;
    m_stringsSectionCollapsed = collapsed;
    if (m_stringsSectionBody)
        m_stringsSectionBody->setVisible(!collapsed);
    if (m_stringsOperation)
        m_stringsOperation->setCollapsed(collapsed);
    if (m_stringsHeaderGap)
        m_stringsHeaderGap->changeSize(0, collapsed ? 0 : kHeaderControlGap,
                                       QSizePolicy::Minimum, QSizePolicy::Fixed);
    if (m_stringsHeader)
        m_stringsHeader->setCollapsed(collapsed);
    if (m_content && m_content->layout())
        m_content->layout()->invalidate();
    if (wasCollapsed && !collapsed) {
        emit sectionExpanded(Section::Strings);
        emitSectionReadyIfPossible(Section::Strings);
    }
    updateStickyHeader();
    QTimer::singleShot(0, this, &FilePropertiesPanel::updateStickyHeader);
}

void FilePropertiesPanel::emitSectionReadyIfPossible(Section section)
{
    if (!m_panelFullyOpened)
        return;

    bool sectionExpanded = false;
    if (section == Section::Properties)
        sectionExpanded = !m_fileSectionCollapsed;
    else if (section == Section::Checksums)
        sectionExpanded = !m_checksumSectionCollapsed;
    else
        sectionExpanded = !m_stringsSectionCollapsed;
    if (sectionExpanded)
        emit sectionReady(section);
}

void FilePropertiesPanel::syncStickyHeader()
{
    if (!m_scrollArea || !m_stickyHeader || !m_fileHeader || !m_checksumHeader || !m_stringsHeader)
        return;

    const int headerWidth = qMax(1, m_scrollArea->viewport()->width()
                                       - 2 * kSectionHeaderOuterMargin);
    if (m_fileHeader->width() != headerWidth)
        m_fileHeader->setFixedWidth(headerWidth);
    if (m_checksumHeader->width() != headerWidth)
        m_checksumHeader->setFixedWidth(headerWidth);
    if (m_stringsHeader->width() != headerWidth)
        m_stringsHeader->setFixedWidth(headerWidth);

    const int fileY = m_fileHeader->mapTo(m_scrollArea->viewport(), QPoint(0, 0)).y();
    const int checksumY = m_checksumHeader->mapTo(m_scrollArea->viewport(), QPoint(0, 0)).y();
    const int stringsY = m_stringsHeader->mapTo(m_scrollArea->viewport(), QPoint(0, 0)).y();
    Section activeSection = Section::Properties;
    int nextHeaderY = checksumY;
    if (checksumY <= 0) {
        activeSection = Section::Checksums;
        nextHeaderY = stringsY;
    }
    if (stringsY <= 0) {
        activeSection = Section::Strings;
        nextHeaderY = kSectionHeaderHeight;
    }

    if (activeSection == Section::Strings) {
        m_stickyHeader->setTitle(m_stringsHeader->title());
        m_stickyHeader->setCollapsed(m_stringsSectionCollapsed);
        m_stickyHeader->setClickedCallback([this]() {
            setStringsSectionCollapsed(!m_stringsSectionCollapsed);
        });
    } else if (activeSection == Section::Checksums) {
        m_stickyHeader->setTitle(m_checksumHeader->title());
        m_stickyHeader->setCollapsed(m_checksumSectionCollapsed);
        m_stickyHeader->setClickedCallback([this]() {
            setChecksumSectionCollapsed(!m_checksumSectionCollapsed);
        });
    } else {
        m_stickyHeader->setTitle(m_fileHeader->title());
        m_stickyHeader->setCollapsed(m_fileSectionCollapsed);
        m_stickyHeader->setClickedCallback([this]() {
            setFileSectionCollapsed(!m_fileSectionCollapsed);
        });
    }

    const bool shouldStick = activeSection != Section::Properties || fileY <= 0;
    if (!shouldStick) {
        m_stickyHeader->hide();
        return;
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
